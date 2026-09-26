#ifndef KGUID_H
#define KGUID_H

#include <stdint.h>
#include <stddef.h>
#include "kguiq.h"

#define KGUID_MAX_WINDOWS       8
#define KGUID_DESKTOP_H         184
#define KGUID_TASKBAR_H         16
#define KGUID_TASKBAR_Y         184

/* Application Types */
typedef enum {
    KGUID_APP_NONE = 0,
    KGUID_APP_TERMINAL,
    KGUID_APP_NOTEPAD,
    KGUID_APP_FILES,
    KGUID_APP_SETTINGS,
    KGUID_APP_SYSINFO,
    KGUID_APP_VIEWER,
    KGUID_APP_MAX
} kguid_app_t;

/* Window Structure */
typedef struct {
    int id;
    int x;
    int y;
    int w;
    int h;
    char title[32];
    int active;
    int minimized;
    int maximized;
    int orig_x;
    int orig_y;
    int orig_w;
    int orig_h;
    kguid_app_t app_type;
    kgui_icon_t icon;
    int used;
} kgui_window_t;

/* Desktop Shortcut Icon */
typedef struct {
    int x;
    int y;
    char label[16];
    kgui_icon_t icon;
    kguid_app_t app_type;
} kgui_shortcut_t;

/* ================================================================ */
/*                       Desktop & Wallpaper API                    */
/* ================================================================ */
void kguid_init(void);
void kguid_set_wallpaper(const char* bmp_filename);
const char* kguid_get_wallpaper(void);
void kguid_draw_desktop(void);

/* ================================================================ */
/*                       Desktop Shortcuts API                      */
/* ================================================================ */
int kguid_shortcut_count(void);
kgui_shortcut_t* kguid_get_shortcut(int index);
int kguid_find_shortcut_at(int px, int py);
void kguid_draw_shortcuts(int selected_idx);

/* ================================================================ */
/*                       Window Management API                      */
/* ================================================================ */
int kguid_create_window(const char* title, int x, int y, int w, int h, kguid_app_t app_type, kgui_icon_t icon);
void kguid_close_window(int id);
void kguid_focus_window(int id);
void kguid_minimize_window(int id);
void kguid_maximize_window(int id);
kgui_window_t* kguid_get_window(int id);
int kguid_get_focused_window(void);
int kguid_find_window_at(int px, int py);
int kguid_find_titlebar_at(int px, int py, int* out_btn); /* out_btn: 0=bar, 1=min, 2=max, 3=close */
int kguid_get_zorder_count(void);
int kguid_get_zorder_window(int z_index);

/* ================================================================ */
/*                       Taskbar & Start Menu API                   */
/* ================================================================ */
void kguid_draw_taskbar(int start_pressed, int focused_win_id);
int kguid_find_taskbar_window_at(int px, int py);
int kguid_is_start_menu_open(void);
void kguid_toggle_start_menu(void);
void kguid_set_start_menu(int open);
void kguid_draw_start_menu(int hovered_idx);
int kguid_start_menu_item_at(int px, int py);

#endif /* KGUID_H */
