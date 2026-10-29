#include "kpanic.h"
#include "vga.h"
#include "mode13.h"
#include "sound.h"
#include "ac97.h"
#include "khex.h"
#include "kirillfs.h"
#include <stdint.h>

/* Stack layout built by kpanic_common in kpanic_isr.asm (lowest address first) */
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_pushad, ebx, edx, ecx, eax;   /* pushad */
    uint32_t vector, error;
    uint32_t eip, cs, eflags;                                 /* pushed by the CPU */
} __attribute__((packed)) kpanic_frame_t;

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed)) idt_gate_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

extern const uint32_t kpanic_isr_table[256];
extern void kpanic_irq_master_stub(void);
extern void kpanic_irq_slave_stub(void);
extern const uint8_t kernel_text_start[], kernel_text_end[];   /* linker.ld */

void kpanic_handle(kpanic_frame_t* frame) __attribute__((noreturn));

static idt_gate_t idt[256] __attribute__((aligned(8)));
static const char* volatile panic_message;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* ================================================================ */
/*                          IDT setup                               */
/* ================================================================ */
void kpanic_init(void) {
    uint16_t cs;
    /* kboot and GRUB use different code selectors: take whatever we run on */
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));

    for (int i = 0; i < 256; i++) {
        uint32_t handler = kpanic_isr_table[i];
        if (i >= 0x20 && i < 0x28) handler = (uint32_t)(uintptr_t)kpanic_irq_master_stub;
        else if (i >= 0x28 && i < 0x30) handler = (uint32_t)(uintptr_t)kpanic_irq_slave_stub;

        idt[i].offset_low  = (uint16_t)(handler & 0xFFFF);
        idt[i].offset_high = (uint16_t)(handler >> 16);
        idt[i].selector    = cs;
        idt[i].zero        = 0;
        idt[i].type_attr   = 0x8E;   /* present, ring 0, 32-bit interrupt gate */
    }

    idt_ptr_t idtr = { (uint16_t)(sizeof(idt) - 1), (uint32_t)(uintptr_t)idt };
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}

void kirill_panic(const char* message) {
    panic_message = message;
    __asm__ volatile ("int %0" : : "i"(KPANIC_SOFT_VECTOR));
    for (;;) __asm__ volatile ("cli; hlt");
}

/* ================================================================ */
/*                     Direct VGA text output                       */
/* ================================================================ */
/* The panic screen never goes through vga.c: its output hook may point into KGUI */
#define PANIC_BG VGA_RED
#define BAR_ATTR ((uint8_t)((VGA_LIGHT_GREY << 4) | VGA_RED))   /* bg >= 8 would blink */

static volatile uint16_t* const screen = (volatile uint16_t*)0xB8000;
static int     prow, pcol;
static uint8_t pattr;

static void ink(uint8_t fg) { pattr = (uint8_t)((PANIC_BG << 4) | fg); }
static void at(int row, int col) { prow = row; pcol = col; }

static void put(char c) {
    if (prow >= 0 && prow < VGA_HEIGHT && pcol >= 0 && pcol < VGA_WIDTH)
        screen[prow * VGA_WIDTH + pcol] = (uint16_t)((uint8_t)c | ((uint16_t)pattr << 8));
    pcol++;
}

static void text(const char* s) { while (*s) put(*s++); }

static int text_length(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void hex(uint32_t value, int digits) {
    static const char digit[] = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; i--) put(digit[(value >> (i * 4)) & 0xF]);
}

static void dec(uint32_t value) {
    char buf[10];
    int n = 0;
    do { buf[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) put(buf[--n]);
}

static void fill_row(int row, uint8_t attr) {
    for (int col = 0; col < VGA_WIDTH; col++)
        screen[row * VGA_WIDTH + col] = (uint16_t)(' ' | ((uint16_t)attr << 8));
}

static void label(int row, const char* name) {
    at(row, 1);
    ink(VGA_YELLOW);
    text(name);
    ink(VGA_WHITE);
}

static void reg(const char* name, uint32_t value, int digits) {
    ink(VGA_YELLOW);
    text(name);
    put('=');
    ink(VGA_WHITE);
    hex(value, digits);
    text("  ");
}

static void separator(int row) {
    at(row, 0);
    ink(VGA_LIGHT_RED);
    for (int col = 0; col < VGA_WIDTH; col++) put((char)0xC4);   /* CP437 ─ */
}

/* ================================================================ */
/*                       Big "KIRILL PANIC"                         */
/* ================================================================ */
static const struct {
    char ch;
    const char* rows[5];
} glyphs[] = {
    { 'K', { "#   #", "#  # ", "###  ", "#  # ", "#   #" } },
    { 'I', { "###", " # ", " # ", " # ", "###" } },
    { 'R', { "#### ", "#   #", "#### ", "#  # ", "#   #" } },
    { 'L', { "#    ", "#    ", "#    ", "#    ", "#####" } },
    { 'P', { "#### ", "#   #", "#### ", "#    ", "#    " } },
    { 'A', { " ### ", "#   #", "#####", "#   #", "#   #" } },
    { 'N', { "#   #", "##  #", "# # #", "#  ##", "#   #" } },
    { 'C', { " ####", "#    ", "#    ", "#    ", " ####" } },
    { ' ', { "  ", "  ", "  ", "  ", "  " } },
};

static int glyph_index(char c) {
    for (int i = 0; i < (int)(sizeof(glyphs) / sizeof(glyphs[0])); i++)
        if (glyphs[i].ch == c) return i;
    return -1;
}

/* Draws the banner once at (top, left); returns its width */
static int draw_banner(const char* word, int top, int left, uint8_t fg, int draw) {
    int col = left;
    for (const char* c = word; *c; c++) {
        int g = glyph_index(*c);
        if (g < 0) continue;
        int width = text_length(glyphs[g].rows[0]);
        if (draw) {
            ink(fg);
            for (int r = 0; r < 5; r++) {
                for (int x = 0; x < width; x++) {
                    if (glyphs[g].rows[r][x] != '#') continue;
                    at(top + r, col + x);
                    put((char)0xDB);   /* CP437 █ */
                }
            }
        }
        col += width + 1;
    }
    return col - left - 1;
}

static void draw_big_title(int top) {
    static const char word[] = "KIRILL PANIC";
    int width = draw_banner(word, 0, 0, 0, 0);
    int left = (VGA_WIDTH - width) / 2;
    draw_banner(word, top, left, VGA_WHITE, 1);
}

static void centered(int row, const char* s, uint8_t fg) {
    at(row, (VGA_WIDTH - text_length(s)) / 2);
    ink(fg);
    text(s);
}

/* ================================================================ */
/*                      Exception knowledge                         */
/* ================================================================ */
static const char* const exception_names[32] = {
    "#DE Divide Error",            "#DB Debug",
    "NMI Non-Maskable Interrupt",  "#BP Breakpoint",
    "#OF Overflow",                "#BR BOUND Range Exceeded",
    "#UD Invalid Opcode",          "#NM Device Not Available",
    "#DF Double Fault",            "Coprocessor Segment Overrun",
    "#TS Invalid TSS",             "#NP Segment Not Present",
    "#SS Stack-Segment Fault",     "#GP General Protection Fault",
    "#PF Page Fault",              "Reserved exception",
    "#MF x87 Floating-Point Error", "#AC Alignment Check",
    "#MC Machine Check",           "#XM SIMD Floating-Point Error",
    "#VE Virtualization Exception", "#CP Control Protection",
    "Reserved exception",          "Reserved exception",
    "Reserved exception",          "Reserved exception",
    "Reserved exception",          "Reserved exception",
    "#HV Hypervisor Injection",    "#VC VMM Communication",
    "#SX Security Exception",      "Reserved exception",
};

static const char* exception_hint(uint32_t vector) {
    switch (vector) {
    case 0:  return "Division by zero, or a quotient too big (INT_MIN / -1)";
    case 1:  return "Debug trap fired, but there is no debugger to catch it";
    case 2:  return "Hardware reported a fatal error (memory parity, watchdog)";
    case 3:  return "INT3 breakpoint hit, but there is no debugger to catch it";
    case 4:  return "INTO found the overflow flag set";
    case 5:  return "BOUND found an array index out of range";
    case 6:  return "CPU met bytes it cannot run: bad jump or corrupt pointer";
    case 7:  return "FPU instruction while the FPU is off (CR0.EM or CR0.TS)";
    case 8:  return "A fault happened while the CPU was handling another fault";
    case 10: case 11:
             return "Bad descriptor in a segment or task switch";
    case 12: return "Stack segment limit hit or bad SS selector";
    case 13: return "Bad segment, privileged instruction or out-of-limit access";
    case 14: return "Paging is off in KirillOS, so this should never happen";
    case 17: return "Unaligned memory access with alignment checking on";
    case 18: return "The CPU detected an internal hardware error";
    default: return "CPU exception that KirillOS has no special handling for";
    }
}

static int has_error_code(uint32_t vector) {
    return vector == 8 || vector == 10 || vector == 11 || vector == 12 ||
           vector == 13 || vector == 14 || vector == 17 || vector == 21 ||
           vector == 29 || vector == 30;
}

static void print_error_code(const kpanic_frame_t* f, uint32_t cr2, int software) {
    uint32_t e = f->error;
    if (software || f->vector >= 32 || !has_error_code(f->vector)) {
        ink(VGA_LIGHT_GREY);
        text("none for this kind of stop");
        return;
    }
    text("0x");
    hex(e, 8);
    ink(VGA_LIGHT_GREY);
    if (f->vector == 14) {
        text("  -> ");
        text(e & 1 ? "protection violation" : "page not present");
        text(e & 2 ? ", write" : ", read");
        text(e & 4 ? ", user mode" : ", kernel mode");
        text(", address CR2=0x");
        hex(cr2, 8);
    } else if (f->vector >= 10 && f->vector <= 13 && e) {
        text("  -> ");
        if (e & 2) {
            text("IDT entry ");
            dec((e >> 3) & 0x1FFF);
        } else {
            text(e & 4 ? "LDT" : "GDT");
            text(" index ");
            dec((e >> 3) & 0x1FFF);
            text(" (selector 0x");
            hex(e & 0xFFFC, 4);
            put(')');
        }
        if (e & 1) text(", external event");
    } else if (e == 0) {
        text("  -> no segment selector involved");
    }
}

static void print_eflags(uint32_t eflags) {
    static const struct { uint8_t bit; const char* name; } flags[] = {
        { 11, "OF" }, { 10, "DF" }, { 9, "IF" }, { 8, "TF" }, { 7, "SF" },
        { 6, "ZF" }, { 4, "AF" }, { 2, "PF" }, { 0, "CF" },
    };
    ink(VGA_LIGHT_GREY);
    put('[');
    int first = 1;
    for (int i = 0; i < (int)(sizeof(flags) / sizeof(flags[0])); i++) {
        if (!(eflags & (1u << flags[i].bit))) continue;
        if (!first) put(' ');
        text(flags[i].name);
        first = 0;
    }
    text("] IOPL=");
    dec((eflags >> 12) & 3);
}

/* A value on the stack that points right after a CALL is probably a return address */
static int in_code(uint32_t value) {
    uint32_t text_start = (uint32_t)(uintptr_t)kernel_text_start;
    uint32_t text_end = (uint32_t)(uintptr_t)kernel_text_end;
    if (value >= text_start + 6 && value <= text_end) return 1;
    return value >= KHEX_LOAD_ADDR + 6 && value <= KHEX_LOAD_ADDR + KFS_DATA_MAX;
}

static int looks_like_return(uint32_t value) {
    if (!in_code(value)) return 0;
    const uint8_t* p = (const uint8_t*)(uintptr_t)value;
    if (p[-5] == 0xE8) return 1;                                /* call rel32 */
    if (p[-2] == 0xFF && ((p[-1] >> 3) & 7) == 2) return 1;     /* call reg / [reg] */
    if (p[-3] == 0xFF && ((p[-2] >> 3) & 7) == 2) return 1;     /* call [reg+disp8] */
    if (p[-6] == 0xFF && ((p[-5] >> 3) & 7) == 2) return 1;     /* call [disp32] */
    return 0;
}

static const char* kirill_says(uint32_t vector, uint32_t eip) {
    static const char* const quotes[] = {
        "Kirill says: it worked on my machine.",
        "Kirill says: this is not a bug, it is an undocumented feature.",
        "Kirill says: have you tried turning it off and on again?",
        "Kirill says: relax, I have seen worse. Press R.",
        "Kirill says: the compiler did it, not me.",
        "Kirill says: one more coffee and I will fix it.",
    };
    if (vector == 0) return "Kirill says: somebody divided by zero again.";
    if (vector == 6) return "Kirill says: the CPU does not speak this language.";
    return quotes[(eip ^ (eip >> 8) ^ vector) % (sizeof(quotes) / sizeof(quotes[0]))];
}

/* ================================================================ */
/*                          Reboot key                              */
/* ================================================================ */
static __attribute__((noreturn)) void reboot(void) {
    while (inb(0x64) & 2) { }
    outb(0x64, 0xFE);   /* 8042 reset line */

    /* Still alive: triple fault through an empty IDT */
    idt_ptr_t none = { 0, 0 };
    __asm__ volatile ("lidt %0; int3" : : "m"(none));
    for (;;) __asm__ volatile ("hlt");
}

static __attribute__((noreturn)) void wait_for_reboot(void) {
    for (;;) {
        uint8_t status = inb(0x64);
        if (!(status & 1)) {
            __asm__ volatile ("pause");
            continue;
        }
        uint8_t scancode = inb(0x60);
        if (status & 0x20) continue;    /* mouse byte */
        if (scancode == 0x13) reboot(); /* R pressed */
    }
}

/* ================================================================ */
/*                        The panic screen                          */
/* ================================================================ */
void kpanic_handle(kpanic_frame_t* f) {
    static int depth;
    if (depth++) {
        /* Faulted while drawing the panic: say so and stop */
        at(0, 0);
        pattr = 0x4F;
        text(" KIRILL PANIC: fault inside the panic handler, system halted ");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    uint32_t cr0, cr2, cr3, cr4;
    uint16_t ss;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile ("mov %%ss, %0" : "=r"(ss));
    /* Same privilege level: the CPU did not push SS:ESP, so the old stack starts right after EFLAGS */
    uint32_t esp = (uint32_t)(uintptr_t)&f->eflags + 4;
    int software = f->vector == KPANIC_SOFT_VECTOR && panic_message;

    /* Silence, then get back to 80x25 text no matter where we crashed */
    sound_stop();
    ac97_stop();
    vga_set_output_hook(0);
    if (mode13_is_active()) mode13_exit();
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);   /* hide the hardware cursor */

    for (int row = 0; row < VGA_HEIGHT; row++) fill_row(row, (uint8_t)((PANIC_BG << 4) | VGA_WHITE));

    /* Title bar */
    fill_row(0, BAR_ATTR);
    at(0, 1);
    pattr = BAR_ATTR;
    text("KIRILL PANIC  -  fatal system error");
    at(0, VGA_WIDTH - 15);
    text("KirillOS v0.2");

    draw_big_title(1);
    centered(7, "Kirill stopped KirillOS so that nothing worse could happen.", VGA_LIGHT_GREY);
    separator(8);

    /* What happened */
    label(9, "Exception: ");
    if (software) {
        text("KIRILL PANIC (the kernel called kirill_panic)");
    } else if (f->vector < 32) {
        text(exception_names[f->vector]);
    } else {
        text("Unexpected interrupt");
    }
    ink(VGA_LIGHT_GREY);
    text("  vector ");
    dec(f->vector);
    text(" (0x");
    hex(f->vector, 2);
    put(')');

    label(10, "Details  : ");
    if (software) text(panic_message);
    else if (f->vector < 32) text(exception_hint(f->vector));
    else text("Interrupt nobody asked for: stray INT or an unmasked IRQ");

    label(11, "Error    : ");
    print_error_code(f, cr2, software);

    separator(12);

    /* Registers */
    at(13, 1);
    reg("EAX", f->eax, 8); reg("EBX", f->ebx, 8); reg("ECX", f->ecx, 8); reg("EDX", f->edx, 8);
    at(14, 1);
    reg("ESI", f->esi, 8); reg("EDI", f->edi, 8); reg("EBP", f->ebp, 8); reg("ESP", esp, 8);
    at(15, 1);
    reg("EIP", f->eip, 8); reg("EFLAGS", f->eflags, 8);
    print_eflags(f->eflags);
    at(16, 1);
    reg("CS", f->cs & 0xFFFF, 4); reg("DS", f->ds & 0xFFFF, 4); reg("ES", f->es & 0xFFFF, 4);
    reg("FS", f->fs & 0xFFFF, 4); reg("GS", f->gs & 0xFFFF, 4); reg("SS", ss, 4);
    at(17, 1);
    reg("CR0", cr0, 8); reg("CR2", cr2, 8); reg("CR3", cr3, 8); reg("CR4", cr4, 8);

    /* Machine code around EIP, the faulting byte highlighted */
    label(18, "Code ");
    at(18, 7);
    if (f->eip >= 8 && f->eip <= 0xFFFFFFF0u) {
        const uint8_t* code = (const uint8_t*)(uintptr_t)(f->eip - 8);
        for (int i = 0; i < 20; i++) {
            if (i == 8) pattr = (uint8_t)((VGA_BLACK << 4) | VGA_YELLOW);
            else ink(VGA_WHITE);
            hex(code[i], 2);
            ink(VGA_WHITE);
            put(' ');
        }
        ink(VGA_LIGHT_GREY);
        text("<- EIP");
    } else {
        ink(VGA_LIGHT_GREY);
        text("EIP is outside readable memory");
    }

    /* Raw stack: 24 dwords from ESP */
    label(19, "Stack");
    for (int line = 0; line < 3; line++) {
        at(19 + line, 7);
        for (int i = 0; i < 8; i++) {
            uint32_t addr = esp + (uint32_t)(line * 8 + i) * 4;
            if (addr < esp || addr > 0xFFFFFFFCu) break;
            hex(*(const uint32_t*)(uintptr_t)addr, 8);
            put(' ');
        }
    }

    /* Return addresses found on the stack (no frame pointers at -O2, so scan) */
    label(22, "Trace");
    at(22, 7);
    int found = 0;
    for (int i = 0; i < 256 && found < 8; i++) {
        uint32_t addr = esp + (uint32_t)i * 4;
        if (addr < esp || addr > 0xFFFFFFFCu) break;
        uint32_t value = *(const uint32_t*)(uintptr_t)addr;
        if (!looks_like_return(value)) continue;
        ink(value < (uint32_t)(uintptr_t)kernel_text_start ? VGA_LIGHT_CYAN : VGA_WHITE);
        hex(value, 8);
        put(' ');
        found++;
    }
    if (!found) {
        ink(VGA_LIGHT_GREY);
        text("no return addresses found on the stack");
    }

    centered(23, kirill_says(f->vector, f->eip), VGA_YELLOW);

    /* Footer */
    fill_row(24, BAR_ATTR);
    at(24, 1);
    pattr = BAR_ATTR;
    text("Press R to reboot");
    at(24, VGA_WIDTH - 21);
    text("Dedicated to Kirill");

    wait_for_reboot();
}

/* ================================================================ */
/*                      Shell 'panic' command                       */
/* ================================================================ */
static int same(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

void kpanic_test(const char* kind) {
    if (!kind || !*kind) kirill_panic("Panic requested from the shell with the 'panic' command");

    if (same(kind, "div")) {
        __asm__ volatile ("xor %%edx, %%edx\n\t"
                          "mov $1, %%eax\n\t"
                          "xor %%ecx, %%ecx\n\t"
                          "div %%ecx" : : : "eax", "ecx", "edx");
    } else if (same(kind, "ud")) {
        __asm__ volatile ("ud2");
    } else if (same(kind, "gp")) {
        /* Selector 0x1230 is far past the end of the GDT */
        __asm__ volatile ("mov $0x1230, %%ax\n\t"
                          "mov %%ax, %%ds" : : : "eax", "memory");
    } else if (same(kind, "int3")) {
        __asm__ volatile ("int3");
    } else {
        vga_write("Usage: panic [div|ud|gp|int3]\n");
    }
}
