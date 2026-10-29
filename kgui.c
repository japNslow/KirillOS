#include "kgui.h"
#include "kguiq.h"
#include "kguid.h"
#include "mode13.h"
#include "vga.h"
#include "mouse.h"
#include "keyboard.h"
#include "kirillfs.h"
#include "kaint.h"
#include "kcommander.h"
#include "sound.h"
#include <stdint.h>

extern void kernel_execute_command(char* cmd);

static int kgui_exit_flag = 0;

static void kgui_redraw(void);

static int kgui_streq(const char* a, const char* b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static int kgui_str_starts_with(const char* s, const char* prefix) {
    int i = 0;
    while (prefix[i]) {
        if (s[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

static int kgui_strlen(const char* s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void kgui_strcpy(char* dst, const char* src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int kgui_ends_with(const char* s, const char* suffix) {
    int ls = kgui_strlen(s), lx = kgui_strlen(suffix);
    return ls >= lx && kgui_streq(s + ls - lx, suffix);
}

static inline uint8_t kgui_inb(uint16_t port) {
    uint8_t r;
    __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

/* Programs started from KGUI (kc, kaint, mode13...) leave Mode 13h and disable the mouse */
static void restore_graphics(void) {
    if (!mode13_is_active()) {
        mode13_enter();
        mouse_init();
    }
}

/* ================================================================ */
/*                       App: Terminal State                        */
/* ================================================================ */
#define TERM_MAX_LINES 32
#define TERM_LINE_LEN  32   /* 31 chars: fits the 265-px terminal window */
static char term_lines[TERM_MAX_LINES][TERM_LINE_LEN];
static int  term_line_count = 0;
static char term_input[TERM_LINE_LEN];
static int  term_input_len = 0;

static char term_out_line[TERM_LINE_LEN];
static int  term_out_len = 0;

static void term_add_line(const char* s) {
    if (term_line_count < TERM_MAX_LINES) {
        int i = 0;
        while (s && s[i] && i < TERM_LINE_LEN - 1) {
            term_lines[term_line_count][i] = s[i];
            i++;
        }
        term_lines[term_line_count][i] = 0;
        term_line_count++;
    } else {
        /* Scroll up */
        for (int i = 0; i < TERM_MAX_LINES - 1; i++) {
            for (int j = 0; j < TERM_LINE_LEN; j++) {
                term_lines[i][j] = term_lines[i + 1][j];
            }
        }
        int i = 0;
        while (s && s[i] && i < TERM_LINE_LEN - 1) {
            term_lines[TERM_MAX_LINES - 1][i] = s[i];
            i++;
        }
        term_lines[TERM_MAX_LINES - 1][i] = 0;
    }
}

static void kgui_term_char_hook(char c) {
    if (!mode13_is_active()) {
        /* A program launched from KGUI switched to text mode: print on the real screen */
        vga_set_output_hook(0);
        if (c == '\f') vga_clear(); else vga_putchar(c);
        vga_set_output_hook(kgui_term_char_hook);
        return;
    }
    if (c == '\f') {
        term_line_count = 0;
        term_out_len = 0;
        term_out_line[0] = 0;
        return;
    }
    if (c == '\n') {
        term_out_line[term_out_len] = 0;
        term_add_line(term_out_line);
        term_out_len = 0;
        term_out_line[0] = 0;
        /* Show progress of long commands (ping, dhcp...) line by line */
        if (mode13_is_active()) kgui_redraw();
        return;
    }
    if (c == '\r') {
        return;
    }
    if (c == '\b') {
        if (term_out_len > 0) {
            term_out_line[--term_out_len] = 0;
        }
        return;
    }
    if (c >= 32 && c <= 126) {
        if (term_out_len < TERM_LINE_LEN - 1) {
            term_out_line[term_out_len++] = c;
            term_out_line[term_out_len] = 0;
        } else {
            term_out_line[term_out_len] = 0;
            term_add_line(term_out_line);
            term_out_len = 0;
            term_out_line[term_out_len++] = c;
            term_out_line[term_out_len] = 0;
        }
    }
}

/* ================================================================ */
/*                       App: Notepad Documents                     */
/* ================================================================ */
#define NOTE_DOCS 4
#define NOTE_MAX  4096

typedef struct {
    int  win_id;                       /* 0 = slot free */
    char name[KFS_NAME_MAX + 1];
    char text[NOTE_MAX + 1];
    int  len, cursor, top, left;
    int  dirty, readonly;
    int  naming;                       /* 0 = off, 1 = open file, 2 = save as */
    char name_edit[KFS_NAME_MAX + 1];
    int  confirm_close, confirm_discard;
    char status[30];
} note_doc_t;

static note_doc_t notes[NOTE_DOCS];

static note_doc_t* note_find(int win_id) {
    for (int i = 0; i < NOTE_DOCS; i++)
        if (win_id > 0 && notes[i].win_id == win_id) return &notes[i];
    return 0;
}

static void note_status(note_doc_t* d, const char* msg) {
    kgui_strcpy(d->status, msg, sizeof(d->status));
}

static void note_set_title(note_doc_t* d) {
    kgui_window_t* w = kguid_get_window(d->win_id);
    if (w) kgui_strcpy(w->title, d->name, sizeof(w->title));
}

static void note_load(note_doc_t* d, const char* name) {
    const char* data;
    int size;
    kgui_strcpy(d->name, name, sizeof(d->name));
    d->len = d->cursor = d->top = d->left = 0;
    d->dirty = d->readonly = 0;
    if (kfs_read(name, &data, &size) == KFS_OK) {
        if (size > NOTE_MAX) {
            size = NOTE_MAX;
            d->readonly = 1;
        }
        for (int i = 0; i < size; i++) d->text[i] = data[i];
        d->len = size;
        note_status(d, d->readonly ? "Too big: read-only" : "File loaded");
    } else {
        note_status(d, "New file");
    }
    d->text[d->len] = 0;
    note_set_title(d);
}

static void note_save(note_doc_t* d) {
    if (d->readonly) { note_status(d, "Read-only: not saved"); return; }
    kfs_result_t r = kfs_touch(d->name);
    if (r != KFS_OK && r != KFS_EXISTS) {
        note_status(d, r == KFS_FULL ? "Disk full (16 files)" : "Bad file name");
        return;
    }
    if (kfs_write_binary(d->name, d->text, d->len) != KFS_OK) {
        note_status(d, "Save failed");
        return;
    }
    d->dirty = 0;
    note_status(d, "Saved");
}

/* Open (or focus, if already open) a notepad window for a file */
static int note_open(const char* name) {
    for (int i = 0; i < NOTE_DOCS; i++) {
        if (notes[i].win_id && kgui_streq(notes[i].name, name)) {
            kguid_focus_window(notes[i].win_id);
            return notes[i].win_id;
        }
    }
    for (int i = 0; i < NOTE_DOCS; i++) {
        if (notes[i].win_id) continue;
        int id = kguid_create_window(name, 40 + i * 12, 20 + i * 10, 220, 130,
                                     KGUID_APP_NOTEPAD, KGUI_ICON_NOTEPAD);
        if (id < 0) return -1;
        notes[i].win_id = id;
        notes[i].naming = 0;
        notes[i].confirm_close = notes[i].confirm_discard = 0;
        note_load(&notes[i], name);
        return id;
    }
    return -1;
}

static int note_line_start(const note_doc_t* d, int pos) {
    while (pos > 0 && d->text[pos - 1] != '\n') pos--;
    return pos;
}

static int note_line_end(const note_doc_t* d, int pos) {
    while (pos < d->len && d->text[pos] != '\n') pos++;
    return pos;
}

static void note_cursor_line_col(const note_doc_t* d, int* line, int* col) {
    int l = 0;
    for (int i = 0; i < d->cursor; i++) if (d->text[i] == '\n') l++;
    *line = l;
    *col = d->cursor - note_line_start(d, d->cursor);
}

/* Position of (line, col), clamped to the text */
static int note_pos_of(const note_doc_t* d, int line, int col) {
    int pos = 0;
    while (line > 0 && pos < d->len) {
        if (d->text[pos++] == '\n') line--;
    }
    int end = note_line_end(d, pos);
    return (pos + col < end) ? pos + col : end;
}

/* Client area layout of a notepad window */
#define NOTE_TOOLBAR_H 13
#define NOTE_STATUS_H  10
static int note_rows(const kgui_window_t* w) { return (w->h - 22 - NOTE_TOOLBAR_H - NOTE_STATUS_H - 4) / 9; }
static int note_cols(const kgui_window_t* w) { return (w->w - 8 - 6) / 8; }

static void note_scroll_to_cursor(note_doc_t* d) {
    kgui_window_t* w = kguid_get_window(d->win_id);
    if (!w) return;
    int line, col, rows = note_rows(w), cols = note_cols(w);
    note_cursor_line_col(d, &line, &col);
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;
    if (line < d->top) d->top = line;
    if (line >= d->top + rows) d->top = line - rows + 1;
    if (col < d->left) d->left = col;
    if (col >= d->left + cols) d->left = col - cols + 1;
}

static void note_insert(note_doc_t* d, char c) {
    if (d->readonly) { note_status(d, "Read-only"); return; }
    if (d->len >= NOTE_MAX) { note_status(d, "Note is full (4 KB)"); return; }
    for (int i = d->len; i > d->cursor; i--) d->text[i] = d->text[i - 1];
    d->text[d->cursor++] = c;
    d->text[++d->len] = 0;
    d->dirty = 1;
}

static void note_delete_at(note_doc_t* d, int pos) {
    if (d->readonly) { note_status(d, "Read-only"); return; }
    if (pos < 0 || pos >= d->len) return;
    for (int i = pos; i < d->len; i++) d->text[i] = d->text[i + 1];
    d->len--;
    d->dirty = 1;
}

static void note_start_naming(note_doc_t* d, int mode) {
    d->naming = mode;
    d->confirm_discard = 0;
    kgui_strcpy(d->name_edit, mode == 2 ? d->name : "", sizeof(d->name_edit));
    note_status(d, mode == 1 ? "Open: type name, Enter" : "Save as: name, Enter");
}

static void note_commit_naming(note_doc_t* d) {
    if (!d->name_edit[0]) { note_status(d, "Type a file name"); return; }
    if (d->naming == 1) {
        if (d->dirty && !d->confirm_discard) {
            d->confirm_discard = 1;
            note_status(d, "Unsaved! Enter=discard");
            return;
        }
        d->naming = 0;
        note_load(d, d->name_edit);
    } else {
        d->naming = 0;
        kgui_strcpy(d->name, d->name_edit, sizeof(d->name));
        note_set_title(d);
        note_save(d);
    }
}

static void note_key(note_doc_t* d, char key) {
    d->confirm_close = 0;
    if (d->naming) {
        int n = kgui_strlen(d->name_edit);
        if (key == '\n') note_commit_naming(d);
        else if (key == '\b') { if (n > 0) d->name_edit[n - 1] = 0; d->confirm_discard = 0; }
        else if (key > 32 && key < 127 && n < KFS_NAME_MAX - 1) {
            d->name_edit[n] = key;
            d->name_edit[n + 1] = 0;
            d->confirm_discard = 0;
        }
        return;
    }

    kgui_window_t* w = kguid_get_window(d->win_id);
    int line, col;
    note_cursor_line_col(d, &line, &col);
    d->status[0] = 0;

    if (key == KEY_SAVE) note_save(d);
    else if (key == KEY_CTRL_O) note_start_naming(d, 1);
    else if (key == '\n') note_insert(d, '\n');
    else if (key == '\t') { do note_insert(d, ' '); while (!d->readonly && d->len < NOTE_MAX && ++col % 4); }
    else if (key == '\b') { if (d->cursor > 0 && !d->readonly) { d->cursor--; note_delete_at(d, d->cursor); } }
    else if (key == KEY_DEL) note_delete_at(d, d->cursor);
    else if (key == KEY_LEFT) { if (d->cursor > 0) d->cursor--; }
    else if (key == KEY_RIGHT) { if (d->cursor < d->len) d->cursor++; }
    else if (key == KEY_UP) { if (line > 0) d->cursor = note_pos_of(d, line - 1, col); }
    else if (key == KEY_DOWN) d->cursor = note_pos_of(d, line + 1, col);
    else if (key == KEY_PGUP) d->cursor = note_pos_of(d, line > note_rows(w) ? line - note_rows(w) : 0, col);
    else if (key == KEY_PGDN) d->cursor = note_pos_of(d, line + note_rows(w), col);
    else if (key == KEY_HOME) d->cursor = note_line_start(d, d->cursor);
    else if (key == KEY_END) d->cursor = note_line_end(d, d->cursor);
    else if (key >= 32 && key < 127) note_insert(d, key);
    note_scroll_to_cursor(d);
}

static void draw_note(kgui_window_t* win, note_doc_t* d, int cx, int cy, int cw, int ch) {
    /* Toolbar: Save | Open | As | file name (or name input) */
    kguiq_draw_rect_fill(cx, cy, cw, NOTE_TOOLBAR_H, KGUI_3D_FACE);
    kguiq_draw_button(cx + 1, cy + 1, 38, 11, "Save", 0, 0);
    kguiq_draw_button(cx + 40, cy + 1, 38, 11, "Open", 0, 0);
    kguiq_draw_button(cx + 79, cy + 1, 22, 11, "As", 0, 0);
    if (d->naming) {
        kguiq_draw_input(cx + 103, cy + 1, cw - 104, 11, d->name_edit, kgui_strlen(d->name_edit), 1);
    } else {
        kguiq_draw_string_clipped(cx + 105, cy + 3, d->name, KGUI_COLOR_DARK_GREY, cx + cw - 2);
    }

    /* Text */
    int tx0 = cx + 3, ty0 = cy + NOTE_TOOLBAR_H + 2;
    int rows = note_rows(win), cols = note_cols(win);
    kguiq_draw_rect_fill(cx, cy + NOTE_TOOLBAR_H, cw, ch - NOTE_TOOLBAR_H - NOTE_STATUS_H, KGUI_COLOR_WHITE);
    int pos = note_pos_of(d, d->top, 0);
    if (d->top > 0 && pos == 0) pos = d->len;               /* past the last line */
    for (int r = 0; r < rows && pos <= d->len; r++) {
        int col = 0;
        while (pos < d->len && d->text[pos] != '\n') {
            if (col >= d->left && col - d->left < cols)
                mode13_draw_char(tx0 + (col - d->left) * 8, ty0 + r * 9, d->text[pos], KGUI_COLOR_BLACK);
            col++;
            pos++;
        }
        if (pos >= d->len) break;
        pos++;                                               /* skip '\n' */
    }

    /* Cursor */
    int line, col;
    note_cursor_line_col(d, &line, &col);
    if (win->active && !d->naming && line >= d->top && line < d->top + rows &&
        col >= d->left && col - d->left < cols) {
        int x = tx0 + (col - d->left) * 8, y = ty0 + (line - d->top) * 9;
        mode13_draw_line(x, y, x, y + 7, KGUI_COLOR_BLUE);
    }

    /* Status bar */
    int sy = cy + ch - NOTE_STATUS_H;
    kguiq_draw_rect_fill(cx, sy, cw, NOTE_STATUS_H, KGUI_3D_FACE);
    char buf[32];
    if (d->status[0]) {
        kgui_strcpy(buf, d->status, sizeof(buf));
    } else {
        /* "Ln 12 Col 4 *" */
        int n = 0, v;
        char digits[6];
        buf[n++] = 'L'; buf[n++] = 'n'; buf[n++] = ' ';
        for (int part = 0; part < 2; part++) {
            v = (part == 0 ? line : col) + 1;
            int k = 0;
            do { digits[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 5);
            while (k) buf[n++] = digits[--k];
            if (part == 0) { buf[n++] = ' '; buf[n++] = 'C'; buf[n++] = 'o'; buf[n++] = 'l'; buf[n++] = ' '; }
        }
        if (d->dirty) { buf[n++] = ' '; buf[n++] = '*'; }
        if (d->readonly) { buf[n++] = ' '; buf[n++] = 'R'; buf[n++] = 'O'; }
        buf[n] = 0;
    }
    kguiq_draw_string_clipped(cx + 2, sy + 1, buf, KGUI_COLOR_BLACK, cx + cw - 2);
}

/* Toolbar buttons and click-to-place-cursor */
static void note_click(kgui_window_t* win, note_doc_t* d, int mx, int my, int cx, int cy) {
    d->confirm_close = 0;
    if (my < cy + NOTE_TOOLBAR_H) {
        if (kguiq_point_in_rect(mx, my, cx + 1, cy + 1, 38, 11)) {
            if (d->naming == 2) note_commit_naming(d);
            else { d->naming = 0; note_save(d); }
        } else if (kguiq_point_in_rect(mx, my, cx + 40, cy + 1, 38, 11)) {
            if (d->naming == 1) note_commit_naming(d);
            else note_start_naming(d, 1);
        } else if (kguiq_point_in_rect(mx, my, cx + 79, cy + 1, 22, 11)) {
            note_start_naming(d, 2);
        }
        return;
    }
    int row = (my - (cy + NOTE_TOOLBAR_H + 2)) / 9;
    if (row < 0 || row >= note_rows(win) || mx < cx + 3) return;
    d->cursor = note_pos_of(d, d->top + row, d->left + (mx - (cx + 3)) / 8);
    d->status[0] = 0;
}

/* Close a window; a modified notepad asks for a second click first */
static void kgui_close_window(int id) {
    note_doc_t* d = note_find(id);
    if (d && d->dirty && !d->confirm_close) {
        d->confirm_close = 1;
        note_status(d, "Unsaved! X again=discard");
        kguid_focus_window(id);
        return;
    }
    if (d) d->win_id = 0;
    kguid_close_window(id);
}

/* ================================================================ */
/*                       App: Files Explorer State                  */
/* ================================================================ */
static int files_selected = 0;
static int files_top = 0;
static int files_del_armed = -1;   /* index waiting for the second Del click */

static int files_rows(const kgui_window_t* w) { return (w->h - 22 - 3 - 16) / 11; }

static void files_clamp(const kgui_window_t* w) {
    int count = kfs_file_count(), rows = files_rows(w);
    if (files_selected >= count) files_selected = count - 1;
    if (files_selected < 0) files_selected = 0;
    if (files_top > files_selected) files_top = files_selected;
    if (rows > 0 && files_selected >= files_top + rows) files_top = files_selected - rows + 1;
    if (files_top < 0) files_top = 0;
}

static void files_open_selected(void) {
    const char* fname = kfs_name(files_selected);
    if (!fname) return;
    if (kgui_ends_with(fname, ".bmp") || kgui_ends_with(fname, ".BMP")) kguid_set_wallpaper(fname);
    else note_open(fname);
}

static void files_delete_selected(void) {
    const char* fname = kfs_name(files_selected);
    if (!fname) return;
    if (files_del_armed != files_selected) { files_del_armed = files_selected; return; }
    char name[KFS_NAME_MAX + 1];
    kgui_strcpy(name, fname, sizeof(name));
    kfs_remove(name);
    files_del_armed = -1;
}

/* ================================================================ */
/*                       App: Settings State                        */
/* ================================================================ */
static char settings_wall_input[KFS_NAME_MAX + 1] = "demo.bmp";

/* ================================================================ */
/*                     Opening apps with sane sizes                 */
/* ================================================================ */
static void open_app(kguid_app_t app, int x, int y) {
    switch (app) {
        case KGUID_APP_TERMINAL: kguid_create_window("Terminal", 50, 16, 265, 130, app, KGUI_ICON_TERMINAL); break;
        case KGUID_APP_FILES:    kguid_create_window("Files", x, y, 170, 120, app, KGUI_ICON_FILES); break;
        case KGUID_APP_NOTEPAD:  note_open("notes.txt"); break;
        case KGUID_APP_VIEWER:   kguid_create_window("Paint", x, y, 160, 95, app, KGUI_ICON_PAINT); break;
        case KGUID_APP_SETTINGS: kguid_create_window("Display Settings", x, y, 160, 105, app, KGUI_ICON_SETTINGS); break;
        case KGUID_APP_SYSINFO:  kguid_create_window("System Info", x, y, 165, 95, app, KGUI_ICON_SYSINFO); break;
        default: break;
    }
}

static void term_execute(void) {
    if (term_input_len == 0) return;

    /* 1. Echo user prompt */
    char echo_buf[TERM_LINE_LEN];
    echo_buf[0] = '>';
    echo_buf[1] = ' ';
    int el = 2;
    for (int i = 0; i < term_input_len && el < TERM_LINE_LEN - 1; i++) {
        echo_buf[el++] = term_input[i];
    }
    echo_buf[el] = 0;
    term_add_line(echo_buf);

    /* 2. Special GUI commands */
    if (kgui_str_starts_with(term_input, "wallpaper ") || kgui_str_starts_with(term_input, "wall ")) {
        char* fname = term_input + (term_input[1] == 'a' && term_input[2] == 'l' && term_input[3] == 'l' && term_input[4] == ' ' ? 5 : 10);
        while (*fname == ' ') fname++;
        if (*fname) {
            kguid_set_wallpaper(fname);
            term_add_line("Wallpaper updated!");
        } else {
            kguid_set_wallpaper("demo.bmp");
            term_add_line("Default wallpaper set");
        }
    } else if (kgui_streq(term_input, "exit gui") || kgui_streq(term_input, "quit gui")) {
        kgui_exit_flag = 1;
    } else if (kgui_streq(term_input, "exit") || kgui_streq(term_input, "quit")) {
        int f = kguid_get_focused_window();
        if (f != -1) kgui_close_window(f);
    } else if (kgui_streq(term_input, "kgui") || kgui_streq(term_input, "gui") || kgui_streq(term_input, "win")) {
        term_add_line("Already in KGUI");
    } else if (kgui_str_starts_with(term_input, "kano ") || kgui_str_starts_with(term_input, "edit ")) {
        char* fname = term_input + 5;
        while (*fname == ' ') fname++;
        if (*fname) {
            term_add_line(note_open(fname) < 0 ? "No free notepad (max 4)" : "Opened editor window.");
        }
    } else {
        /* 3. Execute directly in REAL KirillOS Kernel */
        term_out_len = 0;
        term_out_line[0] = 0;
        vga_set_output_hook(kgui_term_char_hook);

        kernel_execute_command(term_input);

        if (term_out_len > 0) {
            term_out_line[term_out_len] = 0;
            term_add_line(term_out_line);
            term_out_len = 0;
        }
        vga_set_output_hook(0);
        restore_graphics();
    }

    term_input_len = 0;
    term_input[0] = 0;
}

/* ================================================================ */
/*                     Window Client Drawing Dispatcher             */
/* ================================================================ */
static void draw_window_client(kgui_window_t* win) {
    int cx = win->x + 4;
    int cy = win->y + 18;
    int cw = win->w - 8;
    int ch = win->h - 22;

    switch (win->app_type) {
        case KGUID_APP_TERMINAL: {
            /* Black background */
            kguiq_draw_rect_fill(cx, cy, cw, ch, KGUI_COLOR_BLACK);
            /* Terminal lines: show most recent lines that fit above the prompt */
            int visible_slots = (ch - 14) / 9;
            if (visible_slots < 1) visible_slots = 1;
            int start_idx = 0;
            if (term_line_count > visible_slots) {
                start_idx = term_line_count - visible_slots;
            }
            int start_y = cy + 2;
            for (int i = start_idx; i < term_line_count; i++) {
                kguiq_draw_string_clipped(cx + 2, start_y, term_lines[i], KGUI_COLOR_LIGHT_GREEN, cx + cw - 2);
                start_y += 9;
            }
            /* Prompt and current input */
            if (start_y + 8 <= cy + ch) {
                mode13_draw_string(cx + 2, start_y, ">", KGUI_COLOR_WHITE);
                kguiq_draw_string_clipped(cx + 12, start_y, term_input, KGUI_COLOR_WHITE, cx + cw - 10);
                int cur_x = cx + 12 + term_input_len * 8;
                if (cur_x < cx + cw - 8) {
                    mode13_draw_line(cur_x, start_y, cur_x, start_y + 7, KGUI_COLOR_LIGHT_GREEN);
                }
            }
            break;
        }

        case KGUID_APP_NOTEPAD: {
            note_doc_t* d = note_find(win->id);
            if (d) draw_note(win, d, cx, cy, cw, ch);
            else kguiq_draw_rect_fill(cx, cy, cw, ch, KGUI_COLOR_WHITE);
            break;
        }

        case KGUID_APP_FILES: {
            kguiq_draw_rect_fill(cx, cy, cw, ch, KGUI_COLOR_WHITE);
            files_clamp(win);
            int count = kfs_file_count();
            int rows = files_rows(win);
            int fy = cy + 3;
            for (int i = files_top; i < count && i < files_top + rows; i++) {
                if (i == files_selected) {
                    kguiq_draw_rect_fill(cx + 2, fy - 1, cw - 4, 11, KGUI_COLOR_BLUE);
                }
                kguiq_draw_icon(cx + 4, fy, KGUI_ICON_FILE);
                uint8_t tc = (i == files_selected) ? KGUI_COLOR_WHITE : KGUI_COLOR_BLACK;
                kguiq_draw_string_clipped(cx + 18, fy + 1, kfs_name(i), tc, cx + cw - 4);
                fy += 11;
            }

            /* Bottom action buttons: Open, Del (second click confirms), scroll */
            int by = cy + ch - 14;
            kguiq_draw_rect_fill(cx, by, cw, 14, KGUI_3D_FACE);
            kguiq_draw_bevel(cx, by, cw, 14, 0);
            kguiq_draw_button(cx + 2, by + 1, 38, 12, "Open", 0, 0);
            kguiq_draw_button(cx + 42, by + 1, 46, 12, files_del_armed == files_selected ? "Sure?" : "Del", 0, 0);
            kguiq_draw_button(cx + cw - 32, by + 1, 14, 12, "^", 0, 0);
            kguiq_draw_button(cx + cw - 16, by + 1, 14, 12, "v", 0, 0);
            break;
        }

        case KGUID_APP_SETTINGS: {
            /* Grey background with controls */
            kguiq_draw_rect_fill(cx, cy, cw, ch, KGUI_3D_FACE);
            mode13_draw_string(cx + 4, cy + 4, "Wallpaper Image:", KGUI_COLOR_BLACK);
            kguiq_draw_input(cx + 4, cy + 16, cw - 8, 13, settings_wall_input,
                             kgui_strlen(settings_wall_input), win->active);

            kguiq_draw_button(cx + 4, cy + 33, 56, 12, "demo.bmp", 0, 0);
            kguiq_draw_button(cx + 64, cy + 33, 44, 12, "None", 0, 0);
            kguiq_draw_button(cx + 4, cy + 49, cw - 8, 13, "Apply Wallpaper", 0, 0);

            mode13_draw_string(cx + 4, cy + 66, "Current:", KGUI_COLOR_DARK_GREY);
            kguiq_draw_string_clipped(cx + 56, cy + 66, kguid_get_wallpaper(), KGUI_COLOR_BLUE, cx + cw - 2);
            break;
        }

        case KGUID_APP_SYSINFO: {
            /* Tech specs card */
            kguiq_draw_rect_fill(cx, cy, cw, ch, KGUI_COLOR_BLACK);
            mode13_draw_rect(cx, cy, cw, ch, KGUI_COLOR_CYAN);

            mode13_draw_string(cx + 4, cy + 3,  "KirillOS v0.2 GUI", KGUI_COLOR_YELLOW);
            mode13_draw_line(cx + 4, cy + 12, cx + cw - 5, cy + 12, KGUI_COLOR_DARK_GREY);

            mode13_draw_string(cx + 4, cy + 15, "CPU : x86 IA-32", KGUI_COLOR_LIGHT_GREEN);
            mode13_draw_string(cx + 4, cy + 24, "RAM : 128 MB", KGUI_COLOR_LIGHT_GREEN);
            mode13_draw_string(cx + 4, cy + 33, "VGA : Mode 13h (256c)", KGUI_COLOR_LIGHT_GREEN);
            mode13_draw_string(cx + 4, cy + 42, "Disk: ATA LBA28 KFS4", KGUI_COLOR_LIGHT_GREEN);
            mode13_draw_string(cx + 4, cy + 51, "Snd : AC'97 + PIT", KGUI_COLOR_LIGHT_GREEN);

            kguiq_draw_progressbar(cx + 4, cy + 63, cw - 8, 8, 85, KGUI_COLOR_BLUE);
            break;
        }

        case KGUID_APP_VIEWER: {
            /* Paint / Viewer placeholder with Open Kaint button */
            kguiq_draw_rect_fill(cx, cy, cw, ch, KGUI_COLOR_DARK_GREY);
            mode13_draw_string(cx + 4, cy + 8, "Kaint Image Studio", KGUI_COLOR_WHITE);
            kguiq_draw_button(cx + 10, cy + 24, cw - 20, 16, "Launch Kaint", 0, 0);
            kguiq_draw_button(cx + 10, cy + 44, cw - 20, 16, "Commander", 0, 0);
            break;
        }

        default:
            kguiq_draw_rect_fill(cx, cy, cw, ch, KGUI_COLOR_LIGHT_GREY);
            break;
    }
}

/* ================================================================ */
/*                       Main KGUI Event Loop                       */
/* ================================================================ */
static int running;
static int prev_btn_left;
static int dragging, drag_win_id, drag_dx, drag_dy;
static int selected_shortcut;
static int esc_armed;

static void kgui_redraw(void) {
    mouse_hide_cursor();

    /* 1. Desktop & Wallpaper */
    kguid_draw_desktop();

    /* 2. Desktop Shortcuts */
    kguid_draw_shortcuts(selected_shortcut);

    /* 3. Windows (in bottom-to-top z-order) */
    int z_count = kguid_get_zorder_count();
    for (int z = 0; z < z_count; z++) {
        int wid = kguid_get_zorder_window(z);
        kgui_window_t* w = kguid_get_window(wid);
        if (!w || w->minimized) continue;

        /* Draw 3D Window Frame & Titlebar */
        kguiq_draw_window_frame(w->x, w->y, w->w, w->h, w->title, w->active, 1, 1, 1);

        /* Draw Client Area */
        draw_window_client(w);
    }

    /* 4. Taskbar & Widgets */
    int focused_id = kguid_get_focused_window();
    kguid_draw_taskbar(mouse_btn_left && kguiq_point_in_rect(mouse_x, mouse_y, 2, KGUID_TASKBAR_Y, 54, 16), focused_id);

    /* 5. Start Menu if active */
    if (kguid_is_start_menu_open()) {
        int hovered_sm = kguid_start_menu_item_at(mouse_x, mouse_y);
        kguid_draw_start_menu(hovered_sm);
    }

    /* 6. Mouse Cursor */
    mouse_draw_cursor();
}

static void handle_click(void) {
    /* 1. Click on Start Menu popup */
    if (kguid_is_start_menu_open()) {
        int item = kguid_start_menu_item_at(mouse_x, mouse_y);
        if (item != -1) {
            kguid_set_start_menu(0);
            switch (item) {
                case 0: open_app(KGUID_APP_TERMINAL, 10, 16); break;
                case 1: open_app(KGUID_APP_FILES, 40, 30); break;
                case 2: open_app(KGUID_APP_NOTEPAD, 0, 0); break;
                case 3: open_app(KGUID_APP_VIEWER, 60, 40); break;
                case 4: open_app(KGUID_APP_SETTINGS, 45, 30); break;
                case 5: open_app(KGUID_APP_SYSINFO, 70, 45); break;
                case 6: running = 0; break; /* Exit to Shell */
                case 7: {
                    /* Reboot: wait for the 8042 input buffer, then pulse the reset line */
                    for (int i = 0; i < 100000 && (kgui_inb(0x64) & 0x02); i++) { }
                    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"(0x64));
                    break;
                }
            }
            return;
        } else if (!kguiq_point_in_rect(mouse_x, mouse_y, 2, KGUID_TASKBAR_Y, 54, KGUID_TASKBAR_H)) {
            kguid_set_start_menu(0);
        }
    }

    /* 2. Click on Taskbar */
    if (mouse_y >= KGUID_TASKBAR_Y) {
        if (kguiq_point_in_rect(mouse_x, mouse_y, 2, KGUID_TASKBAR_Y + 2, 54, 12)) {
            kguid_toggle_start_menu();
        } else {
            int clicked_wid = kguid_find_taskbar_window_at(mouse_x, mouse_y);
            kgui_window_t* w = kguid_get_window(clicked_wid);
            if (w) {
                if (w->minimized || !w->active) kguid_focus_window(w->id);
                else kguid_minimize_window(w->id);
            }
        }
        return;
    }

    /* 3. Click on Window Titlebar */
    int title_btn = 0;
    int win_id = kguid_find_titlebar_at(mouse_x, mouse_y, &title_btn);
    if (win_id != -1) {
        kguid_focus_window(win_id);
        if (title_btn == 3) {
            kgui_close_window(win_id);
        } else if (title_btn == 2) {
            kguid_maximize_window(win_id);
            note_doc_t* d = note_find(win_id);
            if (d) note_scroll_to_cursor(d);
        } else if (title_btn == 1) {
            kguid_minimize_window(win_id);
        } else {
            /* Start Dragging */
            kgui_window_t* w = kguid_get_window(win_id);
            if (w && !w->maximized) {
                dragging = 1;
                drag_win_id = win_id;
                drag_dx = mouse_x - w->x;
                drag_dy = mouse_y - w->y;
            }
        }
        return;
    }

    /* 4. Click inside Window Client Area */
    win_id = kguid_find_window_at(mouse_x, mouse_y);
    if (win_id != -1) {
        kguid_focus_window(win_id);
        kgui_window_t* w = kguid_get_window(win_id);
        int cx = w->x + 4;
        int cy = w->y + 18;
        int cw = w->w - 8;
        int ch = w->h - 22;

        if (w->app_type == KGUID_APP_SETTINGS) {
            if (kguiq_point_in_rect(mouse_x, mouse_y, cx + 4, cy + 33, 56, 12)) {
                kguid_set_wallpaper("demo.bmp");
            } else if (kguiq_point_in_rect(mouse_x, mouse_y, cx + 64, cy + 33, 44, 12)) {
                kguid_set_wallpaper("-");
            } else if (kguiq_point_in_rect(mouse_x, mouse_y, cx + 4, cy + 49, cw - 8, 13)) {
                kguid_set_wallpaper(settings_wall_input);
            }
        } else if (w->app_type == KGUID_APP_VIEWER) {
            if (kguiq_point_in_rect(mouse_x, mouse_y, cx + 10, cy + 24, cw - 20, 16)) {
                /* Launch Kaint full-screen and resume */
                mouse_hide_cursor();
                kaint_start("demo.bmp");
                restore_graphics();
            } else if (kguiq_point_in_rect(mouse_x, mouse_y, cx + 10, cy + 44, cw - 20, 16)) {
                mouse_hide_cursor();
                kcommander_start();
                restore_graphics();
            }
        } else if (w->app_type == KGUID_APP_FILES) {
            int by = cy + ch - 14;
            files_clamp(w);
            if (mouse_y >= by) {
                if (kguiq_point_in_rect(mouse_x, mouse_y, cx + 2, by + 1, 38, 12)) {
                    files_del_armed = -1;
                    files_open_selected();
                } else if (kguiq_point_in_rect(mouse_x, mouse_y, cx + 42, by + 1, 46, 12)) {
                    files_delete_selected();
                } else if (kguiq_point_in_rect(mouse_x, mouse_y, cx + cw - 32, by + 1, 14, 12)) {
                    if (files_top > 0) files_top--;
                    if (files_selected >= files_top + files_rows(w)) files_selected = files_top + files_rows(w) - 1;
                } else if (kguiq_point_in_rect(mouse_x, mouse_y, cx + cw - 16, by + 1, 14, 12)) {
                    if (files_top + files_rows(w) < kfs_file_count()) files_top++;
                    if (files_selected < files_top) files_selected = files_top;
                }
            } else {
                /* Only rows that are actually drawn are clickable */
                int row = (mouse_y - (cy + 2)) / 11;
                int idx = files_top + row;
                if (mouse_y >= cy + 2 && row < files_rows(w) && idx < kfs_file_count()) {
                    if (idx != files_selected) files_del_armed = -1;
                    files_selected = idx;
                }
            }
        } else if (w->app_type == KGUID_APP_NOTEPAD) {
            note_doc_t* d = note_find(win_id);
            if (d) note_click(w, d, mouse_x, mouse_y, cx, cy);
        }
        return;
    }

    /* 5. Click on Desktop Shortcuts */
    int sc = kguid_find_shortcut_at(mouse_x, mouse_y);
    if (sc != -1) {
        if (selected_shortcut == sc) {
            /* Second click on a selected shortcut opens it */
            kgui_shortcut_t* item = kguid_get_shortcut(sc);
            if (item) open_app(item->app_type, 30 + sc * 15, 25 + sc * 10);
            selected_shortcut = -1;
        } else {
            selected_shortcut = sc;
        }
    } else {
        selected_shortcut = -1;
    }
}

static void handle_key(char key) {
    int focused_id = kguid_get_focused_window();
    kgui_window_t* focused_win = kguid_get_window(focused_id);
    if (!focused_win) return;

    if (focused_win->app_type == KGUID_APP_TERMINAL) {
        if (key == '\n') {
            term_execute();
        } else if (key == '\b') {
            if (term_input_len > 0) {
                term_input[--term_input_len] = 0;
            }
        } else if (key >= 32 && key < 127) {
            if (term_input_len < TERM_LINE_LEN - 1) {
                term_input[term_input_len++] = key;
                term_input[term_input_len] = 0;
            }
        }
    } else if (focused_win->app_type == KGUID_APP_NOTEPAD) {
        note_doc_t* d = note_find(focused_id);
        if (d) note_key(d, key);
    } else if (focused_win->app_type == KGUID_APP_FILES) {
        int count = kfs_file_count();
        if (key == KEY_UP && files_selected > 0) { files_selected--; files_del_armed = -1; }
        else if (key == KEY_DOWN && files_selected < count - 1) { files_selected++; files_del_armed = -1; }
        else if (key == '\n') files_open_selected();
        else if (key == KEY_DEL) files_delete_selected();
        files_clamp(focused_win);
    } else if (focused_win->app_type == KGUID_APP_SETTINGS) {
        int slen = kgui_strlen(settings_wall_input);
        if (key == '\b') {
            if (slen > 0) settings_wall_input[slen - 1] = 0;
        } else if (key == '\n') {
            kguid_set_wallpaper(settings_wall_input);
        } else if (key >= 32 && key < 127 && slen < KFS_NAME_MAX) {
            settings_wall_input[slen] = key;
            settings_wall_input[slen + 1] = 0;
        }
    }
}

/* ESC leaves KGUI, but cancels a notepad name prompt and warns once about unsaved notes */
static int handle_escape(void) {
    note_doc_t* focused = note_find(kguid_get_focused_window());
    if (focused && focused->naming) {
        focused->naming = 0;
        note_status(focused, "Cancelled");
        return 0;
    }
    for (int i = 0; i < NOTE_DOCS; i++) {
        if (notes[i].win_id && notes[i].dirty && !esc_armed) {
            esc_armed = 1;
            kguid_focus_window(notes[i].win_id);
            note_status(&notes[i], "Unsaved! Esc again=quit");
            return 0;
        }
    }
    return 1;
}

void kgui_start(void) {
    /* 1. Enter Mode 13h (320x200 256 colors) */
    mode13_enter();
    mouse_init();
    mouse_set_position(160, 100);

    /* 2. Init Desktop & Window Manager */
    kguid_init();
    for (int i = 0; i < NOTE_DOCS; i++) notes[i].win_id = 0;
    term_line_count = 0;
    term_add_line("KirillOS Terminal v1.0");
    term_add_line("Real Kernel Shell Ready");

    /* Create initial desktop windows */
    open_app(KGUID_APP_TERMINAL, 10, 16);

    kgui_exit_flag = 0;
    running = 1;
    prev_btn_left = 0;
    dragging = 0;
    drag_win_id = -1;
    selected_shortcut = -1;
    esc_armed = 0;
    int need_redraw = 1;
    uint32_t idle_ticks = 0;

    while (running && !kgui_exit_flag) {
        /* Poll hardware input */
        int mouse_moved = mouse_poll();
        char key = keyboard_poll();

        if (key == KEY_ESC) {
            if (handle_escape()) break;
            key = 0;
            need_redraw = 1;
        } else if (key) {
            esc_armed = 0;
        }

        /* Edge detection happens once per pass, before any handler can return early */
        int btn_clicked = (mouse_btn_left && !prev_btn_left);
        int btn_released = (!mouse_btn_left && prev_btn_left);
        prev_btn_left = mouse_btn_left;

        if (mouse_moved || btn_clicked || btn_released || dragging || (key != 0)) {
            need_redraw = 1;
        }
        idle_ticks++;
        if (idle_ticks >= 200000) {
            idle_ticks = 0;
            need_redraw = 1; /* periodic clock/tray refresh */
        }

        /* Handle Window Dragging */
        if (dragging && drag_win_id != -1) {
            if (mouse_btn_left) {
                kgui_window_t* win = kguid_get_window(drag_win_id);
                if (win && !win->maximized) {
                    win->x = mouse_x - drag_dx;
                    win->y = mouse_y - drag_dy;
                    if (win->x < -win->w + 30) win->x = -win->w + 30;
                    if (win->x > MODE13_WIDTH - 30) win->x = MODE13_WIDTH - 30;
                    if (win->y < 0) win->y = 0;
                    if (win->y > KGUID_DESKTOP_H - 16) win->y = KGUID_DESKTOP_H - 16;
                }
            } else {
                dragging = 0;
                drag_win_id = -1;
            }
        }

        if (btn_clicked) handle_click();

        if (btn_released) {
            dragging = 0;
            drag_win_id = -1;
        }

        if (key != 0) handle_key(key);

        /* ================= Redraw Screen Frame ================= */
        if (need_redraw && running && !kgui_exit_flag) {
            need_redraw = 0;
            kgui_redraw();
        } else {
            for (volatile int i = 0; i < 5000; i++) __asm__ volatile("nop");
        }
    }

    /* Clean exit to text console */
    mouse_hide_cursor();
    mouse_disable();
    mode13_exit();
}
