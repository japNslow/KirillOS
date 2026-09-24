#include "kboot.h"

/* ================================================================ */
/*                       Port I/O Helpers                           */
/* ================================================================ */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

/* ================================================================ */
/*                   VGA 80x25 Console Driver                       */
/* ================================================================ */
#define VGA_MEM ((volatile uint16_t*)0xB8000)
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

#define COLOR_BLACK         0x0
#define COLOR_BLUE          0x1
#define COLOR_GREEN         0x2
#define COLOR_CYAN          0x3
#define COLOR_RED           0x4
#define COLOR_MAGENTA       0x5
#define COLOR_BROWN         0x6
#define COLOR_LIGHT_GREY    0x7
#define COLOR_DARK_GREY     0x8
#define COLOR_LIGHT_BLUE    0x9
#define COLOR_LIGHT_GREEN   0xA
#define COLOR_LIGHT_CYAN    0xB
#define COLOR_LIGHT_RED     0xC
#define COLOR_LIGHT_MAGENTA 0xD
#define COLOR_YELLOW        0xE
#define COLOR_WHITE         0xF

static uint8_t  vga_color = (COLOR_BLACK << 4) | COLOR_LIGHT_GREY;
static uint32_t cursor_row = 0;
static uint32_t cursor_col = 0;

static void vga_set_color(uint8_t fg, uint8_t bg) {
    vga_color = (bg << 4) | (fg & 0x0F);
}

static void vga_scroll(void) {
    if (cursor_row < VGA_HEIGHT) return;
    for (uint32_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (uint32_t x = 0; x < VGA_WIDTH; x++) {
            VGA_MEM[y * VGA_WIDTH + x] = VGA_MEM[(y + 1) * VGA_WIDTH + x];
        }
    }
    for (uint32_t x = 0; x < VGA_WIDTH; x++) {
        VGA_MEM[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = ((uint16_t)vga_color << 8) | ' ';
    }
    cursor_row = VGA_HEIGHT - 1;
}

static void vga_clear(void) {
    for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_MEM[i] = ((uint16_t)vga_color << 8) | ' ';
    }
    cursor_row = 0;
    cursor_col = 0;
}

static void vga_putchar(char c) {
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
        vga_scroll();
        return;
    }
    if (c == '\r') {
        cursor_col = 0;
        return;
    }
    if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            VGA_MEM[cursor_row * VGA_WIDTH + cursor_col] = ((uint16_t)vga_color << 8) | ' ';
        } else if (cursor_row > 0) {
            cursor_row--;
            cursor_col = VGA_WIDTH - 1;
            VGA_MEM[cursor_row * VGA_WIDTH + cursor_col] = ((uint16_t)vga_color << 8) | ' ';
        }
        return;
    }

    VGA_MEM[cursor_row * VGA_WIDTH + cursor_col] = ((uint16_t)vga_color << 8) | (uint8_t)c;
    cursor_col++;
    if (cursor_col >= VGA_WIDTH) {
        cursor_col = 0;
        cursor_row++;
        vga_scroll();
    }
}

static void vga_print(const char* s) {
    while (*s) vga_putchar(*s++);
}

static void vga_print_hex(uint32_t val, int digits) {
    static const char hex[] = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; i--) {
        vga_putchar(hex[(val >> (i * 4)) & 0xF]);
    }
}

static void vga_print_num(uint32_t val) {
    char buf[12];
    int len = 0;
    if (val == 0) {
        vga_putchar('0');
        return;
    }
    while (val > 0) {
        buf[len++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (len > 0) {
        vga_putchar(buf[--len]);
    }
}

/* ================================================================ */
/*                       IDT & PIC (IRQ1)                           */
/* ================================================================ */
typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

extern void isr_keyboard_stub(void);
extern void isr_default_stub(void);
extern void jump_to_kernel(uint32_t entry, uint32_t magic, uint32_t mbi);

static idt_entry_t idt[256];
static idt_ptr_t   idtp;

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].offset_low  = (uint16_t)(base & 0xFFFF);
    idt[num].offset_high = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].selector    = sel;
    idt[num].zero        = 0;
    idt[num].type_attr   = flags;
}

static void pic_remap(void) {
    /* Ремап Master PIC на 0x20..0x27, Slave PIC на 0x28..0x2F */
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();

    outb(0x21, 0x20); io_wait();
    outb(0xA1, 0x28); io_wait();

    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();

    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();

    /* Разрешаем только IRQ1 (клавиатура, бит 1 = 0) на Master, маскируем остальные */
    outb(0x21, 0xFD);
    outb(0xA1, 0xFF);
}

static void idt_init(void) {
    idtp.limit = (uint16_t)(sizeof(idt) - 1);
    idtp.base  = (uint32_t)(uintptr_t)&idt;

    for (int i = 0; i < 256; i++) {
        idt_set_gate((uint8_t)i, (uint32_t)(uintptr_t)isr_default_stub, 0x08, 0x8E);
    }

    /* Вектор 0x21: IRQ1 клавиатура */
    idt_set_gate(0x21, (uint32_t)(uintptr_t)isr_keyboard_stub, 0x08, 0x8E);

    __asm__ volatile("lidt %0" : : "m"(idtp));
}

/* ================================================================ */
/*               Keyboard Ring Buffer & IRQ1 Handler                */
/* ================================================================ */
#define KBD_BUF_SIZE 256
static volatile char     kbd_buf[KBD_BUF_SIZE];
static volatile uint32_t kbd_head = 0;
static volatile uint32_t kbd_tail = 0;
static volatile uint32_t kbd_interrupt_count = 0;
static int shift_active = 0;

static const char kbd_us[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/',
    0,  '*', 0,  ' ',
};

static const char kbd_us_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
    0,  '|','Z','X','C','V','B','N','M','<','>','?',
    0,  '*', 0,  ' ',
};

void kboot_keyboard_isr_c(void) {
    uint8_t sc = inb(0x60);
    kbd_interrupt_count++;

    if (sc & 0x80) {
        uint8_t released = sc & 0x7F;
        if (released == 0x2A || released == 0x36) shift_active = 0;
    } else {
        if (sc == 0x2A || sc == 0x36) {
            shift_active = 1;
        } else if (sc < 128) {
            char ch = shift_active ? kbd_us_shift[sc] : kbd_us[sc];
            if (ch) {
                uint32_t next = (kbd_head + 1) % KBD_BUF_SIZE;
                if (next != kbd_tail) {
                    kbd_buf[kbd_head] = ch;
                    kbd_head = next;
                }
            }
        }
    }

    /* Отправка EOI (End of Interrupt) в Master PIC */
    outb(0x20, 0x20);
}

static char kboot_getchar(void) {
    while (kbd_head == kbd_tail) {
        /* Ждём аппаратного прерывания (экономит процессорное время) */
        __asm__ volatile("hlt");
    }
    char c = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return c;
}

/* ================================================================ */
/*                       PC Speaker Sound                           */
/* ================================================================ */
static void kboot_beep(uint32_t freq, uint32_t loops) {
    if (freq < 20 || freq > 20000) return;
    uint32_t div = 1193180 / freq;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(div & 0xFF));
    outb(0x42, (uint8_t)((div >> 8) & 0xFF));

    uint8_t tmp = inb(0x61);
    outb(0x61, tmp | 3);

    for (volatile uint32_t i = 0; i < loops * 10000; i++) {
        __asm__ volatile("nop");
    }

    outb(0x61, inb(0x61) & 0xFC);
}

/* ================================================================ */
/*                     ELF32 Kernel Loader                          */
/* ================================================================ */
#define KERNEL_LOAD_ADDR 0x20000

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) Elf32_Phdr;

static void* k_memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dest;
}

static void* k_memset(void* dest, int val, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    while (n--) *d++ = (uint8_t)val;
    return dest;
}

static void boot_kirillos(void) {
    vga_set_color(COLOR_YELLOW, COLOR_BLACK);
    vga_print("[kboot] Locating KirillOS kernel ELF at 0x20000...\n");

    Elf32_Ehdr* ehdr = (Elf32_Ehdr*)KERNEL_LOAD_ADDR;

    /* Проверка сигнатуры ELF */
    if (ehdr->e_ident[0] != 0x7F ||
        ehdr->e_ident[1] != 'E'  ||
        ehdr->e_ident[2] != 'L'  ||
        ehdr->e_ident[3] != 'F') {
        vga_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
        vga_print("[kboot] ERROR: Invalid ELF magic at 0x20000!\n");
        return;
    }

    vga_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    vga_print("[kboot] Valid ELF32 kernel found.\n");
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_print("[kboot] Kernel entry point: 0x");
    vga_print_hex(ehdr->e_entry, 8);
    vga_print("\n");

    /* Загрузка всех сегментов PT_LOAD */
    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf32_Phdr* ph = (Elf32_Phdr*)((uint8_t*)ehdr + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (ph->p_type == 1) { /* PT_LOAD */
            vga_print("[kboot] Loading segment to 0x");
            vga_print_hex(ph->p_paddr, 8);
            vga_print(" (");
            vga_print_num(ph->p_memsz);
            vga_print(" bytes)...\n");

            k_memcpy((void*)(uintptr_t)ph->p_paddr,
                     (const void*)((uint8_t*)ehdr + ph->p_offset),
                     ph->p_filesz);

            if (ph->p_memsz > ph->p_filesz) {
                k_memset((void*)(uintptr_t)(ph->p_paddr + ph->p_filesz),
                         0,
                         ph->p_memsz - ph->p_filesz);
            }
        }
    }

    /* Подготовка структуры Multiboot Info для передачи ядру */
    static uint32_t mbi[16];
    mbi[0] = 0x01;          /* флаг: mem_lower и mem_upper валидны */
    mbi[1] = 640;           /* lower memory: 640 КБ */
    mbi[2] = 127 * 1024;    /* upper memory: 127 МБ */

    vga_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    vga_print("[kboot] Handing off control to KirillOS...\n\n");

    /* Небольшая задержка перед передачей управления */
    for (volatile int i = 0; i < 2000000; i++) __asm__ volatile("nop");

    /* Передача управления: cli, eax=0x2BADB002, ebx=&mbi, jmp e_entry */
    jump_to_kernel(ehdr->e_entry, 0x2BADB002, (uint32_t)(uintptr_t)mbi);
}

/* ================================================================ */
/*                       Shell & Commands                           */
/* ================================================================ */
static int str_eq(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (*a == *b);
}

static int str_starts_with(const char* str, const char* prefix) {
    while (*prefix) {
        if (*str++ != *prefix++) return 0;
    }
    return 1;
}

static void show_banner(void) {
    vga_set_color(COLOR_CYAN, COLOR_BLACK);
    vga_print("===============================================================================\n");
    vga_set_color(COLOR_WHITE, COLOR_BLACK);
    vga_print("                       KirillOS Bootloader (kboot v1.0)                        \n");
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_print("         32-bit Protected Mode | Hardware Interrupts: IRQ1 (PIC/IDT)           \n");
    vga_set_color(COLOR_CYAN, COLOR_BLACK);
    vga_print("===============================================================================\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_print("Type 'help' for commands. Type 'boot' to start KirillOS.\n\n");
}

static void show_prompt(void) {
    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    vga_print("kboot");
    vga_set_color(COLOR_WHITE, COLOR_BLACK);
    vga_print("> ");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
}

static void cmd_help(void) {
    vga_set_color(COLOR_YELLOW, COLOR_BLACK);
    vga_print("kboot commands:\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_print("  boot    - load and launch KirillOS kernel\n");
    vga_print("  info    - show system hardware & bootloader status\n");
    vga_print("  clear   - clear terminal screen\n");
    vga_print("  beep    - test PC speaker audio tone\n");
    vga_print("  reboot  - reboot computer\n");
    vga_print("  echo X  - print string X\n");
    vga_print("  about   - information about kboot\n");
    vga_print("  help    - show this help message\n");
}

static void cmd_info(void) {
    vga_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    vga_print("[kboot status]\n");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_print("  CPU Architecture  : x86 IA-32 (Protected Mode, 32-bit)\n");
    vga_print("  Address Line A20  : Enabled\n");
    vga_print("  Interrupts        : PIC 8259A (re-mapped to 0x20..0x2F)\n");
    vga_print("  Keyboard Driver   : IRQ1 hardware interrupt via IDT[0x21]\n");
    vga_print("  Interrupts fired  : ");
    vga_print_num(kbd_interrupt_count);
    vga_print("\n");
    vga_print("  Kernel Image      : Cached at 0x20000 (ELF32)\n");
    vga_print("  Target Load Addr  : 0x00100000 (1 MiB)\n");
}

static void cmd_reboot(void) {
    vga_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    vga_print("Rebooting system...\n");
    /* Аппаратный импульс сброса через контроллер 8042 */
    uint8_t temp;
    do {
        temp = inb(0x64);
        if (temp & 1) (void)inb(0x60);
    } while (temp & 2);
    outb(0x64, 0xFE);

    /* Если не сработало — тройное исключение */
    __asm__ volatile("cli; hlt");
}

static void handle_command(char* cmd) {
    if (cmd[0] == 0) return;

    if (str_eq(cmd, "help")) {
        cmd_help();
    } else if (str_eq(cmd, "boot")) {
        boot_kirillos();
    } else if (str_eq(cmd, "info")) {
        cmd_info();
    } else if (str_eq(cmd, "clear")) {
        vga_clear();
        show_banner();
    } else if (str_eq(cmd, "beep")) {
        vga_print("Testing PC speaker beep...\n");
        kboot_beep(880, 20);
    } else if (str_eq(cmd, "reboot")) {
        cmd_reboot();
    } else if (str_eq(cmd, "about")) {
        vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
        vga_print("kboot v1.0 - Native Bootloader for KirillOS\n");
        vga_print("Developed as a standalone replacement for GRUB.\n");
        vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    } else if (str_starts_with(cmd, "echo ")) {
        vga_print(cmd + 5);
        vga_putchar('\n');
    } else {
        vga_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
        vga_print("Unknown command: '");
        vga_print(cmd);
        vga_print("'. Type 'help' for available commands.\n");
        vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    }
}

/* ================================================================ */
/*                     kboot Entry Point                            */
/* ================================================================ */
#define CMD_MAX 128
static char cmd_buf[CMD_MAX];
static int  cmd_len = 0;

void kboot_main(void) {
    /* 1. Инициализация видеорежима */
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_clear();

    /* 2. Инициализация IDT и контроллера прерываний PIC */
    idt_init();
    pic_remap();

    /* 3. Включение прерываний CPU! */
    __asm__ volatile("sti");

    /* 4. Отображение приветственного экрана */
    show_banner();
    show_prompt();

    /* 5. Интерактивный цикл шелла на прерываниях */
    while (1) {
        char c = kboot_getchar();

        if (c == '\n') {
            cmd_buf[cmd_len] = 0;
            vga_putchar('\n');
            handle_command(cmd_buf);
            cmd_len = 0;
            show_prompt();
        } else if (c == '\b') {
            if (cmd_len > 0) {
                cmd_len--;
                vga_putchar('\b');
            }
        } else if (c >= 32 && c < 127) {
            if (cmd_len < CMD_MAX - 1) {
                cmd_buf[cmd_len++] = c;
                vga_putchar(c);
            }
        }
    }
}
