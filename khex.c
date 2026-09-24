#include "khex.h"
#include "khexd.h"
#include "vga.h"
#include "keyboard.h"
#include "kirillfs.h"
#include "sound.h"
#include "apps/guess_data.h"
#include "apps/matrix_data.h"
#include "apps/snake_data.h"

/* Fast pseudo-random number generator (Xorshift32) */
static uint32_t khex_seed = 0x1337BEEF;

static uint32_t khex_rand_fn(void) {
    khex_seed ^= khex_seed << 13;
    khex_seed ^= khex_seed >> 17;
    khex_seed ^= khex_seed << 5;
    return khex_seed;
}

static void khex_draw_char_fn(uint32_t row, uint32_t col, char ch, uint8_t color) {
    if (row < VGA_HEIGHT && col < VGA_WIDTH) {
        VGA_MEMORY[row * VGA_WIDTH + col] = ((uint16_t)color << 8) | (uint8_t)ch;
    }
}

static void khex_delay_fn(uint32_t ms) {
    sound_note(0, ms);
}

static void khex_beep_fn(uint32_t freq, uint32_t ms) {
    sound_soft_note(freq, ms);
}

static int khex_fs_read_fn(const char* name, const char** data, int* size) {
    return (int)kfs_read(name, data, size);
}

static int khex_fs_write_fn(const char* name, const char* data) {
    return (int)kfs_write(name, data);
}

/* Global API structure passed to binary executables */
static const khex_api_t khex_system_api = {
    .version    = 0x0002,
    .print      = vga_write,
    .putchar    = vga_putchar,
    .clear      = vga_clear,
    .set_color  = vga_set_color,
    .set_cursor = vga_set_cursor,
    .draw_char  = khex_draw_char_fn,
    .getchar    = keyboard_getchar,
    .has_char   = keyboard_has_char,
    .poll_char  = keyboard_poll,
    .delay      = khex_delay_fn,
    .beep       = khex_beep_fn,
    .rand       = khex_rand_fn,
    .fs_read    = khex_fs_read_fn,
    .fs_write   = khex_fs_write_fn
};

void khex_init(void) {
    /* Initialize random seed using PIT timer counter */
    khex_seed ^= 0xA5A55A5A;
}

typedef int (*khex_entry_f)(const khex_api_t* api);

int khex_run_buffer(const void* code, int size) {
    if (size <= 0) return -1;

    uint8_t* target = (uint8_t*)KHEX_LOAD_ADDR;
    const uint8_t* src = (const uint8_t*)code;

    for (int i = 0; i < size; i++) {
        target[i] = src[i];
    }

    khex_entry_f entry = (khex_entry_f)(uintptr_t)KHEX_LOAD_ADDR;
    int exit_code = entry(&khex_system_api);

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    return exit_code;
}

int khex_run_file(const char* filename) {
    const char* data;
    int size = 0;

    if (kfs_read(filename, &data, &size) != KFS_OK) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_write("khex: executable '");
        vga_write(filename);
        vga_write("' not found\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return -1;
    }

    if (size <= 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_write("khex: file '");
        vga_write(filename);
        vga_write("' is empty (0 bytes)\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return -1;
    }

    /* Check if file is a .khex bytecode binary */
    if (size >= 4) {
        uint32_t mg = (uint8_t)data[0] | ((uint8_t)data[1] << 8) |
                      ((uint8_t)data[2] << 16) | ((uint8_t)data[3] << 24);
        if (mg == KHEX_MAGIC) {
            vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
            vga_write("[KHEX] Executing KHEX binary '");
            vga_write(filename);
            vga_write("'...\n\n");
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            return khexd_run_khex(filename);
        }
    }

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write("[KHEX] Executing '");
    vga_write(filename);
    vga_write("' (");
    
    char sbuf[10]; int sl = 0; int tmp = size;
    while (tmp > 0) { sbuf[sl++] = (char)('0' + (tmp % 10)); tmp /= 10; }
    while (sl > 0) vga_putchar(sbuf[--sl]);
    
    vga_write(" bytes) at 0x00050000...\n\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    int exit_code = khex_run_buffer(data, size);

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write("\n[KHEX] Process terminated with return code: ");
    if (exit_code == 0) {
        vga_putchar('0');
    } else {
        int ec = exit_code;
        if (ec < 0) { vga_putchar('-'); ec = -ec; }
        char ebuf[10]; int el = 0;
        while (ec > 0) { ebuf[el++] = (char)('0' + (ec % 10)); ec /= 10; }
        while (el > 0) vga_putchar(ebuf[--el]);
    }
    vga_newline();
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    return exit_code;
}

void khex_info(const char* filename) {
    const char* data;
    int size = 0;

    if (kfs_read(filename, &data, &size) != KFS_OK) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_write("khex: file not found: ");
        vga_write(filename);
        vga_newline();
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write("[KHEX Binary Info]\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_write("  Filename : "); vga_write(filename); vga_newline();
    vga_write("  Size     : ");
    
    char sbuf[10]; int sl = 0; int tmp = size;
    if (tmp == 0) vga_putchar('0');
    while (tmp > 0) { sbuf[sl++] = (char)('0' + (tmp % 10)); tmp /= 10; }
    while (sl > 0) vga_putchar(sbuf[--sl]);
    vga_write(" bytes\n");

    vga_write("  Load Addr: 0x00050000\n");
    vga_write("  First 16 bytes: ");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    int show = size > 16 ? 16 : size;
    for (int i = 0; i < show; i++) {
        vga_write_hex((uint8_t)data[i], 2);
        vga_putchar(' ');
    }
    vga_newline();
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

void khex_list(void) {
    int total = kfs_file_count();
    int count = 0;

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write("Runnable KHEX Binaries:\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    for (int i = 0; i < total; i++) {
        const char* name = kfs_name(i);
        if (!name) continue;

        /* Check if ends with .bin or .khex */
        int len = 0; while (name[len]) len++;
        int is_bin = (len >= 4 && name[len - 4] == '.' && name[len - 3] == 'b' &&
                      name[len - 2] == 'i' && name[len - 1] == 'n');
        int is_khex = (len >= 5 && name[len - 5] == '.' && name[len - 4] == 'k' &&
                       name[len - 3] == 'h' && name[len - 2] == 'e' && name[len - 1] == 'x');

        if (is_bin || is_khex) {
            vga_set_color(is_khex ? VGA_LIGHT_GREEN : VGA_LIGHT_CYAN, VGA_BLACK);
            vga_write("  * ");
            vga_write(name);
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            vga_write(is_khex ? " [KHEX] (" : " [NATIVE] (");
            
            char sbuf[10]; int sl = 0; int tmp = kfs_size(i);
            if (tmp == 0) vga_putchar('0');
            while (tmp > 0) { sbuf[sl++] = (char)('0' + (tmp % 10)); tmp /= 10; }
            while (sl > 0) vga_putchar(sbuf[--sl]);
            vga_write(" B)\n");
            count++;
        }
    }

    if (count == 0) {
        vga_write("  (no executable files found in KirillFS)\n");
    }
}

void khex_init_default_apps(void) {
    /* 1. guess.bin */
    if (kfs_touch("guess.bin") == KFS_OK) {
        kfs_write_binary("guess.bin", GUESS_BIN, sizeof(GUESS_BIN));
    }

    /* 2. matrix.bin */
    if (kfs_touch("matrix.bin") == KFS_OK) {
        kfs_write_binary("matrix.bin", MATRIX_BIN, sizeof(MATRIX_BIN));
    }

    /* 3. snake.bin */
    if (kfs_touch("snake.bin") == KFS_OK) {
        kfs_write_binary("snake.bin", SNAKE_BIN, sizeof(SNAKE_BIN));
    }

    /* 4. Preload .k samples and build .khex executables */
    khexd_init_samples();
}
