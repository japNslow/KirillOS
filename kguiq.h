#ifndef KGUIQ_H
#define KGUIQ_H

#include <stdint.h>
#include <stddef.h>

/* ================================================================ */
/*                     KGUIQ Color Palette Definitions              */
/* ================================================================ */
#define KGUI_COLOR_BLACK         0
#define KGUI_COLOR_BLUE          1   /* Active titlebar / Selection */
#define KGUI_COLOR_GREEN         2
#define KGUI_COLOR_CYAN          3
#define KGUI_COLOR_RED           4
#define KGUI_COLOR_MAGENTA       5
#define KGUI_COLOR_BROWN         6
#define KGUI_COLOR_LIGHT_GREY    7   /* Standard window / dialog bg */
#define KGUI_COLOR_DARK_GREY     8   /* Shadows / Inactive titlebar */
#define KGUI_COLOR_LIGHT_BLUE    9
#define KGUI_COLOR_LIGHT_GREEN   10
#define KGUI_COLOR_LIGHT_CYAN    11
#define KGUI_COLOR_LIGHT_RED     12
#define KGUI_COLOR_LIGHT_MAGENTA 13
#define KGUI_COLOR_YELLOW        14
#define KGUI_COLOR_WHITE         15  /* 3D Highlights / Text */

/* Desktop & Window Themes */
#define KGUI_BG_DESKTOP          3   /* Classic Teal */
#define KGUI_BG_WINDOW           7   /* Light Grey 3D Face */
#define KGUI_BG_CLIENT           15  /* White client area */
#define KGUI_TITLE_ACTIVE        1   /* Deep Blue */
#define KGUI_TITLE_INACTIVE      8   /* Dark Grey */
#define KGUI_3D_LIGHT            15  /* White highlight */
#define KGUI_3D_FACE             7   /* Face grey */
#define KGUI_3D_SHADOW           8   /* Dark grey shadow */
#define KGUI_3D_DARK             0   /* Black border */

/* ================================================================ */
/*                         Icon Enumeration                         */
/* ================================================================ */
typedef enum {
    KGUI_ICON_TERMINAL = 0,
    KGUI_ICON_FILES,
    KGUI_ICON_NOTEPAD,
    KGUI_ICON_PAINT,
    KGUI_ICON_MUSIC,
    KGUI_ICON_SETTINGS,
    KGUI_ICON_SYSINFO,
    KGUI_ICON_COMMANDER,
    KGUI_ICON_FOLDER,
    KGUI_ICON_FILE,
    KGUI_ICON_K_LOGO,
    KGUI_ICON_MAX
} kgui_icon_t;

/* ================================================================ */
/*                       Geometric Helpers                          */
/* ================================================================ */
int kguiq_point_in_rect(int px, int py, int x, int y, int w, int h);
void kguiq_draw_bevel(int x, int y, int w, int h, int sunken);
void kguiq_draw_rect_fill(int x, int y, int w, int h, uint8_t color);
void kguiq_draw_string_clipped(int x, int y, const char* str, uint8_t color, int max_x);

/* ================================================================ */
/*                       Widget Renderers                           */
/* ================================================================ */
void kguiq_draw_icon(int x, int y, kgui_icon_t icon_id);
void kguiq_draw_button(int x, int y, int w, int h, const char* text, int pressed, int focused);
void kguiq_draw_input(int x, int y, int w, int h, const char* text, int cursor_pos, int active);
void kguiq_draw_checkbox(int x, int y, const char* label, int checked);
void kguiq_draw_progressbar(int x, int y, int w, int h, int percent, uint8_t fill_color);
void kguiq_draw_scrollbar(int x, int y, int w, int h, int pos, int max_pos);
void kguiq_draw_window_frame(int x, int y, int w, int h, const char* title, int active, int has_min, int has_max, int has_close);

#endif /* KGUIQ_H */
