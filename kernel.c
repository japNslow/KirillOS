#include "vga.h"
#include "keyboard.h"
#include "kirillfs.h"
#include "kmemory.h"
#include "sound.h"
#include "ac97.h"
#include "kano.h"
#include "kdhe.h"
#include "kmidi.h"
#include "khex.h"
#include "khexd.h"
#include "kfetch.h"
#include "mode13.h"
#include "mouse.h"
#include "kcommander.h"
#include "kaint.h"


#define INPUT_MAX 128

static char input_buf[INPUT_MAX];
static int  input_len = 0;

/* ================================================================ */
/*                     Port I/O for boot log                        */
/* ================================================================ */
static inline uint8_t port_inb(uint16_t port) {
    uint8_t r;
    __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline uint32_t port_inl(uint16_t port) {
    uint32_t r;
    __asm__ volatile("inl %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void port_outl(uint16_t port, uint32_t v) {
    __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(port));
}

static uint32_t pci_cfg(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    port_outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) |
              ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (reg & 0xFC));
    return port_inl(0xCFC);
}

/* ================================================================ */
/*                        Log helpers                               */
/* ================================================================ */
static void log_tag(const char* t) {
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_putchar('[');
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write(t);
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_putchar(']');
    vga_putchar(' ');
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void log_hex(uint32_t v, int d) {
    vga_write("0x");
    vga_write_hex(v, d);
}

static void log_ok(void) {
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write(" OK");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void log_fail(void) {
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    vga_write(" FAIL");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void print_number(int value) {
    char digits[10];
    int length = 0;
    if (value == 0) {
        vga_putchar('0');
        return;
    }
    while (value > 0) {
        digits[length++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (length > 0) vga_putchar(digits[--length]);
}

/* ================================================================ */
/*                    Verbose boot log                              */
/* ================================================================ */
static void boot_verbose(uint32_t magic, uint32_t mbi) {
    uint32_t eflags;
    __asm__ volatile("pushfl; pop %0" : "=r"(eflags));

    /* ---- cpu ---- */
    log_tag("cpu");
    vga_write("i686 protected mode eflags=");
    log_hex(eflags, 8);
    vga_newline();

    /* ---- multiboot ---- */
    log_tag("mboot");
    vga_write("magic=");
    log_hex(magic, 8);
    vga_write(" mbi=");
    log_hex(mbi, 8);
    if (magic == 0x2BADB002) log_ok(); else log_fail();
    vga_newline();

    /* ---- memory from multiboot ---- */
    if (magic == 0x2BADB002) {
        uint32_t* mb = (uint32_t*)(uintptr_t)mbi;
        if (mb[0] & 1) {
            log_tag("mboot");
            vga_write("lower=");
            print_number((int)mb[1]);
            vga_write("K upper=");
            print_number((int)mb[2]);
            vga_write("K (");
            print_number((int)(mb[2] / 1024));
            vga_write(" MiB)");
            vga_newline();
        }
    }

    /* ---- vga ---- */
    log_tag("vga");
    vga_write("text 80x25 framebuf=");
    log_hex(0xB8000, 8);
    vga_write(" 16-color");
    vga_newline();
}

static void log_pci_bus(void) {
    log_tag("pci");
    vga_write("enumerating bus 0...");
    vga_newline();

    for (uint8_t d = 0; d < 32; d++) {
        for (uint8_t f = 0; f < 8; f++) {
            uint32_t id = pci_cfg(0, d, f, 0);
            if (id == 0xFFFFFFFF || id == 0) continue;

            uint16_t ven = id & 0xFFFF;
            uint16_t did = id >> 16;
            uint16_t cls = (uint16_t)(pci_cfg(0, d, f, 8) >> 16);
            uint8_t bc = (uint8_t)(cls >> 8);
            uint8_t sc = (uint8_t)cls;

            log_tag("pci");
            vga_write(" 00:");
            vga_putchar('0' + d / 10);
            vga_putchar('0' + d % 10);
            vga_putchar('.');
            vga_putchar('0' + f);
            vga_putchar(' ');
            vga_write_hex(ven, 4);
            vga_putchar(':');
            vga_write_hex(did, 4);
            vga_write(" [");
            vga_write_hex(cls, 4);
            vga_write("] ");

            if      (bc==0x06 && sc==0x00) vga_write("host-bridge");
            else if (bc==0x06 && sc==0x01) vga_write("ISA-bridge");
            else if (bc==0x01 && sc==0x01) vga_write("IDE");
            else if (bc==0x06)             vga_write("bridge");
            else if (bc==0x03)             vga_write("VGA-compat");
            else if (bc==0x04 && sc==0x01) {
                vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
                vga_write("audio/AC97");
                vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            }
            else if (bc==0x02) vga_write("ethernet");
            else if (bc==0x0C) vga_write("serial-bus");
            else if (bc==0x01) vga_write("storage");
            else               vga_write("device");
            vga_newline();

            if (f == 0) {
                uint8_t hdr = (uint8_t)(pci_cfg(0, d, f, 0x0C) >> 16);
                if (!(hdr & 0x80)) break;
            }
        }
    }
}

static void log_kbd_init(void) {
    keyboard_init();
    log_tag("8042");
    uint8_t st = port_inb(0x64);
    vga_write("status=");
    log_hex(st, 2);
    vga_write(" OBF=");
    vga_putchar((st & 1) ? '1' : '0');
    vga_write(" IBF=");
    vga_putchar((st & 2) ? '1' : '0');
    vga_write(" set1 US-QWERTY");
    log_ok();
    vga_newline();
}

static void log_ata_kfs(void) {
    kfs_init();

    log_tag("ata");
    vga_write("primary master ");
    log_hex(0x1F0, 4);
    if (kfs_is_persistent()) {
        vga_write(" IDENTIFY drq=1 bsy=0 PIO-28");
        log_ok();
    } else {
        vga_write(" no device detected");
        log_fail();
    }
    vga_newline();

    log_tag("kfs");
    if (kfs_is_persistent()) {
        vga_write("sector 321 magic=");
        log_hex(0x4B465333, 8);
        vga_write(" ");
        print_number(kfs_file_count());
        vga_write("/16 slots persistent");
        log_ok();
    } else {
        vga_write("no disk, ");
        print_number(kfs_file_count());
        vga_write("/16 slots RAM-fallback");
    }
    vga_newline();
}

static void log_heap_init(void) {
    kmemory_init();
    log_tag("heap");
    print_number((int)kmemory_capacity());
    vga_write(" bytes pool ");
    print_number((int)kmemory_used());
    vga_write(" used");
    log_ok();
    vga_newline();
}

static void log_sound_init(void) {
    sound_init();

    log_tag("pit");
    vga_write("ch0 div=");
    log_hex(0x04A9, 4);
    vga_write(" rate=~1000Hz ch2 spkr-gate");
    log_ok();
    vga_newline();

    log_tag("ac97");
    if (ac97_ready()) {
        vga_write("mixer=");
        log_hex(ac97_get_mixer_base(), 4);
        vga_write(" bm=");
        log_hex(ac97_get_bm_base(), 4);
        vga_write(" 48kHz VRA=1");
        log_ok();
    } else {
        vga_write("no codec, PC-speaker fallback");
    }
    vga_newline();
}

/* ================================================================ */
/*                     String helpers                               */
/* ================================================================ */
static int strcmp_(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int command_is(const char* command, const char* name) {
    while (*name && *command == *name) {
        command++;
        name++;
    }
    return *name == 0 && (*command == 0 || *command == ' ');
}

static void fs_error(kfs_result_t result) {
    if (result == KFS_OK) return;
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    if (result == KFS_NOT_FOUND) vga_write("File not found\n");
    else if (result == KFS_EXISTS) vga_write("File already exists\n");
    else if (result == KFS_FULL) vga_write("KirillFS: no free file slots\n");
    else if (result == KFS_INVALID_NAME) vga_write("Invalid file name\n");
    else if (result == KFS_TOO_LARGE) vga_write("File is too large (max 511 bytes)\n");
    else vga_write("KirillFS error\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static char* next_word(char* text) {
    while (*text == ' ') text++;
    if (!*text) return 0;
    return text;
}

static uint32_t parse_number(const char* text) {
    uint32_t value = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (uint32_t)(*text - '0');
        text++;
    }
    return value;
}

/* ================================================================ */
/*                     Shell commands                               */
/* ================================================================ */
static void handle_fs_command(char* cmd) {
    char* argument = cmd;
    while (*argument && *argument != ' ') argument++;
    if (*argument) *argument++ = 0;
    argument = next_word(argument);

    if (strcmp_(cmd, "ls") == 0) {
        int count = kfs_file_count();
        if (!count) {
            vga_write("KirillFS is empty\n");
            return;
        }
        for (int i = 0; i < count; i++) {
            vga_write(kfs_name(i));
            vga_write("  ");
            vga_write("bytes: ");
            print_number(kfs_size(i));
            vga_newline();
        }
    } else if (strcmp_(cmd, "touch") == 0) {
        if (!argument) { vga_write("Usage: touch NAME\n"); return; }
        fs_error(kfs_touch(argument));
    } else if (strcmp_(cmd, "cat") == 0) {
        const char* data;
        int size;
        if (!argument) { vga_write("Usage: cat NAME\n"); return; }
        if (kfs_read(argument, &data, &size) != KFS_OK) {
            fs_error(KFS_NOT_FOUND);
            return;
        }
        vga_write(data);
        if (size) vga_newline();
    } else if (strcmp_(cmd, "rm") == 0) {
        if (!argument) { vga_write("Usage: rm NAME\n"); return; }
        fs_error(kfs_remove(argument));
    } else if (strcmp_(cmd, "format") == 0) {
        kfs_format();
        vga_write("KirillFS formatted\n");
    } else if (strcmp_(cmd, "fsinfo") == 0) {
        vga_write("KirillFS: ");
        vga_write("32 files, 511 bytes per file, ");
        vga_write(kfs_is_persistent() ? "persistent ATA disk\n" : "in-memory fallback\n");
    } else {
        vga_write("KirillFS commands: ls, touch, write, cat, rm, format, fsinfo\n");
    }
}

static void print_prompt(void) {
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write("KirillOS");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_write("> ");
}

static void handle_command(char* cmd) {
    if (cmd[0] == 0) return;

    if (strcmp_(cmd, "help") == 0) {
        vga_write("Commands:\n");
        vga_write("  help         - this help\n");
        vga_write("  about        - about KirillOS\n");
        vga_write("  clear        - clear screen\n");
        vga_write("  echo X       - print X\n");
        vga_write("  ls           - list KirillFS files\n");
        vga_write("  touch X      - create an empty file\n");
        vga_write("  write X TEXT - write file contents\n");
        vga_write("  cat X        - show file contents\n");
        vga_write("  rm X         - delete a file\n");
        vga_write("  format       - clear KirillFS\n");
        vga_write("  fsinfo       - show filesystem info\n");
        vga_write("  run X        - execute .bin program (e.g. 'run snake.bin')\n");
        vga_write("  khex list    - list runnable .bin binaries\n");
        vga_write("  kdhe X       - hex editor (or 'kdhe disk LBA')\n");
        vga_write("  kmidi X      - visual MIDI tracker & composer\n");
        vga_write("  play X       - play .kmidi music file\n");
        vga_write("  kano X       - text editor (nano clone)\n");
        vga_write("  beep         - play an 8-bit beep\n");
        vga_write("  tone F M     - play frequency F for M ms\n");
        vga_write("  music        - play KirillOS chiptune demo\n");
        vga_write("  kfetch       - display system info & VGA color palette\n");
        vga_write("  date / time  - display CMOS RTC real-time clock\n");
        vga_write("  mode13       - launch 320x200 256-color VGA Mode 13h demo\n");
        vga_write("  kcommander   - graphical Norton/Total Commander in Mode 13h (alias: kc)\n");
        vga_write("  kaint [file] - graphical paint program with mouse & BMP export\n");
    }
    else if (strcmp_(cmd, "about") == 0) {
        vga_write("KirillOS v0.2 - minimal x86 OS\n");
        vga_write("kboot + KHEX Runner + Kdhe Hex Editor + Kmidi Tracker + Mode 13h + KFS + Kaint\n");
    }
    else if (strcmp_(cmd, "kfetch") == 0 || strcmp_(cmd, "fetch") == 0) {
        kfetch_print();
    }
    else if (strcmp_(cmd, "date") == 0) {
        kfetch_date();
    }
    else if (strcmp_(cmd, "time") == 0) {
        kfetch_time();
    }
    else if (strcmp_(cmd, "mode13") == 0 || strcmp_(cmd, "demo13") == 0) {
        mode13_demo();
    }
    else if (strcmp_(cmd, "kcommander") == 0 || strcmp_(cmd, "kc") == 0 || strcmp_(cmd, "commander") == 0) {
        kcommander_start();
    }
    else if (strcmp_(cmd, "kaint") == 0 || strcmp_(cmd, "paint") == 0) {
        kaint_start(0);
    }
    else if (command_is(cmd, "kaint") || command_is(cmd, "paint")) {
        char* file = next_word(cmd + (command_is(cmd, "kaint") ? 5 : 5));
        kaint_start(file);
    }
    else if (strcmp_(cmd, "clear") == 0) {
        vga_clear();
    }
    else if (command_is(cmd, "run")) {

        char* name = next_word(cmd + 3);
        if (!name) vga_write("Usage: run PROGRAM.BIN\n");
        else khex_run_file(name);
    }
    else if (command_is(cmd, "khex")) {
        char* sub = next_word(cmd + 4);
        if (!sub) {
            vga_write("Usage: khex run FILE.BIN | khex list | khex info FILE.BIN\n");
        } else if (command_is(sub, "run")) {
            char* file = next_word(sub + 3);
            if (!file) vga_write("Usage: khex run FILE.BIN\n");
            else khex_run_file(file);
        } else if (command_is(sub, "list")) {
            khex_list();
        } else if (command_is(sub, "info")) {
            char* file = next_word(sub + 4);
            if (!file) vga_write("Usage: khex info FILE.BIN\n");
            else khex_info(file);
        } else {
            khex_run_file(sub);
        }
    }
    else if (command_is(cmd, "khexd")) {
        char* sub = next_word(cmd + 5);
        if (!sub) {
            vga_write("KHEXD Compiler & Linker:\n");
            vga_write("  khexd build FILE.k [FILE.khex] - compile & link .k to .khex\n");
            vga_write("  khexd compile FILE.k [FILE.ko] - compile .k to .ko\n");
            vga_write("  khexd link FILE.ko [FILE.khex] - link .ko to .khex\n");
        } else if (command_is(sub, "compile")) {
            char* args = next_word(sub + 7);
            if (!args) {
                vga_write("Usage: khexd compile SOURCE.k [OUTPUT.ko]\n");
            } else {
                char* out = args;
                while (*out && *out != ' ') out++;
                if (*out) {
                    *out++ = 0;
                    out = next_word(out);
                } else {
                    out = 0;
                }
                char default_obj[32];
                if (!out) {
                    int i = 0;
                    while (args[i] && args[i] != '.' && i < 20) { default_obj[i] = args[i]; i++; }
                    default_obj[i++] = '.'; default_obj[i++] = 'k'; default_obj[i++] = 'o'; default_obj[i] = 0;
                    out = default_obj;
                }
                khexd_compile(args, out);
            }
        } else if (command_is(sub, "link")) {
            char* args = next_word(sub + 4);
            if (!args) {
                vga_write("Usage: khexd link INPUT.ko [OUTPUT.khex]\n");
            } else {
                char* out = args;
                while (*out && *out != ' ') out++;
                if (*out) {
                    *out++ = 0;
                    out = next_word(out);
                } else {
                    out = 0;
                }
                char default_khex[32];
                if (!out) {
                    int i = 0;
                    while (args[i] && args[i] != '.' && i < 18) { default_khex[i] = args[i]; i++; }
                    default_khex[i++] = '.'; default_khex[i++] = 'k'; default_khex[i++] = 'h'; default_khex[i++] = 'e'; default_khex[i++] = 'x'; default_khex[i] = 0;
                    out = default_khex;
                }
                khexd_link(args, out);
            }
        } else if (command_is(sub, "build")) {
            char* args = next_word(sub + 5);
            if (!args) {
                vga_write("Usage: khexd build SOURCE.k [OUTPUT.khex]\n");
            } else {
                char* out = args;
                while (*out && *out != ' ') out++;
                if (*out) {
                    *out++ = 0;
                    out = next_word(out);
                } else {
                    out = 0;
                }
                char default_khex[32];
                if (!out) {
                    int i = 0;
                    while (args[i] && args[i] != '.' && i < 18) { default_khex[i] = args[i]; i++; }
                    default_khex[i++] = '.'; default_khex[i++] = 'k'; default_khex[i++] = 'h'; default_khex[i++] = 'e'; default_khex[i++] = 'x'; default_khex[i] = 0;
                    out = default_khex;
                }
                khexd_build(args, out);
            }
        }
    }
    else if (command_is(cmd, "kano")) {
        char* name = next_word(cmd + 4);
        if (!name) vga_write("Usage: kano NAME\n");
        else kano_open(name);
    }
    else if (command_is(cmd, "kdhe")) {
        char* name = next_word(cmd + 4);
        if (!name) {
            vga_write("Usage: kdhe FILE  or  kdhe disk LBA\n");
        } else if (name[0] == 'd' && name[1] == 'i' && name[2] == 's' && name[3] == 'k' && name[4] == ' ') {
            char* lba_str = next_word(name + 5);
            if (!lba_str) vga_write("Usage: kdhe disk LBA\n");
            else kdhe_open_disk(parse_number(lba_str));
        } else {
            kdhe_open(name);
        }
    }
    else if (command_is(cmd, "kmidi") || command_is(cmd, "kidi")) {
        char* name = next_word(cmd + 5);
        if (!name && command_is(cmd, "kidi")) name = next_word(cmd + 4);
        if (!name) vga_write("Usage: kmidi SONG.KMIDI\n");
        else kmidi_open(name);
    }
    else if (command_is(cmd, "play")) {
        char* name = next_word(cmd + 4);
        if (!name) vga_write("Usage: play SONG.KMIDI\n");
        else kmidi_play_file(name);
    }
    else if (strcmp_(cmd, "beep") == 0) {
        sound_soft_note(880, 180);
    }
    else if (strcmp_(cmd, "music") == 0) {
        vga_write("Playing KirillOS 8-bit MIDI demo...\n");
        sound_play_demo();
    }
    else if (command_is(cmd, "tone")) {
        char* args = next_word(cmd + 4);
        char* duration;
        uint32_t frequency;
        if (!args) {
            vga_write("Usage: tone FREQUENCY MILLISECONDS\n");
        } else {
            duration = args;
            while (*duration && *duration != ' ') duration++;
            if (!*duration) {
                vga_write("Usage: tone FREQUENCY MILLISECONDS\n");
            } else {
                *duration++ = 0;
                duration = next_word(duration);
                frequency = parse_number(args);
                if (!duration || !frequency) {
                    vga_write("Usage: tone FREQUENCY MILLISECONDS\n");
                } else {
                    sound_soft_note(frequency, parse_number(duration));
                }
            }
        }
    }
    else if (command_is(cmd, "ls") || command_is(cmd, "touch") ||
             command_is(cmd, "cat") || command_is(cmd, "rm") ||
             command_is(cmd, "format") || command_is(cmd, "fsinfo")) {
        handle_fs_command(cmd);
    }
    else if (cmd[0]=='w'&&cmd[1]=='r'&&cmd[2]=='i'&&cmd[3]=='t'&&cmd[4]=='e'&&cmd[5]==' ') {
        char* name = cmd + 6;
        char* data = name;
        while (*data && *data != ' ') data++;
        if (!*data) {
            vga_write("Usage: write NAME TEXT\n");
        } else {
            *data++ = 0;
            data = next_word(data);
            if (!data) vga_write("Usage: write NAME TEXT\n");
            else fs_error(kfs_write(name, data));
        }
    }
    else if (cmd[0]=='e'&&cmd[1]=='c'&&cmd[2]=='h'&&cmd[3]=='o'&&cmd[4]==' ') {
        vga_write(cmd + 5);
        vga_newline();
    }
    else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_write("Unknown command: ");
        vga_write(cmd);
        vga_newline();
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

/* ================================================================ */
/*                      Kernel entry point                          */
/* ================================================================ */
void kernel_main(uint32_t magic, uint32_t mbi) {
    vga_init();

    /* Sync VGA cursor with boot.asm position so log continues seamlessly */
    {
        extern uint32_t boot_row, boot_col;
        vga_set_cursor(boot_row, boot_col);
    }

    /* ---- Verbose hardware log ---- */
    boot_verbose(magic, mbi);
    log_kbd_init();
    log_pci_bus();
    log_ata_kfs();
    log_heap_init();
    log_sound_init();

    /* ---- All systems go ---- */
    log_tag("init");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write("KirillOS v0.2 all subsystems online");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_newline();
    vga_newline();

    /* Preload default .kmidi songs (Tetris, Mario, Chiptune) if not present */
    kmidi_init_default_songs();

    /* Initialize KHEX binary runner and preload games (snake, guess, matrix) */
    khex_init();
    khex_init_default_apps();

    /* Preload default demo.bmp for Kaint and Mode 13h */
    kaint_init_default_bmp();

    /* ---- Banner ---- */
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write("  _  ___          _ _ _  ___  ____  \n");
    vga_write(" | |/ (_)_ _ _ _ (_) | |/ _ \\/ ___| \n");
    vga_write(" | ' <| | '_| '_|| | | | (_) \\___ \\ \n");
    vga_write(" |_|\\_\\_|_| |_|  |_|_|_|\\___/|____/ v0.2\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_write("\nType 'help' for commands. Try 'play tetris.kmidi' or 'kdhe'!\n\n");

    print_prompt();

    while (1) {
        char c = keyboard_getchar();
        if (c == '\n') {
            input_buf[input_len] = 0;
            vga_newline();
            handle_command(input_buf);
            input_len = 0;
            print_prompt();
        }
        else if (c == '\b') {
            if (input_len > 0) {
                input_len--;
                vga_backspace();
            }
        }
        else if (c >= 32 && c < 127) {
            if (input_len < INPUT_MAX - 1) {
                input_buf[input_len++] = c;
                vga_putchar(c);
            }
        }
    }
}