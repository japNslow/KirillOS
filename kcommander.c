#include "kcommander.h"
#include "mode13.h"
#include "mouse.h"
#include "keyboard.h"
#include "kirillfs.h"
#include "vga.h"
#include "kano.h"
#include "kdhe.h"
#include "kmidi.h"
#include "khex.h"
#include "khexd.h"
#include "kaint.h"
#include "sound.h"
#include <stdint.h>
#include <stddef.h>

/* ================================================================ */
/*                     Geometry & Color Constants                   */
/* ================================================================ */
#define COLOR_BLACK         0
#define COLOR_BLUE          1   /* Norton Commander Deep Blue */
#define COLOR_GREEN         2
#define COLOR_CYAN          3
#define COLOR_RED           4
#define COLOR_MAGENTA       5
#define COLOR_BROWN         6
#define COLOR_LIGHT_GREY    7
#define COLOR_DARK_GREY     8
#define COLOR_LIGHT_BLUE    9
#define COLOR_LIGHT_GREEN   10
#define COLOR_LIGHT_CYAN    11
#define COLOR_LIGHT_RED     12
#define COLOR_LIGHT_MAGENTA 13
#define COLOR_YELLOW        14
#define COLOR_WHITE         15

#define LEFT_PANEL_X        3
#define LEFT_PANEL_Y        14
#define LEFT_PANEL_W        198
#define LEFT_PANEL_H        142

#define RIGHT_PANEL_X       204
#define RIGHT_PANEL_Y       14
#define RIGHT_PANEL_W       113
#define RIGHT_PANEL_H       142

#define FILE_LIST_Y         27
#define FILE_ROW_H          10
#define VISIBLE_FILES       11

#define SCROLL_X            189
#define SCROLL_Y            27
#define SCROLL_W            10
#define SCROLL_H            126

#define BTN_Y               172
#define BTN_H               24
#define BTN_W               51

/* ================================================================ */
/*                       CMOS RTC Time Helper                       */
/* ================================================================ */
static inline void outb_cmos(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb_cmos(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline uint8_t cmos_read_byte(uint8_t reg) {
    outb_cmos(0x70, reg | 0x80);
    return inb_cmos(0x71);
}

static uint8_t bcd_to_dec(uint8_t val) {
    return (uint8_t)(((val >> 4) * 10) + (val & 0x0F));
}

static void read_rtc_time(uint8_t* hour, uint8_t* min, uint8_t* sec) {
    uint32_t timeout = 1000;
    while ((cmos_read_byte(0x0A) & 0x80) && --timeout);

    *sec  = cmos_read_byte(0x00);
    *min  = cmos_read_byte(0x02);
    *hour = cmos_read_byte(0x04);

    uint8_t reg_b = cmos_read_byte(0x0B);
    if (!(reg_b & 0x04)) {
        *sec  = bcd_to_dec(*sec);
        *min  = bcd_to_dec(*min);
        *hour = (uint8_t)(bcd_to_dec((uint8_t)(*hour & 0x7F)) | (*hour & 0x80));
    }
    if (!(reg_b & 0x02) && (*hour & 0x80)) {
        *hour = (uint8_t)(((*hour & 0x7F) + 12) % 24);
    }
}

/* ================================================================ */
/*                         File Types & Info                        */
/* ================================================================ */
typedef enum {
    FTYPE_EXE = 0,    /* .bin */
    FTYPE_KHEX,       /* .khex */
    FTYPE_MIDI,       /* .kmidi */
    FTYPE_CODE,       /* .k */
    FTYPE_OBJ,        /* .ko */
    FTYPE_BMP,        /* .bmp */
    FTYPE_TXT         /* .txt and others */
} file_type_t;

static int str_len(const char* s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static int str_ends_with(const char* str, const char* suffix) {
    int slen = str_len(str);
    int suflen = str_len(suffix);
    if (slen < suflen) return 0;
    for (int i = 0; i < suflen; i++) {
        char a = str[slen - suflen + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return 0;
    }
    return 1;
}

static file_type_t get_file_type(const char* name) {
    if (str_ends_with(name, ".bin"))  return FTYPE_EXE;
    if (str_ends_with(name, ".khex")) return FTYPE_KHEX;
    if (str_ends_with(name, ".kmidi"))return FTYPE_MIDI;
    if (str_ends_with(name, ".k"))    return FTYPE_CODE;
    if (str_ends_with(name, ".ko"))   return FTYPE_OBJ;
    if (str_ends_with(name, ".bmp"))  return FTYPE_BMP;
    return FTYPE_TXT;
}

static const char* get_type_badge(file_type_t t) {
    switch (t) {
        case FTYPE_EXE:  return "[BIN]";
        case FTYPE_KHEX: return "[HEX]";
        case FTYPE_MIDI: return "[MID]";
        case FTYPE_CODE: return "[K  ]";
        case FTYPE_OBJ:  return "[OBJ]";
        case FTYPE_BMP:  return "[BMP]";
        default:         return "[TXT]";
    }
}

static uint8_t get_type_color(file_type_t t) {
    switch (t) {
        case FTYPE_EXE:  return COLOR_LIGHT_GREEN;
        case FTYPE_KHEX: return COLOR_LIGHT_CYAN;
        case FTYPE_MIDI: return COLOR_LIGHT_MAGENTA;
        case FTYPE_CODE: return COLOR_YELLOW;
        case FTYPE_OBJ:  return COLOR_LIGHT_GREY;
        case FTYPE_BMP:  return COLOR_LIGHT_RED;
        default:         return COLOR_WHITE;
    }
}

static void format_size_str(int bytes, char* out, int max_len) {
    if (max_len < 6) return;
    if (bytes < 1024) {
        char tmp[10]; int tl = 0; int val = bytes;
        if (val == 0) tmp[tl++] = '0';
        while (val > 0) { tmp[tl++] = (char)('0' + (val % 10)); val /= 10; }
        int p = 0;
        while (p < 4 - tl) out[p++] = ' ';
        while (tl > 0) out[p++] = tmp[--tl];
        out[p++] = 'B';
        out[p] = 0;
    } else {
        int k = bytes / 1024;
        int rem = (bytes % 1024) / 100;
        char tmp[10]; int tl = 0; int val = k;
        if (val == 0) tmp[tl++] = '0';
        while (val > 0) { tmp[tl++] = (char)('0' + (val % 10)); val /= 10; }
        int p = 0;
        while (tl > 0) out[p++] = tmp[--tl];
        out[p++] = '.';
        out[p++] = (char)('0' + rem);
        out[p++] = 'K';
        out[p] = 0;
    }
}

static void int_to_str(int val, char* out) {
    if (val == 0) { out[0] = '0'; out[1] = 0; return; }
    char tmp[12]; int tl = 0;
    if (val < 0) { out[0] = '-'; out++; val = -val; }
    while (val > 0) { tmp[tl++] = (char)('0' + (val % 10)); val /= 10; }
    int p = 0;
    while (tl > 0) out[p++] = tmp[--tl];
    out[p] = 0;
}

/* ================================================================ */
/*                       GUI Drawing Helpers                        */
/* ================================================================ */

/* Draws a classic Norton Commander double frame */
static void draw_nc_frame(int x, int y, int w, int h, const char* title, uint8_t border_color, uint8_t title_color) {
    /* Outer border */
    mode13_draw_rect(x, y, w, h, border_color);
    /* Inner border */
    mode13_draw_rect(x + 2, y + 2, w - 4, h - 4, border_color);

    /* Draw Title Header */
    if (title && title[0]) {
        int tlen = str_len(title);
        int tx = x + (w - tlen * 8) / 2;
        if (tx < x + 6) tx = x + 6;
        /* Clear background under title */
        mode13_fill_rect(tx - 3, y, tlen * 8 + 6, 8, COLOR_BLUE);
        mode13_draw_string(tx, y, title, title_color);
    }
}

/* 3D pushable button */
static void draw_3d_button(int x, int y, int w, int h, int num, const char* label, int pressed) {
    uint8_t bg = COLOR_LIGHT_GREY;
    uint8_t light = pressed ? COLOR_BLACK : COLOR_WHITE;
    uint8_t dark  = pressed ? COLOR_WHITE : COLOR_BLACK;

    mode13_fill_rect(x, y, w, h, bg);

    /* Top & Left edges */
    mode13_draw_line(x, y, x + w - 1, y, light);
    mode13_draw_line(x, y + 1, x, y + h - 1, light);

    /* Bottom & Right edges */
    mode13_draw_line(x, y + h - 1, x + w - 1, y + h - 1, dark);
    mode13_draw_line(x + w - 1, y, x + w - 1, y + h - 1, dark);

    /* Text */
    int off = pressed ? 1 : 0;
    char num_str[4];
    num_str[0] = (char)('0' + num);
    num_str[1] = ':';
    num_str[2] = 0;

    int text_x = x + 3 + off;
    int text_y = y + (h - 8) / 2 + off;

    mode13_draw_string(text_x, text_y, num_str, COLOR_YELLOW);
    mode13_draw_string(text_x + 16, text_y, label, COLOR_BLACK);
}

/* ================================================================ */
/*                       Commander State Machine                    */
/* ================================================================ */
static int selected_index = 0;
static int scroll_offset  = 0;
static int hover_index     = -1;
static int active_button   = -1;  /* 0..5 or -1 */
static int button_pressed  = 0;
static int show_help_modal = 0;
static uint8_t last_rtc_sec = 0xFF;

static uint32_t last_click_tick = 0;
static int      last_click_file = -1;

/* Redraws the complete Norton Commander screen */
static void draw_header_time(void) {
    uint8_t h, m, s;
    read_rtc_time(&h, &m, &s);
    last_rtc_sec = s;

    char time_str[12];
    time_str[0] = (char)('0' + (h / 10));
    time_str[1] = (char)('0' + (h % 10));
    time_str[2] = ':';
    time_str[3] = (char)('0' + (m / 10));
    time_str[4] = (char)('0' + (m % 10));
    time_str[5] = ':';
    time_str[6] = (char)('0' + (s / 10));
    time_str[7] = (char)('0' + (s % 10));
    time_str[8] = 0;

    /* Draw time in top-right header */
    mode13_fill_rect(250, 1, 66, 10, COLOR_CYAN);
    mode13_draw_string(252, 2, time_str, COLOR_YELLOW);
}

static void draw_preview_lines(const char* data, int size, int x, int y, int max_lines) {
    int cur_line = 0;
    int pos = 0;

    while (pos < size && cur_line < max_lines) {
        char line_buf[14];
        int lidx = 0;
        while (pos < size && data[pos] != '\n' && data[pos] != '\r' && lidx < 13) {
            char c = data[pos++];
            if (c < 32 || c > 126) c = '.';
            line_buf[lidx++] = c;
        }
        while (pos < size && (data[pos] == '\n' || data[pos] == '\r')) pos++;
        line_buf[lidx] = 0;

        if (lidx > 0) {
            mode13_draw_string(x, y + cur_line * 9, line_buf, COLOR_LIGHT_CYAN);
        }
        cur_line++;
    }
}

static void draw_info_card(int file_idx) {
    int total_files = kfs_file_count();

    /* Clear right panel content */
    mode13_fill_rect(RIGHT_PANEL_X + 3, RIGHT_PANEL_Y + 12, RIGHT_PANEL_W - 6, RIGHT_PANEL_H - 15, COLOR_BLUE);

    if (file_idx < 0 || file_idx >= total_files) {
        mode13_draw_string(RIGHT_PANEL_X + 10, RIGHT_PANEL_Y + 20, "No files in", COLOR_LIGHT_GREY);
        mode13_draw_string(RIGHT_PANEL_X + 10, RIGHT_PANEL_Y + 30, "KirillFS", COLOR_LIGHT_GREY);
        return;
    }

    const char* fname = kfs_name(file_idx);
    int fsize = kfs_size(file_idx);
    file_type_t ftype = get_file_type(fname);

    /* File Name */
    mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 14, fname, COLOR_YELLOW);
    mode13_draw_line(RIGHT_PANEL_X + 4, RIGHT_PANEL_Y + 24, RIGHT_PANEL_X + RIGHT_PANEL_W - 5, RIGHT_PANEL_Y + 24, COLOR_LIGHT_CYAN);

    /* Size */
    mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 28, "Size:", COLOR_WHITE);
    char sz_str[16];
    int_to_str(fsize, sz_str);
    int slen = str_len(sz_str);
    sz_str[slen] = 'B'; sz_str[slen + 1] = 0;
    mode13_draw_string(RIGHT_PANEL_X + 48, RIGHT_PANEL_Y + 28, sz_str, COLOR_LIGHT_GREEN);

    /* Type */
    mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 38, "Type:", COLOR_WHITE);
    const char* tname = "Text";
    if (ftype == FTYPE_EXE)  tname = "NativeApp";
    if (ftype == FTYPE_KHEX) tname = "KHEX Exec";
    if (ftype == FTYPE_MIDI) tname = "MIDI Song";
    if (ftype == FTYPE_CODE) tname = "K-Source";
    if (ftype == FTYPE_OBJ)  tname = "Object";
    mode13_draw_string(RIGHT_PANEL_X + 48, RIGHT_PANEL_Y + 38, tname, get_type_color(ftype));

    /* Description / Hints */
    mode13_draw_line(RIGHT_PANEL_X + 4, RIGHT_PANEL_Y + 49, RIGHT_PANEL_X + RIGHT_PANEL_W - 5, RIGHT_PANEL_Y + 49, COLOR_DARK_GREY);

    if (ftype == FTYPE_EXE) {
        mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 54, "x86 Native", COLOR_LIGHT_CYAN);
        mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 63, "Load:0x50000", COLOR_LIGHT_GREY);
        mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 72, "Action: Run", COLOR_YELLOW);
    } else if (ftype == FTYPE_KHEX) {
        mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 54, "Bytecode VM", COLOR_LIGHT_CYAN);
        mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 63, "Format: KHEX", COLOR_LIGHT_GREY);
        mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 72, "Action: Run", COLOR_YELLOW);
    } else if (ftype == FTYPE_MIDI) {
        mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 54, "PC Speaker", COLOR_LIGHT_MAGENTA);
        mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 63, "Notes/Tempo", COLOR_LIGHT_GREY);
        mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 72, "Action: Play", COLOR_YELLOW);
    } else if (ftype == FTYPE_CODE) {
        mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 54, "K-Language", COLOR_YELLOW);
        mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 63, "Comp: khexd", COLOR_LIGHT_GREY);
        mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 72, "Action: Edit", COLOR_LIGHT_CYAN);
    } else {
        mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 54, "Document", COLOR_WHITE);
        mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 63, "Editor: Kano", COLOR_LIGHT_GREY);
        mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 72, "Action: Edit", COLOR_LIGHT_CYAN);
    }

    /* Content Preview */
    mode13_draw_line(RIGHT_PANEL_X + 4, RIGHT_PANEL_Y + 84, RIGHT_PANEL_X + RIGHT_PANEL_W - 5, RIGHT_PANEL_Y + 84, COLOR_DARK_GREY);
    mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 87, "[ Preview ]", COLOR_YELLOW);

    const char* data;
    int rsize = 0;
    if (kfs_read(fname, &data, &rsize) == KFS_OK && rsize > 0) {
        draw_preview_lines(data, rsize, RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 98, 4);
    } else {
        mode13_draw_string(RIGHT_PANEL_X + 6, RIGHT_PANEL_Y + 100, "(empty file)", COLOR_DARK_GREY);
    }
}

static void draw_files_list(void) {
    int total_files = kfs_file_count();

    /* Clear file list area inside left panel */
    mode13_fill_rect(LEFT_PANEL_X + 3, FILE_LIST_Y, LEFT_PANEL_W - 16, LEFT_PANEL_H - (FILE_LIST_Y - LEFT_PANEL_Y) - 3, COLOR_BLUE);

    for (int i = 0; i < VISIBLE_FILES; i++) {
        int idx = scroll_offset + i;
        int y = FILE_LIST_Y + i * FILE_ROW_H;

        if (idx >= total_files) {
            /* Empty row */
            continue;
        }

        const char* name = kfs_name(idx);
        int size = kfs_size(idx);
        file_type_t ftype = get_file_type(name);

        int is_selected = (idx == selected_index);
        int is_hovered  = (idx == hover_index && !is_selected);

        /* Row background */
        if (is_selected) {
            mode13_fill_rect(LEFT_PANEL_X + 4, y, LEFT_PANEL_W - 18, FILE_ROW_H - 1, COLOR_LIGHT_CYAN);
        } else if (is_hovered) {
            mode13_fill_rect(LEFT_PANEL_X + 4, y, LEFT_PANEL_W - 18, FILE_ROW_H - 1, COLOR_LIGHT_BLUE);
        }

        /* Type badge */
        uint8_t badge_col = is_selected ? COLOR_BLACK : get_type_color(ftype);
        mode13_draw_string(LEFT_PANEL_X + 5, y + 1, get_type_badge(ftype), badge_col);

        /* File Name (padded up to 12 chars) */
        char name_buf[14];
        int nl = 0;
        while (name[nl] && nl < 11) { name_buf[nl] = name[nl]; nl++; }
        while (nl < 11) name_buf[nl++] = ' ';
        name_buf[nl] = 0;

        uint8_t name_col = is_selected ? COLOR_BLACK : COLOR_WHITE;
        mode13_draw_string(LEFT_PANEL_X + 48, y + 1, name_buf, name_col);

        /* Size */
        char sz_buf[8];
        format_size_str(size, sz_buf, 8);
        uint8_t sz_col = is_selected ? COLOR_BLACK : COLOR_YELLOW;
        mode13_draw_string(LEFT_PANEL_X + 140, y + 1, sz_buf, sz_col);
    }

    /* Draw Scrollbar */
    mode13_fill_rect(SCROLL_X, SCROLL_Y, SCROLL_W, SCROLL_H, COLOR_DARK_GREY);

    /* Arrow buttons */
    mode13_fill_rect(SCROLL_X, SCROLL_Y, SCROLL_W, 9, COLOR_LIGHT_GREY);
    mode13_draw_char(SCROLL_X + 1, SCROLL_Y + 1, '^', COLOR_BLACK);

    mode13_fill_rect(SCROLL_X, SCROLL_Y + SCROLL_H - 9, SCROLL_W, 9, COLOR_LIGHT_GREY);
    mode13_draw_char(SCROLL_X + 1, SCROLL_Y + SCROLL_H - 8, 'v', COLOR_BLACK);

    /* Slider / Thumb */
    int track_y = SCROLL_Y + 10;
    int track_h = SCROLL_H - 20;

    int thumb_h = track_h;
    int thumb_y = track_y;

    if (total_files > VISIBLE_FILES) {
        thumb_h = (track_h * VISIBLE_FILES) / total_files;
        if (thumb_h < 8) thumb_h = 8;
        int max_scroll = total_files - VISIBLE_FILES;
        thumb_y = track_y + (track_h - thumb_h) * scroll_offset / max_scroll;
    }

    mode13_fill_rect(SCROLL_X + 1, thumb_y, SCROLL_W - 2, thumb_h, COLOR_WHITE);
}

static void draw_bottom_buttons(void) {
    static const char* const labels[6] = { "Help", "Edit", "Hex", "Play", "Run", "Quit" };
    for (int i = 0; i < 6; i++) {
        int x = 3 + i * (BTN_W + 2);
        int pressed = (button_pressed && active_button == i);
        draw_3d_button(x, BTN_Y, BTN_W, BTN_H, i + 1, labels[i], pressed);
    }
}

static void draw_full_gui(void) {
    /* 1. Global Background */
    mode13_clear(COLOR_BLUE);

    /* 2. Top Header Bar */
    mode13_fill_rect(0, 0, MODE13_WIDTH, 12, COLOR_CYAN);
    mode13_draw_string(4, 2, "Kirill Commander v1.0 [Mode 13h]", COLOR_WHITE);
    draw_header_time();

    /* 3. Left File Panel (Norton Commander Blue) */
    draw_nc_frame(LEFT_PANEL_X, LEFT_PANEL_Y, LEFT_PANEL_W, LEFT_PANEL_H, "[ KirillFS: / ]", COLOR_WHITE, COLOR_YELLOW);

    /* Left Panel Columns Header */
    mode13_draw_string(LEFT_PANEL_X + 5, LEFT_PANEL_Y + 4, "Type Name        Size", COLOR_YELLOW);
    mode13_draw_line(LEFT_PANEL_X + 3, FILE_LIST_Y - 2, LEFT_PANEL_X + LEFT_PANEL_W - 4, FILE_LIST_Y - 2, COLOR_LIGHT_CYAN);

    /* 4. Right Info Card Panel */
    draw_nc_frame(RIGHT_PANEL_X, RIGHT_PANEL_Y, RIGHT_PANEL_W, RIGHT_PANEL_H, "[ File Info ]", COLOR_LIGHT_CYAN, COLOR_YELLOW);

    /* 5. Draw Content */
    draw_files_list();
    draw_info_card(selected_index);

    /* 6. Prompt / Hint Bar */
    mode13_fill_rect(3, 158, 314, 11, COLOR_BLACK);
    mode13_draw_rect(3, 158, 314, 11, COLOR_DARK_GREY);
    mode13_draw_string(6, 160, "Click/Arrows:Select | DblClick/Enter:Run | 1..6:Cmds", COLOR_LIGHT_CYAN);

    /* 7. Bottom Function Buttons (F1..F6) */
    draw_bottom_buttons();
}

/* ================================================================ */
/*                       Modal Help Dialog                          */
/* ================================================================ */
static void draw_help_dialog(void) {
    int w = 264;
    int h = 154;
    int x = (MODE13_WIDTH - w) / 2;
    int y = 20;

    /* Shadow */
    mode13_fill_rect(x + 4, y + 4, w, h, COLOR_BLACK);

    /* Window Body */
    mode13_fill_rect(x, y, w, h, COLOR_BLUE);
    draw_nc_frame(x, y, w, h, "[ Kirill Commander Help ]", COLOR_WHITE, COLOR_YELLOW);

    /* Help Content */
    mode13_draw_string(x + 10, y + 15, "== Mouse Controls ==", COLOR_LIGHT_GREEN);
    mode13_draw_string(x + 10, y + 26, "* Single Click: select file or button", COLOR_WHITE);
    mode13_draw_string(x + 10, y + 36, "* Double Click: run or open file", COLOR_WHITE);
    mode13_draw_string(x + 10, y + 46, "* Scrollbar   : click ^ / v to scroll", COLOR_WHITE);

    mode13_draw_string(x + 10, y + 60, "== Keyboard Shortcuts ==", COLOR_LIGHT_GREEN);
    mode13_draw_string(x + 10, y + 71, "* Up / Down   : navigate file list", COLOR_WHITE);
    mode13_draw_string(x + 10, y + 81, "* Enter       : run / open file", COLOR_WHITE);
    mode13_draw_string(x + 10, y + 91, "* 1:Help  2:Edit (kano)  3:Hex (kdhe)", COLOR_YELLOW);
    mode13_draw_string(x + 10, y + 101, "* 4:Play (kmidi)  5:Run  6/Esc:Quit", COLOR_YELLOW);

    /* OK Button */
    int bw = 100;
    int bh = 18;
    int bx = x + (w - bw) / 2;
    int by = y + h - 26;
    draw_3d_button(bx, by, bw, bh, 1, "  Close", 0);
}

/* ================================================================ */
/*                       Actions Execution                          */
/* ================================================================ */
static void execute_action(int action_id, int file_idx) {
    int total_files = kfs_file_count();
    const char* filename = (file_idx >= 0 && file_idx < total_files) ? kfs_name(file_idx) : 0;

    switch (action_id) {
        case 0: /* 1:Help */
            show_help_modal = 1;
            mouse_hide_cursor();
            draw_help_dialog();
            mouse_draw_cursor();
            break;

        case 1: /* 2:Edit (Kano) */
            if (!filename) return;
            sound_soft_note(880, 40);
            mouse_hide_cursor();
            mouse_disable();
            mode13_exit();
            kano_open(filename);
            mode13_enter();
            mouse_init();
            draw_full_gui();
            mouse_draw_cursor();
            break;

        case 2: /* 3:Hex (Kdhe) */
            if (!filename) return;
            sound_soft_note(880, 40);
            mouse_hide_cursor();
            mouse_disable();
            mode13_exit();
            kdhe_open(filename);
            mode13_enter();
            mouse_init();
            draw_full_gui();
            mouse_draw_cursor();
            break;

        case 3: /* 4:Play (Kmidi) */
            if (!filename) return;
            sound_soft_note(659, 50);
            mouse_hide_cursor();
            mouse_disable();
            mode13_exit();
            if (str_ends_with(filename, ".kmidi")) {
                kmidi_open(filename);
            } else {
                vga_clear();
                vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
                vga_write("kcommander: '");
                vga_write(filename);
                vga_write("' is not a .kmidi music file!\n");
                vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
                vga_write("\nPress any key to return...");
                keyboard_getchar();
            }
            mode13_enter();
            mouse_init();
            draw_full_gui();
            mouse_draw_cursor();
            break;

        case 4: /* 5:Run */
            if (!filename) return;
            sound_soft_note(784, 50);
            mouse_hide_cursor();
            mouse_disable();
            mode13_exit();

            file_type_t ft = get_file_type(filename);
            if (ft == FTYPE_EXE || ft == FTYPE_KHEX) {
                vga_clear();
                khex_run_file(filename);
                vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
                vga_write("\n[Process terminated. Press any key to return to Commander...]");
                vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
                keyboard_getchar();
            } else if (ft == FTYPE_CODE) {
                vga_clear();
                vga_write("Compiling and executing '");
                vga_write(filename);
                vga_write("'...\n\n");
                khexd_build(filename, "temp.khex");
                khex_run_file("temp.khex");
                vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
                vga_write("\n[Process terminated. Press any key to return to Commander...]");
                vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
                keyboard_getchar();
            } else if (ft == FTYPE_MIDI) {
                kmidi_play_file(filename);
                vga_write("\nPress any key to return to Commander...");
                keyboard_getchar();
            } else if (ft == FTYPE_BMP) {
                kaint_start(filename);
            } else {
                kano_open(filename);
            }

            mode13_enter();
            mouse_init();
            draw_full_gui();
            mouse_draw_cursor();
            break;

        case 5: /* 6:Quit */
            /* handled in main loop */
            break;
    }
}

/* ================================================================ */
/*                       Main Interactive Loop                      */
/* ================================================================ */
void kcommander_start(void) {
    /* 1. Enter VGA Mode 13h (320x200 256 colors) */
    mode13_enter();

    /* 2. Initialize PS/2 Mouse & Keyboard */
    mouse_init();
    keyboard_init();

    /* 3. Center cursor */
    mouse_set_position(MODE13_WIDTH / 2, MODE13_HEIGHT / 2);

    selected_index = 0;
    scroll_offset  = 0;
    hover_index    = -1;
    active_button  = -1;
    button_pressed = 0;
    show_help_modal = 0;
    last_click_file = -1;

    /* 4. Initial Screen Drawing */
    draw_full_gui();
    mouse_draw_cursor();

    /* Chime sound for commander start */
    sound_soft_note(440, 50);
    sound_soft_note(880, 80);

    int prev_mouse_btn = 0;
    int loop_counter = 0;

    for (;;) {
        loop_counter++;

        /* -------------------------------------------------------- */
        /* A. Poll Mouse & Update Hover State                       */
        /* -------------------------------------------------------- */
        mouse_poll();

        int total_files = kfs_file_count();
        int old_hover = hover_index;
        int current_hover = -1;

        if (!show_help_modal) {
            /* Check if mouse is hovering file rows */
            if (mouse_x >= LEFT_PANEL_X + 4 && mouse_x < LEFT_PANEL_X + LEFT_PANEL_W - 16 &&
                mouse_y >= FILE_LIST_Y && mouse_y < FILE_LIST_Y + VISIBLE_FILES * FILE_ROW_H) {
                int row = (mouse_y - FILE_LIST_Y) / FILE_ROW_H;
                if (scroll_offset + row < total_files) {
                    current_hover = scroll_offset + row;
                }
            }
        }

        if (current_hover != old_hover) {
            hover_index = current_hover;
            mouse_hide_cursor();
            draw_files_list();
            mouse_draw_cursor();
        }

        /* -------------------------------------------------------- */
        /* B. Update RTC Clock in Header Bar                        */
        /* -------------------------------------------------------- */
        if ((loop_counter % 64) == 0) {
            uint8_t h, m, s;
            read_rtc_time(&h, &m, &s);
            if (s != last_rtc_sec) {
                mouse_hide_cursor();
                draw_header_time();
                mouse_draw_cursor();
            }
        }

        /* -------------------------------------------------------- */
        /* C. Mouse Clicks & Drag Handling                          */
        /* -------------------------------------------------------- */
        if (mouse_btn_left && !prev_mouse_btn) {
            /* Left Button Down */
            if (show_help_modal) {
                /* Click anywhere closes help modal */
                show_help_modal = 0;
                sound_soft_note(600, 20);
                mouse_hide_cursor();
                draw_full_gui();
                mouse_draw_cursor();
            } else {
                /* 1. Check Function Buttons Click */
                if (mouse_y >= BTN_Y && mouse_y < BTN_Y + BTN_H) {
                    for (int i = 0; i < 6; i++) {
                        int bx = 3 + i * (BTN_W + 2);
                        if (mouse_x >= bx && mouse_x < bx + BTN_W) {
                            active_button = i;
                            button_pressed = 1;
                            mouse_hide_cursor();
                            draw_bottom_buttons();
                            mouse_draw_cursor();
                            sound_soft_note(1000, 15);
                            break;
                        }
                    }
                }
                /* 2. Check File List Click */
                else if (mouse_x >= LEFT_PANEL_X + 4 && mouse_x < LEFT_PANEL_X + LEFT_PANEL_W - 16 &&
                         mouse_y >= FILE_LIST_Y && mouse_y < FILE_LIST_Y + VISIBLE_FILES * FILE_ROW_H) {
                    int row = (mouse_y - FILE_LIST_Y) / FILE_ROW_H;
                    int clicked_idx = scroll_offset + row;
                    if (clicked_idx < total_files) {
                        /* Check double-click */
                        if (clicked_idx == last_click_file && (loop_counter - last_click_tick < 300)) {
                            /* Double click: run file! */
                            last_click_file = -1;
                            execute_action(4, clicked_idx);
                        } else {
                            /* Single click: select file */
                            selected_index = clicked_idx;
                            last_click_file = clicked_idx;
                            last_click_tick = loop_counter;
                            mouse_hide_cursor();
                            draw_files_list();
                            draw_info_card(selected_index);
                            mouse_draw_cursor();
                            sound_soft_note(800, 10);
                        }
                    }
                }
                /* 3. Check Scrollbar Buttons */
                else if (mouse_x >= SCROLL_X && mouse_x < SCROLL_X + SCROLL_W) {
                    if (mouse_y >= SCROLL_Y && mouse_y < SCROLL_Y + 10) {
                        /* Scroll Up */
                        if (scroll_offset > 0) {
                            scroll_offset--;
                            mouse_hide_cursor();
                            draw_files_list();
                            mouse_draw_cursor();
                            sound_soft_note(700, 10);
                        }
                    } else if (mouse_y >= SCROLL_Y + SCROLL_H - 10 && mouse_y < SCROLL_Y + SCROLL_H) {
                        /* Scroll Down */
                        if (scroll_offset + VISIBLE_FILES < total_files) {
                            scroll_offset++;
                            mouse_hide_cursor();
                            draw_files_list();
                            mouse_draw_cursor();
                            sound_soft_note(700, 10);
                        }
                    }
                }
            }
        }
        else if (!mouse_btn_left && prev_mouse_btn) {
            /* Left Button Released */
            if (button_pressed && active_button >= 0) {
                int btn = active_button;
                button_pressed = 0;
                active_button = -1;
                mouse_hide_cursor();
                draw_bottom_buttons();
                mouse_draw_cursor();

                if (btn == 5) {
                    /* Quit */
                    break;
                } else {
                    execute_action(btn, selected_index);
                }
            }
        }
        prev_mouse_btn = mouse_btn_left;

        /* Update cursor position on screen */
        mouse_update();

        /* -------------------------------------------------------- */
        /* D. Keyboard Input Handling                               */
        /* -------------------------------------------------------- */
        if (keyboard_has_char()) {
            char c = keyboard_getchar();

            if (show_help_modal) {
                show_help_modal = 0;
                mouse_hide_cursor();
                draw_full_gui();
                mouse_draw_cursor();
                continue;
            }

            /* Exit / Quit */
            if (c == KEY_ESC || c == '6' || c == KEY_QUIT || c == 'q' || c == 'Q') {
                break;
            }
            /* Navigate Up */
            else if (c == KEY_UP) {
                if (selected_index > 0) {
                    selected_index--;
                    if (selected_index < scroll_offset) scroll_offset = selected_index;
                    mouse_hide_cursor();
                    draw_files_list();
                    draw_info_card(selected_index);
                    mouse_draw_cursor();
                    sound_soft_note(750, 10);
                }
            }
            /* Navigate Down */
            else if (c == KEY_DOWN) {
                if (selected_index < total_files - 1) {
                    selected_index++;
                    if (selected_index >= scroll_offset + VISIBLE_FILES) {
                        scroll_offset = selected_index - VISIBLE_FILES + 1;
                    }
                    mouse_hide_cursor();
                    draw_files_list();
                    draw_info_card(selected_index);
                    mouse_draw_cursor();
                    sound_soft_note(750, 10);
                }
            }
            /* Enter: Open / Run */
            else if (c == KEY_ENTER) {
                execute_action(4, selected_index);
            }
            /* 1 or 'h' / 'H': Help */
            else if (c == '1' || c == 'h' || c == 'H') {
                execute_action(0, selected_index);
            }
            /* 2 or 'e' / 'E': Edit */
            else if (c == '2' || c == 'e' || c == 'E') {
                execute_action(1, selected_index);
            }
            /* 3 or 'x' / 'X': Hex */
            else if (c == '3' || c == 'x' || c == 'X') {
                execute_action(2, selected_index);
            }
            /* 4 or 'p' / 'P': Play */
            else if (c == '4' || c == 'p' || c == 'P') {
                execute_action(3, selected_index);
            }
            /* 5 or 'r' / 'R': Run */
            else if (c == '5' || c == 'r' || c == 'R') {
                execute_action(4, selected_index);
            }
        }
    }

    /* ------------------------------------------------------------ */
    /* Clean Shutdown: Hide mouse and restore VGA 80x25 text mode   */
    /* ------------------------------------------------------------ */
    mouse_hide_cursor();
    mouse_disable();
    mode13_exit();
    vga_clear();
}
