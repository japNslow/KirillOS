#include "kdhe.h"
#include "keyboard.h"
#include "kirillfs.h"
#include "ata.h"
#include "vga.h"

#define KDHE_BUF_MAX 2048
#define ROWS_ON_SCREEN 16

static uint8_t buffer[KDHE_BUF_MAX];
static int     buf_size = 0;
static int     cursor = 0;          /* 0 .. buf_size (or buf_size - 1) */
static int     nibble = 0;          /* 0 = high nibble, 1 = low nibble */
static int     scroll_row = 0;      /* first row shown on screen */
static int     ascii_mode = 0;      /* 0 = HEX mode, 1 = ASCII mode */
static int     dirty = 0;
static int     is_disk_mode = 0;
static uint32_t disk_lba = 0;
static char    file_title[32];

static void print_hex2(uint8_t val) {
    vga_write_hex(val, 2);
}

static void print_hex4(uint16_t val) {
    vga_write_hex(val, 4);
}

static void print_dec(int val) {
    char s[10];
    int len = 0;
    if (val == 0) { vga_putchar('0'); return; }
    if (val < 0) { vga_putchar('-'); val = -val; }
    while (val > 0) {
        s[len++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (len > 0) vga_putchar(s[--len]);
}

static void print_bin8(uint8_t val) {
    for (int i = 7; i >= 0; i--) {
        vga_putchar((val & (1 << i)) ? '1' : '0');
    }
}

static void redraw(void) {
    vga_clear();

    /* Header Bar */
    vga_set_color(VGA_BLACK, VGA_LIGHT_CYAN);
    vga_write(" KDHE v0.2 - Kirill Data Hex Editor ");
    vga_set_color(VGA_BLACK, VGA_CYAN);
    vga_write(" | ");
    vga_write(file_title);
    vga_write(" | Size: ");
    print_dec(buf_size);
    vga_write(" B ");
    if (dirty) {
        vga_set_color(VGA_WHITE, VGA_LIGHT_RED);
        vga_write(" [MODIFIED] ");
    }
    for (int i = 0; i < 20; i++) vga_putchar(' ');
    vga_newline();

    /* Column Headers */
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_write(" Offset   00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F   0123456789ABCDEF\n");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write(" -------  -------------------------------------------------   ----------------\n");

    /* Calculate row of cursor */
    int cursor_row = cursor / 16;
    if (cursor_row < scroll_row) scroll_row = cursor_row;
    if (cursor_row >= scroll_row + ROWS_ON_SCREEN) scroll_row = cursor_row - ROWS_ON_SCREEN + 1;

    int total_rows = (buf_size + 15) / 16;
    if (total_rows == 0) total_rows = 1;

    for (int r = 0; r < ROWS_ON_SCREEN; r++) {
        int current_row = scroll_row + r;
        if (current_row >= total_rows && current_row > 0) {
            vga_set_color(VGA_DARK_GREY, VGA_BLACK);
            vga_write(" ~\n");
            continue;
        }

        int row_offset = current_row * 16;

        /* Print offset */
        vga_set_color(VGA_CYAN, VGA_BLACK);
        print_hex4((uint16_t)row_offset);
        vga_write(":  ");

        /* Print 16 hex bytes */
        for (int b = 0; b < 16; b++) {
            int pos = row_offset + b;
            if (b == 8) vga_putchar(' ');

            if (pos < buf_size) {
                if (pos == cursor) {
                    if (!ascii_mode) {
                        vga_set_color(VGA_BLACK, VGA_LIGHT_CYAN);
                    } else {
                        vga_set_color(VGA_BLACK, VGA_DARK_GREY);
                    }
                } else {
                    vga_set_color(buffer[pos] == 0 ? VGA_DARK_GREY : VGA_WHITE, VGA_BLACK);
                }
                print_hex2(buffer[pos]);
                vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
                vga_putchar(' ');
            } else if (pos == buf_size && cursor == buf_size) {
                /* Cursor at end-of-file append position */
                vga_set_color(VGA_BLACK, VGA_LIGHT_GREEN);
                vga_write("..");
                vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
                vga_putchar(' ');
            } else {
                vga_set_color(VGA_DARK_GREY, VGA_BLACK);
                vga_write(".. ");
            }
        }

        vga_write("  ");

        /* Print ASCII representation */
        for (int b = 0; b < 16; b++) {
            int pos = row_offset + b;
            if (pos < buf_size) {
                uint8_t val = buffer[pos];
                char ch = (val >= 32 && val <= 126) ? (char)val : '.';
                if (pos == cursor) {
                    if (ascii_mode) {
                        vga_set_color(VGA_BLACK, VGA_LIGHT_GREEN);
                    } else {
                        vga_set_color(VGA_BLACK, VGA_DARK_GREY);
                    }
                } else {
                    vga_set_color(val >= 32 && val <= 126 ? VGA_LIGHT_GREEN : VGA_DARK_GREY, VGA_BLACK);
                }
                vga_putchar(ch);
            } else {
                vga_set_color(VGA_DARK_GREY, VGA_BLACK);
                vga_putchar(' ');
            }
        }
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        vga_newline();
    }

    /* Inspector / Details Bar */
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write(" ------------------------------------------------------------------------------\n");

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write(" Pos: 0x");
    print_hex4((uint16_t)cursor);
    vga_write(" (");
    print_dec(cursor);
    vga_write(") | ");

    if (cursor < buf_size) {
        uint8_t val = buffer[cursor];
        vga_write("Hex: 0x");
        print_hex2(val);
        vga_write(" | Dec: ");
        print_dec((int)val);
        vga_write(" | Bin: ");
        print_bin8(val);
        vga_write(" | ASCII: '");
        vga_putchar((val >= 32 && val <= 126) ? (char)val : '.');
        vga_write("'");
    } else {
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        vga_write("[EOF Insertion Point]");
    }
    vga_newline();

    /* Status & Controls Bar */
    vga_set_color(VGA_BLACK, VGA_LIGHT_GREY);
    vga_write(" [Tab] Mode: ");
    if (ascii_mode) {
        vga_set_color(VGA_WHITE, VGA_BLUE);
        vga_write(" ASCII ");
    } else {
        vga_set_color(VGA_WHITE, VGA_MAGENTA);
        vga_write("  HEX  ");
    }
    vga_set_color(VGA_BLACK, VGA_LIGHT_GREY);
    vga_write(" | Arrows: Move | [Ins] Insert | [Del] Del | [^S] Save | [^Q]/[ESC] Quit ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void save_kdhe(void) {
    if (is_disk_mode) {
        if (ata_write28(disk_lba, buffer)) {
            dirty = 0;
            redraw();
        } else {
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_write("\nDisk write failed!\n");
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        }
        return;
    }

    if (kfs_write_binary(file_title, buffer, buf_size) == KFS_OK) {
        dirty = 0;
        redraw();
    } else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_write("\nSave failed: KirillFS error or file too large (max 511 bytes)\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

static void insert_byte(uint8_t val) {
    if (buf_size >= KDHE_BUF_MAX - 1) return;
    for (int i = buf_size; i > cursor; i--) {
        buffer[i] = buffer[i - 1];
    }
    buffer[cursor] = val;
    buf_size++;
    dirty = 1;
}

static void delete_byte(void) {
    if (buf_size <= 0 || cursor >= buf_size) return;
    for (int i = cursor; i < buf_size - 1; i++) {
        buffer[i] = buffer[i + 1];
    }
    buf_size--;
    dirty = 1;
}

static int hex_char_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void kdhe_loop(void) {
    redraw();

    for (;;) {
        char c = keyboard_getchar();

        if (c == KEY_QUIT || c == KEY_ESC) {
            if (dirty) {
                vga_set_color(VGA_WHITE, VGA_LIGHT_RED);
                vga_write("\nUnsaved changes! Press Ctrl+S to save, or Ctrl+Q again to discard.\n");
                vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
                char confirm = keyboard_getchar();
                if (confirm == KEY_QUIT || confirm == KEY_ESC) {
                    vga_clear();
                    return;
                }
                if (confirm == KEY_SAVE) save_kdhe();
                redraw();
            } else {
                vga_clear();
                return;
            }
        }
        else if (c == KEY_SAVE) {
            save_kdhe();
        }
        else if (c == KEY_TAB) {
            ascii_mode = !ascii_mode;
            nibble = 0;
            redraw();
        }
        else if (c == KEY_LEFT) {
            if (cursor > 0) cursor--;
            nibble = 0;
            redraw();
        }
        else if (c == KEY_RIGHT) {
            if (cursor < buf_size) cursor++;
            nibble = 0;
            redraw();
        }
        else if (c == KEY_UP) {
            if (cursor >= 16) cursor -= 16;
            nibble = 0;
            redraw();
        }
        else if (c == KEY_DOWN) {
            if (cursor + 16 <= buf_size) cursor += 16;
            else cursor = buf_size;
            nibble = 0;
            redraw();
        }
        else if (c == KEY_HOME) {
            cursor = 0;
            nibble = 0;
            redraw();
        }
        else if (c == KEY_END) {
            cursor = buf_size > 0 ? buf_size - 1 : 0;
            nibble = 0;
            redraw();
        }
        else if (c == KEY_PGUP) {
            if (cursor >= 16 * ROWS_ON_SCREEN) cursor -= 16 * ROWS_ON_SCREEN;
            else cursor = 0;
            nibble = 0;
            redraw();
        }
        else if (c == KEY_PGDN) {
            cursor += 16 * ROWS_ON_SCREEN;
            if (cursor > buf_size) cursor = buf_size;
            nibble = 0;
            redraw();
        }
        else if (c == KEY_INS) {
            insert_byte(0x00);
            redraw();
        }
        else if (c == KEY_DEL) {
            delete_byte();
            redraw();
        }
        else if (c == KEY_BACKSPACE) {
            if (cursor > 0) {
                cursor--;
                delete_byte();
                redraw();
            }
        }
        else if (!ascii_mode) {
            /* HEX Mode editing */
            int h = hex_char_val(c);
            if (h >= 0) {
                if (cursor == buf_size) {
                    insert_byte(0x00);
                }
                if (cursor < buf_size) {
                    if (nibble == 0) {
                        buffer[cursor] = (uint8_t)((buffer[cursor] & 0x0F) | (h << 4));
                        nibble = 1;
                        dirty = 1;
                    } else {
                        buffer[cursor] = (uint8_t)((buffer[cursor] & 0xF0) | h);
                        nibble = 0;
                        dirty = 1;
                        if (cursor < buf_size) cursor++;
                    }
                    redraw();
                }
            }
        }
        else {
            /* ASCII Mode editing */
            if (c >= 32 && c <= 126) {
                if (cursor == buf_size) {
                    insert_byte((uint8_t)c);
                    cursor++;
                } else {
                    buffer[cursor++] = (uint8_t)c;
                    dirty = 1;
                }
                redraw();
            }
        }
    }
}

void kdhe_open(const char* filename) {
    const char* data;
    int size = 0;
    is_disk_mode = 0;

    int idx = 0;
    while (filename[idx] && idx < 30) {
        file_title[idx] = filename[idx];
        idx++;
    }
    file_title[idx] = 0;

    cursor = 0;
    nibble = 0;
    scroll_row = 0;
    ascii_mode = 0;
    dirty = 0;
    buf_size = 0;

    if (kfs_read(filename, &data, &size) == KFS_OK) {
        if (size > KDHE_BUF_MAX) size = KDHE_BUF_MAX;
        for (int i = 0; i < size; i++) buffer[i] = (uint8_t)data[i];
        buf_size = size;
    } else {
        if (kfs_touch(filename) != KFS_OK) {
            vga_write("kdhe: failed to create file\n");
            return;
        }
        buf_size = 0;
    }

    kdhe_loop();
}

void kdhe_open_disk(uint32_t lba) {
    is_disk_mode = 1;
    disk_lba = lba;
    cursor = 0;
    nibble = 0;
    scroll_row = 0;
    ascii_mode = 0;
    dirty = 0;
    buf_size = 512;

    int idx = 0;
    const char* prefix = "Disk LBA ";
    while (*prefix) file_title[idx++] = *prefix++;
    char s[10]; int sl = 0; uint32_t tmp = lba;
    if (tmp == 0) file_title[idx++] = '0';
    else {
        while (tmp > 0) { s[sl++] = (char)('0' + (tmp % 10)); tmp /= 10; }
        while (sl > 0) file_title[idx++] = s[--sl];
    }
    file_title[idx] = 0;

    if (!ata_read28(lba, buffer)) {
        vga_write("kdhe: failed to read disk sector\n");
        return;
    }

    kdhe_loop();
}
