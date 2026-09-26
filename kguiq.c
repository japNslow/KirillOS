#include "kguiq.h"
#include "mode13.h"

/* ================================================================ */
/*                       Geometric Helpers                          */
/* ================================================================ */
int kguiq_point_in_rect(int px, int py, int x, int y, int w, int h) {
    return (px >= x && px < x + w && py >= y && py < y + h);
}

void kguiq_draw_rect_fill(int x, int y, int w, int h, uint8_t color) {
    if (w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w) > MODE13_WIDTH ? MODE13_WIDTH : (x + w);
    int y1 = (y + h) > MODE13_HEIGHT ? MODE13_HEIGHT : (y + h);
    if (x0 >= x1 || y0 >= y1) return;

    for (int cy = y0; cy < y1; cy++) {
        uint8_t* row = MODE13_VRAM + cy * MODE13_WIDTH + x0;
        int len = x1 - x0;
        for (int i = 0; i < len; i++) row[i] = color;
    }
}

void kguiq_draw_bevel(int x, int y, int w, int h, int sunken) {
    if (w <= 1 || h <= 1) return;
    uint8_t c_top_out  = sunken ? KGUI_3D_DARK   : KGUI_3D_LIGHT;
    uint8_t c_top_in   = sunken ? KGUI_3D_SHADOW : KGUI_3D_FACE;
    uint8_t c_bot_in   = sunken ? KGUI_3D_FACE   : KGUI_3D_SHADOW;
    uint8_t c_bot_out  = sunken ? KGUI_3D_LIGHT  : KGUI_3D_DARK;

    /* Top and Left outer */
    mode13_draw_line(x, y, x + w - 1, y, c_top_out);
    mode13_draw_line(x, y, x, y + h - 1, c_top_out);

    /* Bottom and Right outer */
    mode13_draw_line(x, y + h - 1, x + w - 1, y + h - 1, c_bot_out);
    mode13_draw_line(x + w - 1, y, x + w - 1, y + h - 1, c_bot_out);

    if (w > 3 && h > 3) {
        /* Top and Left inner */
        mode13_draw_line(x + 1, y + 1, x + w - 2, y + 1, c_top_in);
        mode13_draw_line(x + 1, y + 1, x + 1, y + h - 2, c_top_in);

        /* Bottom and Right inner */
        mode13_draw_line(x + 1, y + h - 2, x + w - 2, y + h - 2, c_bot_in);
        mode13_draw_line(x + w - 2, y + 1, x + w - 2, y + h - 2, c_bot_in);
    }
}

void kguiq_draw_string_clipped(int x, int y, const char* str, uint8_t color, int max_x) {
    if (!str) return;
    int cur_x = x;
    while (*str) {
        if (cur_x + 8 > max_x) break;
        mode13_draw_char(cur_x, y, *str++, color);
        cur_x += 8;
    }
}

/* ================================================================ */
/*                       12x12 System Icons                         */
/* ================================================================ */
void kguiq_draw_icon(int x, int y, kgui_icon_t icon_id) {
    switch (icon_id) {
        case KGUI_ICON_TERMINAL: {
            /* Black mini screen with green prompt */
            kguiq_draw_rect_fill(x, y, 12, 11, KGUI_COLOR_BLACK);
            mode13_draw_rect(x, y, 12, 11, KGUI_COLOR_DARK_GREY);
            mode13_draw_char(x + 1, y + 1, '>', KGUI_COLOR_LIGHT_GREEN);
            mode13_draw_line(x + 6, y + 8, x + 9, y + 8, KGUI_COLOR_WHITE);
            break;
        }
        case KGUI_ICON_FILES: {
            /* Yellow file folder / cabinet */
            kguiq_draw_rect_fill(x, y + 2, 5, 2, KGUI_COLOR_BROWN);
            kguiq_draw_rect_fill(x, y + 3, 12, 8, KGUI_COLOR_YELLOW);
            mode13_draw_rect(x, y + 3, 12, 8, KGUI_COLOR_BROWN);
            mode13_draw_line(x + 2, y + 6, x + 9, y + 6, KGUI_COLOR_WHITE);
            break;
        }
        case KGUI_ICON_NOTEPAD: {
            /* White document with lines and blue corner */
            kguiq_draw_rect_fill(x + 1, y, 10, 12, KGUI_COLOR_WHITE);
            mode13_draw_rect(x + 1, y, 10, 12, KGUI_COLOR_DARK_GREY);
            mode13_draw_line(x + 3, y + 3, x + 8, y + 3, KGUI_COLOR_BLUE);
            mode13_draw_line(x + 3, y + 6, x + 8, y + 6, KGUI_COLOR_BLUE);
            mode13_draw_line(x + 3, y + 9, x + 7, y + 9, KGUI_COLOR_BLUE);
            break;
        }
        case KGUI_ICON_PAINT: {
            /* Color palette */
            kguiq_draw_rect_fill(x + 1, y + 1, 10, 9, KGUI_COLOR_LIGHT_GREY);
            mode13_draw_rect(x + 1, y + 1, 10, 9, KGUI_COLOR_BLACK);
            mode13_put_pixel(x + 3, y + 3, KGUI_COLOR_RED);
            mode13_put_pixel(x + 7, y + 3, KGUI_COLOR_BLUE);
            mode13_put_pixel(x + 3, y + 7, KGUI_COLOR_GREEN);
            mode13_put_pixel(x + 7, y + 7, KGUI_COLOR_YELLOW);
            mode13_draw_line(x + 8, y + 2, x + 11, y + 0, KGUI_COLOR_BROWN);
            break;
        }
        case KGUI_ICON_MUSIC: {
            /* Blue note pair */
            kguiq_draw_rect_fill(x, y, 12, 11, KGUI_COLOR_LIGHT_GREY);
            mode13_draw_line(x + 3, y + 2, x + 9, y + 2, KGUI_COLOR_BLUE);
            mode13_draw_line(x + 3, y + 3, x + 9, y + 3, KGUI_COLOR_BLUE);
            mode13_draw_line(x + 3, y + 2, x + 3, y + 8, KGUI_COLOR_BLUE);
            mode13_draw_line(x + 9, y + 2, x + 9, y + 8, KGUI_COLOR_BLUE);
            mode13_fill_circle(x + 2, y + 8, 2, KGUI_COLOR_BLUE);
            mode13_fill_circle(x + 8, y + 8, 2, KGUI_COLOR_BLUE);
            break;
        }
        case KGUI_ICON_SETTINGS: {
            /* Monitor display */
            kguiq_draw_rect_fill(x + 1, y, 10, 8, KGUI_COLOR_CYAN);
            mode13_draw_rect(x, y, 12, 9, KGUI_COLOR_DARK_GREY);
            mode13_draw_line(x + 5, y + 9, x + 6, y + 10, KGUI_COLOR_DARK_GREY);
            mode13_draw_line(x + 3, y + 11, x + 8, y + 11, KGUI_COLOR_BLACK);
            break;
        }
        case KGUI_ICON_SYSINFO: {
            /* Microchip / CPU */
            kguiq_draw_rect_fill(x + 2, y + 2, 8, 8, KGUI_COLOR_DARK_GREY);
            mode13_draw_rect(x + 2, y + 2, 8, 8, KGUI_COLOR_BLACK);
            mode13_draw_char(x + 3, y + 2, 'K', KGUI_COLOR_YELLOW);
            mode13_draw_line(x + 3, y, x + 3, y + 1, KGUI_COLOR_LIGHT_GREY);
            mode13_draw_line(x + 8, y, x + 8, y + 1, KGUI_COLOR_LIGHT_GREY);
            mode13_draw_line(x + 3, y + 10, x + 3, y + 11, KGUI_COLOR_LIGHT_GREY);
            mode13_draw_line(x + 8, y + 10, x + 8, y + 11, KGUI_COLOR_LIGHT_GREY);
            break;
        }
        case KGUI_ICON_COMMANDER: {
            /* Dual panel blue commander */
            kguiq_draw_rect_fill(x, y + 1, 5, 9, KGUI_COLOR_BLUE);
            kguiq_draw_rect_fill(x + 6, y + 1, 5, 9, KGUI_COLOR_BLUE);
            mode13_draw_rect(x, y + 1, 12, 9, KGUI_COLOR_WHITE);
            mode13_draw_line(x + 5, y + 1, x + 5, y + 10, KGUI_COLOR_WHITE);
            break;
        }
        case KGUI_ICON_K_LOGO: {
            /* Red and White KirillOS K */
            kguiq_draw_rect_fill(x, y, 12, 12, KGUI_COLOR_RED);
            mode13_draw_rect(x, y, 12, 12, KGUI_COLOR_WHITE);
            mode13_draw_char(x + 2, y + 2, 'K', KGUI_COLOR_WHITE);
            break;
        }
        default: {
            kguiq_draw_rect_fill(x, y, 10, 10, KGUI_COLOR_LIGHT_BLUE);
            mode13_draw_rect(x, y, 10, 10, KGUI_COLOR_BLACK);
            break;
        }
    }
}

/* ================================================================ */
/*                       Widget Renderers                           */
/* ================================================================ */
void kguiq_draw_button(int x, int y, int w, int h, const char* text, int pressed, int focused) {
    kguiq_draw_rect_fill(x + 1, y + 1, w - 2, h - 2, KGUI_3D_FACE);
    kguiq_draw_bevel(x, y, w, h, pressed);

    if (focused && !pressed && w > 6 && h > 6) {
        mode13_draw_rect(x + 2, y + 2, w - 4, h - 4, KGUI_3D_SHADOW);
    }

    if (text) {
        int text_len = 0;
        while (text[text_len]) text_len++;
        int text_w = text_len * 8;
        int tx = x + (w - text_w) / 2;
        int ty = y + (h - 8) / 2;
        if (pressed) { tx += 1; ty += 1; }
        if (tx < x + 3) tx = x + 3;
        kguiq_draw_string_clipped(tx, ty, text, KGUI_COLOR_BLACK, x + w - 3);
    }
}

void kguiq_draw_input(int x, int y, int w, int h, const char* text, int cursor_pos, int active) {
    kguiq_draw_rect_fill(x + 2, y + 2, w - 4, h - 4, KGUI_COLOR_WHITE);
    kguiq_draw_bevel(x, y, w, h, 1); /* Sunken */

    if (text) {
        kguiq_draw_string_clipped(x + 4, y + (h - 8) / 2, text, KGUI_COLOR_BLACK, x + w - 4);
    }

    if (active && cursor_pos >= 0) {
        int cx = x + 4 + cursor_pos * 8;
        if (cx < x + w - 4) {
            mode13_draw_line(cx, y + 3, cx, y + h - 4, KGUI_COLOR_BLACK);
        }
    }
}

void kguiq_draw_checkbox(int x, int y, const char* label, int checked) {
    kguiq_draw_rect_fill(x + 1, y + 1, 9, 9, KGUI_COLOR_WHITE);
    kguiq_draw_bevel(x, y, 11, 11, 1);
    if (checked) {
        mode13_draw_line(x + 2, y + 5, x + 4, y + 8, KGUI_COLOR_BLACK);
        mode13_draw_line(x + 4, y + 8, x + 8, y + 2, KGUI_COLOR_BLACK);
    }
    if (label) {
        mode13_draw_string(x + 15, y + 2, label, KGUI_COLOR_BLACK);
    }
}

void kguiq_draw_progressbar(int x, int y, int w, int h, int percent, uint8_t fill_color) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    kguiq_draw_rect_fill(x + 1, y + 1, w - 2, h - 2, KGUI_COLOR_LIGHT_GREY);
    kguiq_draw_bevel(x, y, w, h, 1);

    int fill_w = ((w - 4) * percent) / 100;
    if (fill_w > 0) {
        kguiq_draw_rect_fill(x + 2, y + 2, fill_w, h - 4, fill_color);
    }
}

void kguiq_draw_scrollbar(int x, int y, int w, int h, int pos, int max_pos) {
    kguiq_draw_rect_fill(x, y, w, h, KGUI_COLOR_LIGHT_GREY);
    kguiq_draw_bevel(x, y, w, h, 1);

    /* Up arrow button */
    kguiq_draw_button(x, y, w, w, "^", 0, 0);
    /* Down arrow button */
    kguiq_draw_button(x, y + h - w, w, w, "v", 0, 0);

    /* Thumb */
    int track_h = h - 2 * w;
    if (track_h > 12 && max_pos > 0) {
        int thumb_h = track_h / (max_pos + 1);
        if (thumb_h < 8) thumb_h = 8;
        int thumb_y = y + w + ((track_h - thumb_h) * pos) / max_pos;
        kguiq_draw_button(x + 1, thumb_y, w - 2, thumb_h, "", 0, 0);
    }
}

void kguiq_draw_window_frame(int x, int y, int w, int h, const char* title, int active, int has_min, int has_max, int has_close) {
    /* 1. Window 3D Outer border & face */
    kguiq_draw_rect_fill(x, y, w, h, KGUI_BG_WINDOW);
    kguiq_draw_bevel(x, y, w, h, 0); /* Raised outer frame */

    /* 2. Title Bar */
    int tb_x = x + 3;
    int tb_y = y + 3;
    int tb_w = w - 6;
    int tb_h = 12;
    uint8_t tb_color = active ? KGUI_TITLE_ACTIVE : KGUI_TITLE_INACTIVE;
    kguiq_draw_rect_fill(tb_x, tb_y, tb_w, tb_h, tb_color);

    /* 3. Window Icon & Title */
    int title_x = tb_x + 3;
    int title_max_x = tb_x + tb_w - 34;
    kguiq_draw_string_clipped(title_x, tb_y + 2, title ? title : "Window", KGUI_COLOR_WHITE, title_max_x);

    /* 4. Titlebar Control Buttons */
    int btn_size = 10;
    int btn_y = tb_y + 1;
    int cur_bx = tb_x + tb_w - btn_size - 1;

    if (has_close) {
        kguiq_draw_button(cur_bx, btn_y, btn_size, btn_size, "", 0, 0);
        /* Draw sharp X */
        mode13_draw_line(cur_bx + 2, btn_y + 2, cur_bx + 7, btn_y + 7, KGUI_COLOR_BLACK);
        mode13_draw_line(cur_bx + 3, btn_y + 2, cur_bx + 6, btn_y + 5, KGUI_COLOR_BLACK);
        mode13_draw_line(cur_bx + 2, btn_y + 7, cur_bx + 7, btn_y + 2, KGUI_COLOR_BLACK);
        mode13_draw_line(cur_bx + 3, btn_y + 7, cur_bx + 6, btn_y + 4, KGUI_COLOR_BLACK);
        cur_bx -= (btn_size + 2);
    }
    if (has_max) {
        kguiq_draw_button(cur_bx, btn_y, btn_size, btn_size, "", 0, 0);
        /* Draw maximize window frame */
        mode13_draw_rect(cur_bx + 2, btn_y + 2, 6, 6, KGUI_COLOR_BLACK);
        mode13_draw_line(cur_bx + 2, btn_y + 3, cur_bx + 7, btn_y + 3, KGUI_COLOR_BLACK);
        cur_bx -= (btn_size + 2);
    }
    if (has_min) {
        kguiq_draw_button(cur_bx, btn_y, btn_size, btn_size, "", 0, 0);
        /* Draw minimize underline */
        mode13_draw_line(cur_bx + 2, btn_y + 7, cur_bx + 7, btn_y + 7, KGUI_COLOR_BLACK);
        mode13_draw_line(cur_bx + 2, btn_y + 8, cur_bx + 7, btn_y + 8, KGUI_COLOR_BLACK);
    }

    /* 5. Client Area Inset */
    int ca_x = x + 3;
    int ca_y = y + 17;
    int ca_w = w - 6;
    int ca_h = h - 20;
    kguiq_draw_bevel(ca_x, ca_y, ca_w, ca_h, 1); /* Sunken client border */
}
