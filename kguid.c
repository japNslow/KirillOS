#include "kguid.h"
#include "mode13.h"
#include "kirillfs.h"
#include <stdint.h>

/* ================================================================ */
/*                       Internal Desktop State                     */
/* ================================================================ */
static char wallpaper_name[KFS_NAME_MAX + 1] = "demo.bmp";
static int wallpaper_enabled = 1;

#define NUM_SHORTCUTS 6
static kgui_shortcut_t shortcuts[NUM_SHORTCUTS] = {
    { 4,   4, "Term",   KGUI_ICON_TERMINAL, KGUID_APP_TERMINAL },
    { 4,  34, "Files",  KGUI_ICON_FILES,    KGUID_APP_FILES },
    { 4,  64, "Notes",  KGUI_ICON_NOTEPAD,  KGUID_APP_NOTEPAD },
    { 4,  94, "Paint",  KGUI_ICON_PAINT,    KGUID_APP_VIEWER },
    { 4, 124, "Config", KGUI_ICON_SETTINGS, KGUID_APP_SETTINGS },
    { 4, 154, "Info",   KGUI_ICON_SYSINFO,  KGUID_APP_SYSINFO }
};

/* Windows and Compositor */
static kgui_window_t windows[KGUID_MAX_WINDOWS];
static int zorder[KGUID_MAX_WINDOWS];
static int zorder_count = 0;
static int next_win_id = 1;

/* Start Menu */
static int start_menu_active = 0;
#define START_MENU_ITEMS 8
static const char* start_menu_labels[START_MENU_ITEMS] = {
    "Terminal",
    "File Explorer",
    "Text Editor",
    "Paint / Viewer",
    "Display Settings",
    "System Info",
    "Exit to Shell",
    "Reboot System"
};
static const kgui_icon_t start_menu_icons[START_MENU_ITEMS] = {
    KGUI_ICON_TERMINAL,
    KGUI_ICON_FILES,
    KGUI_ICON_NOTEPAD,
    KGUI_ICON_PAINT,
    KGUI_ICON_SETTINGS,
    KGUI_ICON_SYSINFO,
    KGUI_ICON_K_LOGO,
    KGUI_ICON_COMMANDER
};

/* ================================================================ */
/*                     CMOS RTC Direct Helper                       */
/* ================================================================ */
static inline uint8_t cmos_in(uint8_t reg) {
    uint8_t ret;
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)(reg | 0x80)), "Nd"((uint16_t)0x70));
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"((uint16_t)0x71));
    return ret;
}

static uint8_t bcd_to_bin(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

static void get_rtc_time(int* hour, int* min, int* sec) {
    uint32_t timeout = 10000;
    while ((cmos_in(0x0A) & 0x80) && --timeout);

    uint8_t s = cmos_in(0x00);
    uint8_t m = cmos_in(0x02);
    uint8_t h = cmos_in(0x04);
    uint8_t reg_b = cmos_in(0x0B);

    /* Re-enable NMI */
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x00), "Nd"((uint16_t)0x70));

    if (!(reg_b & 0x04)) {
        s = bcd_to_bin(s);
        m = bcd_to_bin(m);
        h = (uint8_t)(bcd_to_bin(h & 0x7F) | (h & 0x80));
    }
    if (!(reg_b & 0x02) && (h & 0x80)) {
        h = ((h & 0x7F) + 12) % 24;
    }
    *hour = (int)h;
    *min  = (int)m;
    *sec  = (int)s;
}

/* ================================================================ */
/*                       Desktop & Wallpaper API                    */
/* ================================================================ */
void kguid_init(void) {
    for (int i = 0; i < KGUID_MAX_WINDOWS; i++) {
        windows[i].used = 0;
        zorder[i] = -1;
    }
    zorder_count = 0;
    start_menu_active = 0;
    next_win_id = 1;
}

void kguid_set_wallpaper(const char* bmp_filename) {
    if (!bmp_filename || !bmp_filename[0] || (bmp_filename[0] == '-' && !bmp_filename[1])) {
        wallpaper_enabled = 0;
        wallpaper_name[0] = 0;
        return;
    }
    int len = 0;
    while (bmp_filename[len] && len < KFS_NAME_MAX) {
        wallpaper_name[len] = bmp_filename[len];
        len++;
    }
    wallpaper_name[len] = 0;
    wallpaper_enabled = 1;
}

const char* kguid_get_wallpaper(void) {
    return wallpaper_enabled ? wallpaper_name : "None";
}

void kguid_draw_desktop(void) {
    /* 1. Base desktop background fill */
    kguiq_draw_rect_fill(0, 0, MODE13_WIDTH, KGUID_DESKTOP_H, KGUI_BG_DESKTOP);

    /* 2. Dotted retro pattern across desktop */
    for (int py = 4; py < KGUID_DESKTOP_H; py += 8) {
        for (int px = 4; px < MODE13_WIDTH; px += 8) {
            mode13_put_pixel(px, py, KGUI_COLOR_DARK_GREY);
        }
    }

    /* 3. Render BMP Wallpaper if present and valid */
    if (wallpaper_enabled && wallpaper_name[0]) {
        const char* file_data = 0;
        int file_size = 0;
        if (kfs_read(wallpaper_name, &file_data, &file_size) == KFS_OK && file_size > 54) {
            const uint8_t* p = (const uint8_t*)file_data;
            if (p[0] == 0x42 && p[1] == 0x4D) { /* "BM" */
                uint32_t off_bits = (uint32_t)p[10] | ((uint32_t)p[11] << 8) |
                                    ((uint32_t)p[12] << 16) | ((uint32_t)p[13] << 24);
                int32_t bmp_w = (int32_t)((uint32_t)p[18] | ((uint32_t)p[19] << 8) |
                                          ((uint32_t)p[20] << 16) | ((uint32_t)p[21] << 24));
                int32_t bmp_h = (int32_t)((uint32_t)p[22] | ((uint32_t)p[23] << 8) |
                                          ((uint32_t)p[24] << 16) | ((uint32_t)p[25] << 24));
                uint16_t bpp = (uint16_t)p[28] | ((uint16_t)p[29] << 8);

                if (bpp == 8 && bmp_w > 0 && bmp_h != 0) {
                    int bottom_up = 1;
                    if (bmp_h < 0) { bmp_h = -bmp_h; bottom_up = 0; }

                    int draw_w = bmp_w > MODE13_WIDTH ? MODE13_WIDTH : bmp_w;
                    int draw_h = bmp_h > KGUID_DESKTOP_H ? KGUID_DESKTOP_H : bmp_h;
                    int start_x = (MODE13_WIDTH - draw_w) / 2;
                    int start_y = (KGUID_DESKTOP_H - draw_h) / 2;
                    int row_stride = (bmp_w + 3) & ~3;

                    /* Draw beveled frame around centered wallpaper */
                    if (start_x > 2 && start_y > 2) {
                        kguiq_draw_bevel(start_x - 2, start_y - 2, draw_w + 4, draw_h + 4, 1);
                    }

                    for (int y = 0; y < draw_h; y++) {
                        int src_y = bottom_up ? (bmp_h - 1 - y) : y;
                        const uint8_t* row = p + off_bits + src_y * row_stride;
                        uint8_t* dst = MODE13_VRAM + (start_y + y) * MODE13_WIDTH + start_x;
                        for (int x = 0; x < draw_w; x++) {
                            dst[x] = row[x];
                        }
                    }
                }
            }
        }
    }

    /* 4. Desktop Branding watermark */
    mode13_draw_string(216, 172, "KirillOS 0.2", KGUI_COLOR_DARK_GREY);
}

/* ================================================================ */
/*                       Desktop Shortcuts API                      */
/* ================================================================ */
int kguid_shortcut_count(void) { return NUM_SHORTCUTS; }

kgui_shortcut_t* kguid_get_shortcut(int index) {
    if (index >= 0 && index < NUM_SHORTCUTS) return &shortcuts[index];
    return 0;
}

int kguid_find_shortcut_at(int px, int py) {
    for (int i = 0; i < NUM_SHORTCUTS; i++) {
        if (kguiq_point_in_rect(px, py, shortcuts[i].x - 1, shortcuts[i].y - 1, 42, 28)) {
            return i;
        }
    }
    return -1;
}

void kguid_draw_shortcuts(int selected_idx) {
    for (int i = 0; i < NUM_SHORTCUTS; i++) {
        int sx = shortcuts[i].x;
        int sy = shortcuts[i].y;

        if (i == selected_idx) {
            kguiq_draw_rect_fill(sx - 1, sy - 1, 42, 28, KGUI_COLOR_BLUE);
            mode13_draw_rect(sx - 1, sy - 1, 42, 28, KGUI_COLOR_WHITE);
        }

        /* Draw Icon in center of shortcut box */
        kguiq_draw_icon(sx + 14, sy + 2, shortcuts[i].icon);

        /* Draw Label */
        int text_len = 0;
        while (shortcuts[i].label[text_len]) text_len++;
        int text_w = text_len * 8;
        int tx = sx + (40 - text_w) / 2;
        if (tx < sx) tx = sx;
        uint8_t text_color = (i == selected_idx) ? KGUI_COLOR_WHITE : KGUI_COLOR_WHITE;
        kguiq_draw_string_clipped(tx, sy + 16, shortcuts[i].label, text_color, sx + 52);
    }
}

/* ================================================================ */
/*                       Window Management API                      */
/* ================================================================ */
int kguid_create_window(const char* title, int x, int y, int w, int h, kguid_app_t app_type, kgui_icon_t icon) {
    int slot = -1;
    for (int i = 0; i < KGUID_MAX_WINDOWS; i++) {
        if (!windows[i].used) { slot = i; break; }
    }
    if (slot == -1) return -1; /* No free window slots */

    kgui_window_t* win = &windows[slot];
    win->id = next_win_id++;
    win->x = x < 0 ? 0 : x;
    win->y = y < 0 ? 0 : y;
    win->w = w < 80 ? 80 : (w > MODE13_WIDTH ? MODE13_WIDTH : w);
    win->h = h < 50 ? 50 : (h > KGUID_DESKTOP_H ? KGUID_DESKTOP_H : h);
    win->orig_x = win->x;
    win->orig_y = win->y;
    win->orig_w = win->w;
    win->orig_h = win->h;
    win->app_type = app_type;
    win->icon = icon;
    win->minimized = 0;
    win->maximized = 0;
    win->used = 1;

    int t_len = 0;
    while (title && title[t_len] && t_len < 31) {
        win->title[t_len] = title[t_len];
        t_len++;
    }
    win->title[t_len] = 0;

    /* Add to top of z-order and focus */
    zorder[zorder_count++] = slot;
    kguid_focus_window(win->id);

    return win->id;
}

void kguid_focus_window(int id) {
    int slot = -1;
    for (int i = 0; i < KGUID_MAX_WINDOWS; i++) {
        if (windows[i].used && windows[i].id == id) { slot = i; break; }
    }
    if (slot == -1) return;

    /* Unfocus all */
    for (int i = 0; i < KGUID_MAX_WINDOWS; i++) {
        if (windows[i].used) windows[i].active = 0;
    }
    windows[slot].active = 1;
    windows[slot].minimized = 0;

    /* Move to top of z-order */
    int z_pos = -1;
    for (int i = 0; i < zorder_count; i++) {
        if (zorder[i] == slot) { z_pos = i; break; }
    }
    if (z_pos != -1) {
        for (int i = z_pos; i < zorder_count - 1; i++) {
            zorder[i] = zorder[i + 1];
        }
        zorder[zorder_count - 1] = slot;
    }
}

void kguid_minimize_window(int id) {
    kgui_window_t* win = kguid_get_window(id);
    if (!win) return;
    win->minimized = 1;
    win->active = 0;

    /* Focus next topmost visible window */
    for (int i = zorder_count - 1; i >= 0; i--) {
        int s = zorder[i];
        if (windows[s].used && !windows[s].minimized) {
            kguid_focus_window(windows[s].id);
            break;
        }
    }
}

void kguid_maximize_window(int id) {
    kgui_window_t* win = kguid_get_window(id);
    if (!win) return;
    if (win->maximized) {
        win->x = win->orig_x;
        win->y = win->orig_y;
        win->w = win->orig_w;
        win->h = win->orig_h;
        win->maximized = 0;
    } else {
        win->orig_x = win->x;
        win->orig_y = win->y;
        win->orig_w = win->w;
        win->orig_h = win->h;
        win->x = 0;
        win->y = 0;
        win->w = MODE13_WIDTH;
        win->h = KGUID_DESKTOP_H;
        win->maximized = 1;
    }
    kguid_focus_window(id);
}

void kguid_close_window(int id) {
    int slot = -1;
    for (int i = 0; i < KGUID_MAX_WINDOWS; i++) {
        if (windows[i].used && windows[i].id == id) { slot = i; break; }
    }
    if (slot == -1) return;

    windows[slot].used = 0;

    /* Remove from z-order */
    int z_pos = -1;
    for (int i = 0; i < zorder_count; i++) {
        if (zorder[i] == slot) { z_pos = i; break; }
    }
    if (z_pos != -1) {
        for (int i = z_pos; i < zorder_count - 1; i++) {
            zorder[i] = zorder[i + 1];
        }
        zorder_count--;
    }

    /* Focus new top window */
    if (zorder_count > 0) {
        int top_slot = zorder[zorder_count - 1];
        kguid_focus_window(windows[top_slot].id);
    }
}

kgui_window_t* kguid_get_window(int id) {
    for (int i = 0; i < KGUID_MAX_WINDOWS; i++) {
        if (windows[i].used && windows[i].id == id) return &windows[i];
    }
    return 0;
}

int kguid_get_focused_window(void) {
    for (int i = 0; i < KGUID_MAX_WINDOWS; i++) {
        if (windows[i].used && windows[i].active && !windows[i].minimized) return windows[i].id;
    }
    return -1;
}

int kguid_find_window_at(int px, int py) {
    for (int i = zorder_count - 1; i >= 0; i--) {
        int s = zorder[i];
        if (windows[s].used && !windows[s].minimized) {
            if (kguiq_point_in_rect(px, py, windows[s].x, windows[s].y, windows[s].w, windows[s].h)) {
                return windows[s].id;
            }
        }
    }
    return -1;
}

int kguid_find_titlebar_at(int px, int py, int* out_btn) {
    if (out_btn) *out_btn = 0;
    for (int i = zorder_count - 1; i >= 0; i--) {
        int s = zorder[i];
        if (windows[s].used && !windows[s].minimized) {
            if (kguiq_point_in_rect(px, py, windows[s].x, windows[s].y, windows[s].w, 16)) {
                int btn_size = 10;
                int btn_y = windows[s].y + 4;
                int close_x = windows[s].x + windows[s].w - btn_size - 4;
                int max_x   = close_x - btn_size - 2;
                int min_x   = max_x - btn_size - 2;

                if (out_btn) {
                    if (kguiq_point_in_rect(px, py, close_x, btn_y, btn_size, btn_size)) *out_btn = 3; /* Close */
                    else if (kguiq_point_in_rect(px, py, max_x, btn_y, btn_size, btn_size)) *out_btn = 2; /* Max */
                    else if (kguiq_point_in_rect(px, py, min_x, btn_y, btn_size, btn_size)) *out_btn = 1; /* Min */
                    else *out_btn = 0; /* Titlebar drag */
                }
                return windows[s].id;
            }
        }
    }
    return -1;
}

int kguid_get_zorder_count(void) { return zorder_count; }

int kguid_get_zorder_window(int z_index) {
    if (z_index >= 0 && z_index < zorder_count) {
        int s = zorder[z_index];
        if (windows[s].used) return windows[s].id;
    }
    return -1;
}

/* ================================================================ */
/*                       Taskbar & Start Menu API                   */
/* ================================================================ */
void kguid_draw_taskbar(int start_pressed, int focused_win_id) {
    /* 1. Base Taskbar Surface */
    kguiq_draw_rect_fill(0, KGUID_TASKBAR_Y, MODE13_WIDTH, KGUID_TASKBAR_H, KGUI_3D_FACE);
    mode13_draw_line(0, KGUID_TASKBAR_Y, MODE13_WIDTH - 1, KGUID_TASKBAR_Y, KGUI_3D_LIGHT);

    /* 2. Start Button */
    int sb_x = 2;
    int sb_y = KGUID_TASKBAR_Y + 2;
    int sb_w = 54;
    int sb_h = 12;
    kguiq_draw_rect_fill(sb_x, sb_y, sb_w, sb_h, KGUI_3D_FACE);
    kguiq_draw_bevel(sb_x, sb_y, sb_w, sb_h, start_pressed || start_menu_active);
    kguiq_draw_icon(sb_x + 2, sb_y + 1, KGUI_ICON_K_LOGO);
    mode13_draw_string(sb_x + 14, sb_y + 2, "Start", KGUI_COLOR_BLACK);

    /* 3. Open Windows on Taskbar */
    int cur_bx = 58;
    for (int i = 0; i < KGUID_MAX_WINDOWS; i++) {
        if (!windows[i].used) continue;
        int btn_w = 54;
        if (cur_bx + btn_w > 244) break; /* Avoid tray overlap */
        int is_active = (windows[i].id == focused_win_id && !windows[i].minimized);

        kguiq_draw_rect_fill(cur_bx, sb_y, btn_w, sb_h, KGUI_3D_FACE);
        kguiq_draw_bevel(cur_bx, sb_y, btn_w, sb_h, is_active);
        kguiq_draw_icon(cur_bx + 2, sb_y + 1, windows[i].icon);
        kguiq_draw_string_clipped(cur_bx + 14, sb_y + 2, windows[i].title, KGUI_COLOR_BLACK, cur_bx + btn_w - 2);

        cur_bx += (btn_w + 2);
    }

    /* 4. System Tray (Notification Area & Widgets) */
    int tray_x = 246;
    int tray_y = KGUID_TASKBAR_Y + 2;
    int tray_w = 72;
    int tray_h = 12;
    kguiq_draw_rect_fill(tray_x, tray_y, tray_w, tray_h, KGUI_3D_FACE);
    kguiq_draw_bevel(tray_x, tray_y, tray_w, tray_h, 1); /* Sunken tray */

    /* Live Digital Clock Widget */
    int h = 0, m = 0, s = 0;
    get_rtc_time(&h, &m, &s);
    char time_str[9];
    time_str[0] = (char)('0' + (h / 10));
    time_str[1] = (char)('0' + (h % 10));
    time_str[2] = ':';
    time_str[3] = (char)('0' + (m / 10));
    time_str[4] = (char)('0' + (m % 10));
    time_str[5] = ':';
    time_str[6] = (char)('0' + (s / 10));
    time_str[7] = (char)('0' + (s % 10));
    time_str[8] = 0;

    mode13_draw_string(tray_x + 4, tray_y + 2, time_str, KGUI_COLOR_BLACK);
}

int kguid_find_taskbar_window_at(int px, int py) {
    if (py < KGUID_TASKBAR_Y + 2 || py >= KGUID_TASKBAR_Y + 14) return -1;
    int cur_bx = 58;
    for (int i = 0; i < KGUID_MAX_WINDOWS; i++) {
        if (!windows[i].used) continue;
        int btn_w = 54;
        if (cur_bx + btn_w > 244) break;
        if (px >= cur_bx && px < cur_bx + btn_w) {
            return windows[i].id;
        }
        cur_bx += (btn_w + 2);
    }
    return -1;
}

int kguid_is_start_menu_open(void) { return start_menu_active; }

void kguid_toggle_start_menu(void) { start_menu_active = !start_menu_active; }

void kguid_set_start_menu(int open) { start_menu_active = open; }

int kguid_start_menu_item_at(int px, int py) {
    if (!start_menu_active) return -1;
    int sm_x = 2;
    int sm_y = 86;
    int sm_w = 114;
    int sm_h = 96;
    if (!kguiq_point_in_rect(px, py, sm_x, sm_y, sm_w, sm_h)) return -1;

    int item_idx = (py - (sm_y + 3)) / 11;
    if (item_idx >= 0 && item_idx < START_MENU_ITEMS) return item_idx;
    return -1;
}

void kguid_draw_start_menu(int hovered_idx) {
    if (!start_menu_active) return;
    int sm_x = 2;
    int sm_y = 86;
    int sm_w = 114;
    int sm_h = 96;

    /* Menu container & 3D shadow */
    kguiq_draw_rect_fill(sm_x, sm_y, sm_w, sm_h, KGUI_3D_FACE);
    kguiq_draw_bevel(sm_x, sm_y, sm_w, sm_h, 0); /* Raised menu */

    /* Items */
    for (int i = 0; i < START_MENU_ITEMS; i++) {
        int iy = sm_y + 3 + i * 11;
        if (i == hovered_idx) {
            kguiq_draw_rect_fill(sm_x + 3, iy, sm_w - 6, 11, KGUI_COLOR_BLUE);
        }

        kguiq_draw_icon(sm_x + 4, iy + 1, start_menu_icons[i]);
        uint8_t tc = (i == hovered_idx) ? KGUI_COLOR_WHITE : KGUI_COLOR_BLACK;
        kguiq_draw_string_clipped(sm_x + 18, iy + 2, start_menu_labels[i], tc, sm_x + sm_w - 4);
    }
}
