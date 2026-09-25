#include "kano.h"
#include "keyboard.h"
#include "kirillfs.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

#define KANO_BUFFER_SIZE  KFS_DATA_MAX   /* 2048 bytes */
#define EDIT_ROWS         21
#define SCREEN_COLS       80
#define SCREEN_ROWS       25

static char buffer[KANO_BUFFER_SIZE + 1];
static int  length = 0;
static int  cursor = 0;
static int  dirty = 0;
static char filename[KFS_NAME_MAX + 1];
static char editor_title[32];

static char cutbuffer[KANO_BUFFER_SIZE + 1];
static int  cut_len = 0;

static int  top_line = 0;
static int  left_col = 0;

static char status_msg[SCREEN_COLS + 1];
static int  status_len = 0;
static int  status_is_err = 0;

static inline uint16_t make_entry(char c, uint8_t fg, uint8_t bg) {
    return (uint16_t)(uint8_t)c | ((uint16_t)(fg | (bg << 4)) << 8);
}

static void str_copy(char* dst, const char* src, int max_len) {
    int i = 0;
    while (src[i] && i < max_len - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int str_len(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void set_status(const char* msg, int is_err) {
    status_len = 0;
    while (msg[status_len] && status_len < SCREEN_COLS) {
        status_msg[status_len] = msg[status_len];
        status_len++;
    }
    status_msg[status_len] = '\0';
    status_is_err = is_err;
}

static void append_int(char* str, int* idx, int val) {
    char tmp[12];
    int tlen = 0;
    if (val == 0) {
        if (*idx < SCREEN_COLS) str[(*idx)++] = '0';
        return;
    }
    if (val < 0) {
        if (*idx < SCREEN_COLS) str[(*idx)++] = '-';
        val = -val;
    }
    while (val > 0) {
        tmp[tlen++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (tlen > 0) {
        if (*idx < SCREEN_COLS) str[(*idx)++] = tmp[--tlen];
        else break;
    }
}

static int get_total_lines(void) {
    int lines = 1;
    for (int i = 0; i < length; i++) {
        if (buffer[i] == '\n') lines++;
    }
    return lines;
}

static int get_line_of_pos(int pos) {
    if (pos > length) pos = length;
    int line = 0;
    for (int i = 0; i < pos; i++) {
        if (buffer[i] == '\n') line++;
    }
    return line;
}

static int get_col_of_pos(int pos) {
    if (pos > length) pos = length;
    int col = 0;
    for (int i = pos - 1; i >= 0; i--) {
        if (buffer[i] == '\n') break;
        col++;
    }
    return col;
}

static int get_line_start(int line_idx) {
    if (line_idx <= 0) return 0;
    int cur = 0;
    for (int i = 0; i < length; i++) {
        if (buffer[i] == '\n') {
            cur++;
            if (cur == line_idx) return i + 1;
        }
    }
    return length;
}

static int get_line_length(int line_idx) {
    int start = get_line_start(line_idx);
    int p = start;
    while (p < length && buffer[p] != '\n') p++;
    return p - start;
}

static int get_pos_from_line_col(int target_line, int target_col) {
    int start = get_line_start(target_line);
    int p = start;
    while (p < length && buffer[p] != '\n' && (p - start) < target_col) {
        p++;
    }
    return p;
}

static void draw_shortcut(int row, int col, const char* key, const char* label) {
    uint16_t* vram = VGA_MEMORY;
    uint16_t key_attr = (uint16_t)(VGA_BLACK | (VGA_LIGHT_CYAN << 4)) << 8;
    uint16_t lbl_attr = (uint16_t)(VGA_LIGHT_GREY | (VGA_BLACK << 4)) << 8;

    vram[row * SCREEN_COLS + col + 0] = (uint16_t)key[0] | key_attr;
    vram[row * SCREEN_COLS + col + 1] = (uint16_t)key[1] | key_attr;

    int llen = str_len(label);
    for (int i = 0; i < 14; i++) {
        char ch = (i < llen) ? label[i] : ' ';
        vram[row * SCREEN_COLS + col + 2 + i] = (uint16_t)ch | lbl_attr;
    }
}

static void redraw(void) {
    uint16_t* vram = VGA_MEMORY;
    int cur_l = get_line_of_pos(cursor);
    int cur_c = get_col_of_pos(cursor);
    int total_l = get_total_lines();

    /* Auto-scrolling */
    if (cur_l < top_line) top_line = cur_l;
    if (cur_l >= top_line + EDIT_ROWS) top_line = cur_l - (EDIT_ROWS - 1);
    if (cur_c < left_col) left_col = cur_c;
    if (cur_c >= left_col + SCREEN_COLS) left_col = cur_c - (SCREEN_COLS - 1);

    /* Row 0: Nano Header Bar */
    uint16_t hdr_attr = (uint16_t)(VGA_BLACK | (VGA_LIGHT_CYAN << 4)) << 8;
    for (int c = 0; c < SCREEN_COLS; c++) {
        vram[c] = (uint16_t)' ' | hdr_attr;
    }

    const char* app_tag = "  KANO 0.2";
    int tag_len = str_len(app_tag);
    for (int c = 0; c < tag_len; c++) {
        vram[c] = (uint16_t)app_tag[c] | hdr_attr;
    }

    char file_hdr[48];
    int fh_idx = 0;
    const char* fpre = "File: ";
    while (*fpre) file_hdr[fh_idx++] = *fpre++;
    const char* fn = filename;
    while (*fn && fh_idx < 40) file_hdr[fh_idx++] = *fn++;
    file_hdr[fh_idx] = '\0';

    int fh_start = (SCREEN_COLS - fh_idx) / 2;
    for (int c = 0; c < fh_idx; c++) {
        vram[fh_start + c] = (uint16_t)file_hdr[c] | hdr_attr;
    }

    if (dirty) {
        const char* mod_str = "[Modified]";
        int mlen = str_len(mod_str);
        int mstart = SCREEN_COLS - mlen - 2;
        for (int c = 0; c < mlen; c++) {
            vram[mstart + c] = (uint16_t)mod_str[c] | hdr_attr;
        }
    }

    /* Rows 1..21: Text Editing Area */
    for (int r = 0; r < EDIT_ROWS; r++) {
        int line_idx = top_line + r;
        int row_offset = (1 + r) * SCREEN_COLS;

        if (line_idx < total_l) {
            int l_start = get_line_start(line_idx);
            int l_len = get_line_length(line_idx);

            for (int c = 0; c < SCREEN_COLS; c++) {
                int char_idx = left_col + c;
                char ch = ' ';
                if (char_idx < l_len) {
                    ch = buffer[l_start + char_idx];
                    if ((uint8_t)ch < 32 || (uint8_t)ch >= 127) ch = ' ';
                }
                vram[row_offset + c] = make_entry(ch, VGA_LIGHT_GREY, VGA_BLACK);
            }
        } else {
            for (int c = 0; c < SCREEN_COLS; c++) {
                vram[row_offset + c] = make_entry(' ', VGA_LIGHT_GREY, VGA_BLACK);
            }
        }
    }

    /* Row 22: Status / Message Line */
    uint8_t stat_fg = status_is_err ? VGA_LIGHT_RED : VGA_WHITE;
    uint8_t stat_bg = VGA_BLACK;
    int stat_row = 22 * SCREEN_COLS;
    for (int c = 0; c < SCREEN_COLS; c++) {
        char ch = (c < status_len) ? status_msg[c] : ' ';
        vram[stat_row + c] = make_entry(ch, stat_fg, stat_bg);
    }

    /* Rows 23 & 24: Nano Two-Row Shortcut Bars */
    draw_shortcut(23, 0,  "^G", " Get Help     ");
    draw_shortcut(23, 16, "^O", " WriteOut     ");
    draw_shortcut(23, 32, "^W", " Where Is     ");
    draw_shortcut(23, 48, "^K", " Cut Line     ");
    draw_shortcut(23, 64, "^C", " Cur Pos      ");

    draw_shortcut(24, 0,  "^X", " Exit         ");
    draw_shortcut(24, 16, "^R", " Read File    ");
    draw_shortcut(24, 32, "^U", " Paste        ");
    draw_shortcut(24, 48, "^\\", " Go To Line   ");
    draw_shortcut(24, 64, "^S", " QuickSave    ");

    /* Position the hardware blinking cursor */
    int screen_r = 1 + (cur_l - top_line);
    int screen_c = cur_c - left_col;
    if (screen_r >= 1 && screen_r <= EDIT_ROWS && screen_c >= 0 && screen_c < SCREEN_COLS) {
        vga_set_cursor((size_t)screen_r, (size_t)screen_c);
    }
}

static void insert_char(char c) {
    if (length >= KANO_BUFFER_SIZE - 1) {
        set_status("[ Buffer full (max 2048 bytes) ]", 1);
        return;
    }
    for (int i = length; i >= cursor; i--) {
        buffer[i + 1] = buffer[i];
    }
    buffer[cursor] = c;
    cursor++;
    length++;
    buffer[length] = '\0';
    dirty = 1;
}

static void insert_tab(void) {
    int col = get_col_of_pos(cursor);
    int spaces = 4 - (col % 4);
    if (spaces == 0) spaces = 4;
    if (length + spaces >= KANO_BUFFER_SIZE) {
        set_status("[ Buffer full ]", 1);
        return;
    }
    for (int i = 0; i < spaces; i++) {
        insert_char(' ');
    }
}

static void delete_backspace(void) {
    if (cursor <= 0) return;
    for (int i = cursor - 1; i < length; i++) {
        buffer[i] = buffer[i + 1];
    }
    cursor--;
    length--;
    buffer[length] = '\0';
    dirty = 1;
}

static void delete_char(void) {
    if (cursor >= length) return;
    for (int i = cursor; i < length; i++) {
        buffer[i] = buffer[i + 1];
    }
    length--;
    buffer[length] = '\0';
    dirty = 1;
}

static void cut_line(void) {
    if (length == 0) {
        set_status("[ Buffer is empty ]", 0);
        return;
    }
    int cur_l = get_line_of_pos(cursor);
    int start = get_line_start(cur_l);
    int end = start;
    while (end < length && buffer[end] != '\n') end++;
    if (end < length && buffer[end] == '\n') end++;

    int cut_size = end - start;
    if (cut_size == 0) return;

    for (int i = 0; i < cut_size; i++) {
        cutbuffer[i] = buffer[start + i];
    }
    cut_len = cut_size;
    cutbuffer[cut_len] = '\0';

    for (int i = start; i <= length - cut_size; i++) {
        buffer[i] = buffer[i + cut_size];
    }
    length -= cut_size;
    buffer[length] = '\0';

    if (cursor > length) cursor = length;
    else if (cursor > start) cursor = start;

    dirty = 1;
    set_status("[ Cut 1 line to cutbuffer ]", 0);
}

static void paste_line(void) {
    if (cut_len == 0) {
        set_status("[ Cutbuffer is empty ]", 1);
        return;
    }
    if (length + cut_len >= KANO_BUFFER_SIZE) {
        set_status("[ Paste would exceed buffer limit ]", 1);
        return;
    }
    for (int i = length; i >= cursor; i--) {
        buffer[i + cut_len] = buffer[i];
    }
    for (int i = 0; i < cut_len; i++) {
        buffer[cursor + i] = cutbuffer[i];
    }
    cursor += cut_len;
    length += cut_len;
    buffer[length] = '\0';
    dirty = 1;
    set_status("[ Pasted line from cutbuffer ]", 0);
}

static void show_cursor_pos(void) {
    int cur_l = get_line_of_pos(cursor) + 1;
    int total_l = get_total_lines();
    int cur_c = get_col_of_pos(cursor) + 1;
    int line_l = get_line_length(get_line_of_pos(cursor)) + 1;

    char msg[SCREEN_COLS + 1];
    int idx = 0;
    const char* p1 = "[ line ";
    while (*p1) msg[idx++] = *p1++;
    append_int(msg, &idx, cur_l);
    msg[idx++] = '/';
    append_int(msg, &idx, total_l);
    const char* p2 = " (";
    while (*p2) msg[idx++] = *p2++;
    append_int(msg, &idx, (total_l > 0) ? (cur_l * 100 / total_l) : 100);
    const char* p3 = "%), col ";
    while (*p3) msg[idx++] = *p3++;
    append_int(msg, &idx, cur_c);
    msg[idx++] = '/';
    append_int(msg, &idx, line_l);
    const char* p4 = ", char ";
    while (*p4) msg[idx++] = *p4++;
    append_int(msg, &idx, cursor);
    msg[idx++] = '/';
    append_int(msg, &idx, length);
    msg[idx++] = ' ';
    msg[idx++] = ']';
    msg[idx] = '\0';
    set_status(msg, 0);
}

static int save_file(void) {
    if (kfs_write_binary(filename, buffer, length) == KFS_OK) {
        dirty = 0;
        char msg[SCREEN_COLS + 1];
        int idx = 0;
        const char* p1 = "[ Wrote ";
        while (*p1) msg[idx++] = *p1++;
        append_int(msg, &idx, get_total_lines());
        const char* p2 = " lines (";
        while (*p2) msg[idx++] = *p2++;
        append_int(msg, &idx, length);
        const char* p3 = " bytes) to ";
        while (*p3) msg[idx++] = *p3++;
        const char* fn = filename;
        while (*fn && idx < SCREEN_COLS - 4) msg[idx++] = *fn++;
        msg[idx++] = ' ';
        msg[idx++] = ']';
        msg[idx] = '\0';
        set_status(msg, 0);
        return 1;
    } else {
        set_status("[ Save failed: file exceeds FS limit or disk error ]", 1);
        return 0;
    }
}

static void prompt_search(void) {
    char query[40];
    int qlen = 0;
    query[0] = '\0';

    for (;;) {
        vga_set_cursor(22, 0);
        vga_set_color(VGA_BLACK, VGA_LIGHT_CYAN);
        vga_write("Search: ");
        vga_write(query);
        for (int i = 8 + qlen; i < SCREEN_COLS; i++) vga_putchar(' ');
        vga_set_cursor(22, 8 + qlen);

        char c = keyboard_getchar();
        if (c == KEY_ESC || c == KEY_CTRL_C) {
            set_status("[ Cancelled ]", 0);
            return;
        }
        if (c == KEY_ENTER) break;
        if (c == KEY_BACKSPACE) {
            if (qlen > 0) {
                qlen--;
                query[qlen] = '\0';
            }
        } else if (c >= 32 && c < 127 && qlen < 38) {
            query[qlen++] = c;
            query[qlen] = '\0';
        }
    }

    if (qlen == 0) {
        set_status("[ Cancelled ]", 0);
        return;
    }

    int found = -1;
    for (int i = cursor + 1; i <= length - qlen; i++) {
        int match = 1;
        for (int k = 0; k < qlen; k++) {
            char b = buffer[i + k];
            char q = query[k];
            if (b >= 'A' && b <= 'Z') b += 32;
            if (q >= 'A' && q <= 'Z') q += 32;
            if (b != q) { match = 0; break; }
        }
        if (match) { found = i; break; }
    }
    if (found == -1) {
        for (int i = 0; i <= cursor && i <= length - qlen; i++) {
            int match = 1;
            for (int k = 0; k < qlen; k++) {
                char b = buffer[i + k];
                char q = query[k];
                if (b >= 'A' && b <= 'Z') b += 32;
                if (q >= 'A' && q <= 'Z') q += 32;
                if (b != q) { match = 0; break; }
            }
            if (match) { found = i; break; }
        }
    }

    if (found != -1) {
        cursor = found;
        set_status("[ Found occurrence ]", 0);
    } else {
        set_status("[ Text not found ]", 1);
    }
}

static void prompt_goto_line(void) {
    char num_str[16];
    int nlen = 0;
    num_str[0] = '\0';

    for (;;) {
        vga_set_cursor(22, 0);
        vga_set_color(VGA_BLACK, VGA_LIGHT_CYAN);
        vga_write("Enter line number: ");
        vga_write(num_str);
        for (int i = 19 + nlen; i < SCREEN_COLS; i++) vga_putchar(' ');
        vga_set_cursor(22, 19 + nlen);

        char c = keyboard_getchar();
        if (c == KEY_ESC || c == KEY_CTRL_C) {
            set_status("[ Cancelled ]", 0);
            return;
        }
        if (c == KEY_ENTER) break;
        if (c == KEY_BACKSPACE) {
            if (nlen > 0) {
                nlen--;
                num_str[nlen] = '\0';
            }
        } else if (c >= '0' && c <= '9' && nlen < 8) {
            num_str[nlen++] = c;
            num_str[nlen] = '\0';
        }
    }

    if (nlen == 0) {
        set_status("[ Cancelled ]", 0);
        return;
    }

    int target = 0;
    for (int i = 0; i < nlen; i++) {
        target = target * 10 + (num_str[i] - '0');
    }
    int total = get_total_lines();
    if (target < 1) target = 1;
    if (target > total) target = total;

    cursor = get_pos_from_line_col(target - 1, 0);
    set_status("[ Jumped to line ]", 0);
}

static void prompt_read_file(void) {
    char rname[KFS_NAME_MAX + 1];
    int rlen = 0;
    rname[0] = '\0';

    for (;;) {
        vga_set_cursor(22, 0);
        vga_set_color(VGA_BLACK, VGA_LIGHT_CYAN);
        vga_write("File to insert [from KirillFS]: ");
        vga_write(rname);
        for (int i = 32 + rlen; i < SCREEN_COLS; i++) vga_putchar(' ');
        vga_set_cursor(22, 32 + rlen);

        char c = keyboard_getchar();
        if (c == KEY_ESC || c == KEY_CTRL_C) {
            set_status("[ Cancelled ]", 0);
            return;
        }
        if (c == KEY_ENTER) break;
        if (c == KEY_BACKSPACE) {
            if (rlen > 0) {
                rlen--;
                rname[rlen] = '\0';
            }
        } else if (c >= 33 && c <= 126 && rlen < KFS_NAME_MAX) {
            rname[rlen++] = c;
            rname[rlen] = '\0';
        }
    }

    if (rlen == 0) {
        set_status("[ Cancelled ]", 0);
        return;
    }

    const char* rdata;
    int rsize = 0;
    if (kfs_read(rname, &rdata, &rsize) != KFS_OK) {
        set_status("[ Cannot open file to insert ]", 1);
        return;
    }

    if (length + rsize >= KANO_BUFFER_SIZE) {
        set_status("[ Insertion would exceed buffer limit ]", 1);
        return;
    }

    for (int i = length; i >= cursor; i--) {
        buffer[i + rsize] = buffer[i];
    }
    for (int i = 0; i < rsize; i++) {
        buffer[cursor + i] = rdata[i];
    }
    cursor += rsize;
    length += rsize;
    buffer[length] = '\0';
    dirty = 1;
    set_status("[ Inserted file ]", 0);
}

static void show_help(void) {
    vga_clear();
    vga_set_color(VGA_BLACK, VGA_LIGHT_CYAN);
    vga_write("                         KANO v0.2 - GNU nano Clone Help                        \n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_write("\n");
    vga_write("  Navigation:\n");
    vga_write("    Arrow Keys       - Move cursor Left, Right, Up, Down\n");
    vga_write("    Home / Ctrl+A    - Move to start of line\n");
    vga_write("    End  / Ctrl+E    - Move to end of line\n");
    vga_write("    PageUp / PageDn  - Scroll 21 lines up or down\n");
    vga_write("\n");
    vga_write("  File Operations:\n");
    vga_write("    Ctrl+O / Ctrl+S  - WriteOut (Save buffer to disk)\n");
    vga_write("    Ctrl+R           - Read file and insert at current cursor\n");
    vga_write("    Ctrl+X / Ctrl+Q  - Exit editor (prompts to save if modified)\n");
    vga_write("\n");
    vga_write("  Editing Commands:\n");
    vga_write("    Ctrl+K           - Cut current line into cutbuffer\n");
    vga_write("    Ctrl+U           - Uncut (Paste) line from cutbuffer\n");
    vga_write("    Ctrl+W           - Where Is (Search text, case-insensitive)\n");
    vga_write("    Ctrl+\\           - Go To Line number\n");
    vga_write("    Ctrl+C           - Display current line/col and character count\n");
    vga_write("    Tab              - Insert 4 spaces (smart tabstop alignment)\n");
    vga_write("    Del / Backspace  - Delete character under/before cursor\n");
    vga_write("\n");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write("  [ Press any key to return to editing ]\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    keyboard_getchar();
}

static int confirm_exit(void) {
    if (!dirty) return 1;

    vga_set_cursor(22, 0);
    vga_set_color(VGA_BLACK, VGA_LIGHT_CYAN);
    vga_write("Save modified buffer? (Y)es, (N)o, (C)ancel: ");
    for (int i = 45; i < SCREEN_COLS; i++) vga_putchar(' ');
    vga_set_cursor(22, 45);

    for (;;) {
        char c = keyboard_getchar();
        if (c == 'y' || c == 'Y') {
            return save_file();
        } else if (c == 'n' || c == 'N') {
            return 1;
        } else if (c == 'c' || c == 'C' || c == KEY_ESC || c == KEY_CTRL_C) {
            set_status("[ Cancelled ]", 0);
            return 0;
        }
    }
}

void kano_open_named(const char* name, const char* title) {
    const char* data;
    int size;
    str_copy(filename, name, KFS_NAME_MAX + 1);
    str_copy(editor_title, title, 32);
    length = 0;
    cursor = 0;
    dirty = 0;
    top_line = 0;
    left_col = 0;
    cut_len = 0;
    status_len = 0;
    status_is_err = 0;

    if (kfs_read(name, &data, &size) == KFS_OK) {
        if (size >= KANO_BUFFER_SIZE) size = KANO_BUFFER_SIZE - 1;
        for (int i = 0; i < size; i++) buffer[i] = data[i];
        length = size;
        buffer[length] = '\0';

        char msg[SCREEN_COLS + 1];
        int idx = 0;
        const char* p1 = "[ Read ";
        while (*p1) msg[idx++] = *p1++;
        append_int(msg, &idx, get_total_lines());
        const char* p2 = " lines (";
        while (*p2) msg[idx++] = *p2++;
        append_int(msg, &idx, length);
        const char* p3 = " bytes) ]";
        while (*p3) msg[idx++] = *p3++;
        msg[idx] = '\0';
        set_status(msg, 0);
    } else if (kfs_touch(name) == KFS_OK) {
        buffer[0] = '\0';
        length = 0;
        set_status("[ New File ]", 0);
    } else {
        vga_clear();
        vga_write("kano: cannot create or open file\n");
        return;
    }

    for (;;) {
        redraw();
        char c = keyboard_getchar();

        /* Exit commands */
        if (c == KEY_CTRL_X || c == KEY_QUIT || c == KEY_ESC) {
            if (confirm_exit()) {
                vga_clear();
                return;
            }
        }
        /* Save / WriteOut */
        else if (c == KEY_CTRL_O || c == KEY_SAVE) {
            save_file();
        }
        /* Help */
        else if (c == KEY_CTRL_G) {
            show_help();
        }
        /* Search */
        else if (c == KEY_CTRL_W) {
            prompt_search();
        }
        /* Cut line */
        else if (c == KEY_CTRL_K) {
            cut_line();
        }
        /* Paste line */
        else if (c == KEY_CTRL_U) {
            paste_line();
        }
        /* Cur Pos */
        else if (c == KEY_CTRL_C) {
            show_cursor_pos();
        }
        /* Read file */
        else if (c == KEY_CTRL_R) {
            prompt_read_file();
        }
        /* Go to line: Ctrl+\ (ASCII 28) or Ctrl+J */
        else if (c == 28 || c == KEY_CTRL_J) {
            prompt_goto_line();
        }
        /* Navigation: Home / Ctrl+A */
        else if (c == KEY_HOME || c == KEY_CTRL_A) {
            int cur_l = get_line_of_pos(cursor);
            cursor = get_line_start(cur_l);
        }
        /* Navigation: End / Ctrl+E */
        else if (c == KEY_END || c == KEY_CTRL_E) {
            int cur_l = get_line_of_pos(cursor);
            int start = get_line_start(cur_l);
            int llen = get_line_length(cur_l);
            cursor = start + llen;
        }
        /* Navigation: Left */
        else if (c == KEY_LEFT) {
            if (cursor > 0) cursor--;
        }
        /* Navigation: Right */
        else if (c == KEY_RIGHT) {
            if (cursor < length) cursor++;
        }
        /* Navigation: Up */
        else if (c == KEY_UP) {
            int cur_l = get_line_of_pos(cursor);
            int cur_c = get_col_of_pos(cursor);
            if (cur_l > 0) {
                cursor = get_pos_from_line_col(cur_l - 1, cur_c);
            } else {
                cursor = 0;
            }
        }
        /* Navigation: Down */
        else if (c == KEY_DOWN) {
            int cur_l = get_line_of_pos(cursor);
            int cur_c = get_col_of_pos(cursor);
            int total_l = get_total_lines();
            if (cur_l < total_l - 1) {
                cursor = get_pos_from_line_col(cur_l + 1, cur_c);
            } else {
                cursor = length;
            }
        }
        /* Navigation: PgUp */
        else if (c == KEY_PGUP) {
            int cur_l = get_line_of_pos(cursor);
            int cur_c = get_col_of_pos(cursor);
            cur_l = (cur_l > EDIT_ROWS) ? (cur_l - EDIT_ROWS) : 0;
            cursor = get_pos_from_line_col(cur_l, cur_c);
        }
        /* Navigation: PgDn */
        else if (c == KEY_PGDN) {
            int cur_l = get_line_of_pos(cursor);
            int cur_c = get_col_of_pos(cursor);
            int total_l = get_total_lines();
            cur_l = (cur_l + EDIT_ROWS < total_l) ? (cur_l + EDIT_ROWS) : (total_l - 1);
            cursor = get_pos_from_line_col(cur_l, cur_c);
        }
        /* Editing: Backspace */
        else if (c == KEY_BACKSPACE) {
            delete_backspace();
        }
        /* Editing: Delete */
        else if (c == KEY_DEL) {
            delete_char();
        }
        /* Editing: Enter */
        else if (c == KEY_ENTER) {
            insert_char('\n');
        }
        /* Editing: Tab */
        else if (c == KEY_TAB) {
            insert_tab();
        }
        /* Editing: Printable characters */
        else if ((uint8_t)c >= 32 && (uint8_t)c < 127) {
            insert_char(c);
        }
    }
}

void kano_open(const char* name) {
    kano_open_named(name, "KANO");
}
