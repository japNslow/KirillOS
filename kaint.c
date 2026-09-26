#include "kaint.h"
#include "mode13.h"
#include "mouse.h"
#include "keyboard.h"
#include "kirillfs.h"
#include "sound.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

/* ================================================================ */
/*                     Port I/O Helpers                             */
/* ================================================================ */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* ================================================================ */
/*                     UI Layout Constants                          */
/* ================================================================ */
#define CANVAS_X        52
#define CANVAS_Y        16
#define CANVAS_W        240
#define CANVAS_H        150

#define TOOLBAR_W       48
#define PALETTE_Y       171

/* Colors from standard VGA 16 */
#define C_BLACK         0
#define C_BLUE          1
#define C_GREEN         2
#define C_CYAN          3
#define C_RED           4
#define C_MAGENTA       5
#define C_BROWN         6
#define C_LIGHT_GREY    7
#define C_DARK_GREY     8
#define C_LIGHT_BLUE    9
#define C_LIGHT_GREEN   10
#define C_LIGHT_CYAN    11
#define C_LIGHT_RED     12
#define C_LIGHT_MAGENTA 13
#define C_YELLOW        14
#define C_WHITE         15

/* Palette row 2 colors (rich Mode 13h indices) */
static const uint8_t PALETTE_ROW2[16] = {
    40, /* Orange */
    44, /* Coral */
    55, /* Sky Blue */
    50, /* Teal */
    42, /* Gold */
    47, /* Lime */
    48, /* Forest Green */
    54, /* Navy */
    58, /* Purple */
    60, /* Violet */
    36, /* Peach / Skin */
    38, /* Tan */
    32, /* Crimson */
    23, /* Charcoal */
    31, /* Cream */
    100 /* Dark Brown */
};

/* ================================================================ */
/*                         Tools Enum                               */
/* ================================================================ */
typedef enum {
    TOOL_PENCIL = 0,
    TOOL_BRUSH,
    TOOL_LINE,
    TOOL_RECT,
    TOOL_FILL_RECT,
    TOOL_CIRCLE,
    TOOL_FILL_CIRCLE,
    TOOL_BUCKET,
    TOOL_TEXT,
    TOOL_ERASER,
    TOOL_PICKER,
    TOOL_COUNT
} kaint_tool_t;

static const char* const TOOL_NAMES[TOOL_COUNT] = {
    "Pencil", "Brush", "Line", "Rect", "FRect", "Circle", "FCirc", "Bucket", "Text", "Eraser", "Picker"
};

/* ================================================================ */
/*                     Global Application State                     */
/* ================================================================ */
static uint8_t canvas[CANVAS_W * CANVAS_H];
static uint8_t primary_color = C_BLACK;
static uint8_t secondary_color = C_WHITE;
static kaint_tool_t current_tool = TOOL_PENCIL;
static char current_filename[KFS_NAME_MAX + 1] = "ART.BMP";
static char status_msg[64] = "Welcome to Kaint! Select tool and draw.";

/* Drawing drag state */
static int is_dragging = 0;
static int drag_button = 0; /* 1 = left, 2 = right */
static int drag_start_x = -1;
static int drag_start_y = -1;
static int last_drawn_x = -1;
static int last_drawn_y = -1;

/* Preview restore box */
static int preview_active = 0;
static int prev_min_x = 0, prev_min_y = 0, prev_max_x = 0, prev_max_y = 0;

/* Text tool interactive state */
static int text_mode_active = 0;
static int text_start_cx = 0;
static int text_start_cy = 0;
static int text_cursor = 0;
static char text_buf[32];

/* ================================================================ */
/*                     Helper Math Routines                         */
/* ================================================================ */
static inline int int_abs(int v) { return v < 0 ? -v : v; }
static inline int int_min(int a, int b) { return a < b ? a : b; }
static inline int int_max(int a, int b) { return a > b ? a : b; }

static int int_sqrt(int n) {
    if (n <= 0) return 0;
    int x = n;
    int y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

static int str_len(const char* s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static void str_copy(char* dest, const char* src, int max_len) {
    int i = 0;
    while (src && src[i] && i < max_len - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = 0;
}

/* ================================================================ */
/*                     Canvas Drawing Primitives                    */
/* ================================================================ */

static inline void canvas_put_pixel(int cx, int cy, uint8_t color) {
    if (cx < 0 || cx >= CANVAS_W || cy < 0 || cy >= CANVAS_H) return;
    canvas[cy * CANVAS_W + cx] = color;
    MODE13_VRAM[(CANVAS_Y + cy) * MODE13_WIDTH + (CANVAS_X + cx)] = color;
}

static inline uint8_t canvas_get_pixel(int cx, int cy) {
    if (cx < 0 || cx >= CANVAS_W || cy < 0 || cy >= CANVAS_H) return 0;
    return canvas[cy * CANVAS_W + cx];
}

static void canvas_draw_line(int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = int_abs(x1 - x0);
    int dy = -int_abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        canvas_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void canvas_draw_brush_pixel(int cx, int cy, uint8_t color, int radius) {
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= radius * radius + 1) {
                canvas_put_pixel(cx + dx, cy + dy, color);
            }
        }
    }
}

static void canvas_draw_brush_line(int x0, int y0, int x1, int y1, uint8_t color, int radius) {
    int dx = int_abs(x1 - x0);
    int dy = -int_abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        canvas_draw_brush_pixel(x0, y0, color, radius);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void canvas_draw_rect(int x0, int y0, int x1, int y1, uint8_t color, int filled) {
    int min_x = int_min(x0, x1);
    int max_x = int_max(x0, x1);
    int min_y = int_min(y0, y1);
    int max_y = int_max(y0, y1);

    if (filled) {
        for (int y = min_y; y <= max_y; y++) {
            for (int x = min_x; x <= max_x; x++) {
                canvas_put_pixel(x, y, color);
            }
        }
    } else {
        for (int x = min_x; x <= max_x; x++) {
            canvas_put_pixel(x, min_y, color);
            canvas_put_pixel(x, max_y, color);
        }
        for (int y = min_y; y <= max_y; y++) {
            canvas_put_pixel(min_x, y, color);
            canvas_put_pixel(max_x, y, color);
        }
    }
}

static void canvas_draw_circle(int xc, int yc, int r, uint8_t color, int filled) {
    if (r <= 0) {
        canvas_put_pixel(xc, yc, color);
        return;
    }

    int x = 0, y = r;
    int d = 3 - 2 * r;

    while (y >= x) {
        if (filled) {
            for (int i = xc - x; i <= xc + x; i++) {
                canvas_put_pixel(i, yc + y, color);
                canvas_put_pixel(i, yc - y, color);
            }
            for (int i = xc - y; i <= xc + y; i++) {
                canvas_put_pixel(i, yc + x, color);
                canvas_put_pixel(i, yc - x, color);
            }
        } else {
            canvas_put_pixel(xc + x, yc + y, color);
            canvas_put_pixel(xc - x, yc + y, color);
            canvas_put_pixel(xc + x, yc - y, color);
            canvas_put_pixel(xc - x, yc - y, color);
            canvas_put_pixel(xc + y, yc + x, color);
            canvas_put_pixel(xc - y, yc + x, color);
            canvas_put_pixel(xc + y, yc - x, color);
            canvas_put_pixel(xc - y, yc - x, color);
        }
        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
}

/* Fast Scanline Flood Fill */
static void canvas_flood_fill(int start_x, int start_y, uint8_t fill_color) {
    if (start_x < 0 || start_x >= CANVAS_W || start_y < 0 || start_y >= CANVAS_H) return;
    uint8_t target_color = canvas[start_y * CANVAS_W + start_x];
    if (target_color == fill_color) return;

    static uint16_t qx[1024];
    static uint16_t qy[1024];
    int head = 0, tail = 0;

    qx[tail] = start_x;
    qy[tail] = start_y;
    tail = (tail + 1) % 1024;

    while (head != tail) {
        int x = qx[head];
        int y = qy[head];
        head = (head + 1) % 1024;

        if (canvas[y * CANVAS_W + x] != target_color) continue;

        int x1 = x;
        while (x1 > 0 && canvas[y * CANVAS_W + (x1 - 1)] == target_color) x1--;
        int x2 = x;
        while (x2 < CANVAS_W - 1 && canvas[y * CANVAS_W + (x2 + 1)] == target_color) x2++;

        for (int i = x1; i <= x2; i++) {
            canvas[y * CANVAS_W + i] = fill_color;
            MODE13_VRAM[(CANVAS_Y + y) * MODE13_WIDTH + (CANVAS_X + i)] = fill_color;
        }

        for (int ny = y - 1; ny <= y + 1; ny += 2) {
            if (ny < 0 || ny >= CANVAS_H) continue;
            for (int i = x1; i <= x2; i++) {
                if (canvas[ny * CANVAS_W + i] == target_color) {
                    if ((tail + 1) % 1024 != head) {
                        qx[tail] = i;
                        qy[tail] = ny;
                        tail = (tail + 1) % 1024;
                    }
                    while (i <= x2 && canvas[ny * CANVAS_W + i] == target_color) i++;
                }
            }
        }
    }
}

/* ================================================================ */
/*                     Live Preview Routines                        */
/* ================================================================ */

static void preview_restore(void) {
    if (!preview_active) return;
    int x0 = int_max(0, prev_min_x);
    int x1 = int_min(CANVAS_W - 1, prev_max_x);
    int y0 = int_max(0, prev_min_y);
    int y1 = int_min(CANVAS_H - 1, prev_max_y);

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            MODE13_VRAM[(CANVAS_Y + y) * MODE13_WIDTH + (CANVAS_X + x)] = canvas[y * CANVAS_W + x];
        }
    }
    preview_active = 0;
}

static void preview_draw_shape(int x0, int y0, int x1, int y1, kaint_tool_t tool, uint8_t color) {
    preview_restore();

    int min_x = int_min(x0, x1);
    int max_x = int_max(x0, x1);
    int min_y = int_min(y0, y1);
    int max_y = int_max(y0, y1);

    if (tool == TOOL_LINE) {
        prev_min_x = min_x - 1; prev_max_x = max_x + 1;
        prev_min_y = min_y - 1; prev_max_y = max_y + 1;
        mode13_draw_line(CANVAS_X + x0, CANVAS_Y + y0, CANVAS_X + x1, CANVAS_Y + y1, color);
    } else if (tool == TOOL_RECT) {
        prev_min_x = min_x - 1; prev_max_x = max_x + 1;
        prev_min_y = min_y - 1; prev_max_y = max_y + 1;
        mode13_draw_rect(CANVAS_X + min_x, CANVAS_Y + min_y, max_x - min_x + 1, max_y - min_y + 1, color);
    } else if (tool == TOOL_FILL_RECT) {
        prev_min_x = min_x; prev_max_x = max_x;
        prev_min_y = min_y; prev_max_y = max_y;
        mode13_fill_rect(CANVAS_X + min_x, CANVAS_Y + min_y, max_x - min_x + 1, max_y - min_y + 1, color);
    } else if (tool == TOOL_CIRCLE || tool == TOOL_FILL_CIRCLE) {
        int dx = x1 - x0;
        int dy = y1 - y0;
        int r = int_sqrt(dx * dx + dy * dy);
        prev_min_x = x0 - r - 1; prev_max_x = x0 + r + 1;
        prev_min_y = y0 - r - 1; prev_max_y = y0 + r + 1;
        if (tool == TOOL_FILL_CIRCLE) {
            mode13_fill_circle(CANVAS_X + x0, CANVAS_Y + y0, r, color);
        } else {
            mode13_draw_circle(CANVAS_X + x0, CANVAS_Y + y0, r, color);
        }
    }

    preview_active = 1;
}

/* ================================================================ */
/*                     UI Drawing Elements                          */
/* ================================================================ */

static void draw_bevel_box(int x, int y, int w, int h, int sunken) {
    uint8_t top_left = sunken ? C_DARK_GREY : C_WHITE;
    uint8_t bot_right = sunken ? C_WHITE : C_DARK_GREY;

    mode13_fill_rect(x, y, w, h, C_LIGHT_GREY);
    for (int i = 0; i < w; i++) {
        mode13_put_pixel(x + i, y, top_left);
        mode13_put_pixel(x + i, y + h - 1, bot_right);
    }
    for (int j = 0; j < h; j++) {
        mode13_put_pixel(x, y + j, top_left);
        mode13_put_pixel(x + w - 1, y + j, bot_right);
    }
}

static void draw_tool_button(int col, int row, const char* label, int pressed) {
    int x = 2 + col * 23;
    int y = 16 + row * 17;
    int w = 22;
    int h = 15;

    draw_bevel_box(x, y, w, h, pressed);

    uint8_t text_col = pressed ? C_BLUE : C_BLACK;
    int tx = x + (pressed ? 2 : 1);
    int ty = y + (pressed ? 4 : 3);
    mode13_draw_string(tx, ty, label, text_col);
}

static void draw_toolbar_buttons(void) {
    static const char* const labels[8][2] = {
        { "Pen",  "Brsh" },
        { "Line", "Rect" },
        { "FRct", "Circ" },
        { "FCrc", "Fill" },
        { "Text", "Eras" },
        { "Pick", "Clr " },
        { "Save", "Open" },
        { "New ", "Exit" }
    };

    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 2; c++) {
            int tool_idx = r * 2 + c;
            int pressed = 0;
            if (tool_idx < TOOL_COUNT && (kaint_tool_t)tool_idx == current_tool) {
                pressed = 1;
            }
            draw_tool_button(c, r, labels[r][c], pressed);
        }
    }

    /* Primary and Secondary Color Swatches Preview */
    draw_bevel_box(2, 153, 45, 17, 1);

    /* Secondary color box (bottom-right) */
    mode13_draw_rect(22, 158, 14, 10, C_BLACK);
    mode13_fill_rect(23, 159, 12, 8, secondary_color);

    /* Primary color box (top-left) */
    mode13_draw_rect(6, 155, 14, 10, C_BLACK);
    mode13_fill_rect(7, 156, 12, 8, primary_color);

    mode13_draw_string(37, 158, "FG", C_BLACK);
}

static void draw_top_bar(int mouse_cx, int mouse_cy) {
    mode13_fill_rect(0, 0, MODE13_WIDTH, 14, C_BLUE);
    mode13_draw_line(0, 14, MODE13_WIDTH, 14, C_DARK_GREY);

    /* Title */
    mode13_draw_string(4, 3, "KAINT v1.0", C_YELLOW);

    /* File info */
    mode13_draw_string(80, 3, "File: ", C_WHITE);
    mode13_draw_string(120, 3, current_filename, C_LIGHT_CYAN);

    /* Tool name */
    mode13_draw_string(190, 3, "Tool:", C_WHITE);
    mode13_draw_string(225, 3, TOOL_NAMES[current_tool], C_LIGHT_GREEN);

    /* Coordinates */
    char coord_buf[16];
    if (mouse_cx >= 0 && mouse_cx < CANVAS_W && mouse_cy >= 0 && mouse_cy < CANVAS_H) {
        /* Format X: %3d Y: %3d */
        coord_buf[0] = 'X'; coord_buf[1] = ':';
        coord_buf[2] = '0' + (mouse_cx / 100) % 10;
        coord_buf[3] = '0' + (mouse_cx / 10) % 10;
        coord_buf[4] = '0' + (mouse_cx % 10);
        coord_buf[5] = ' '; coord_buf[6] = 'Y'; coord_buf[7] = ':';
        coord_buf[8] = '0' + (mouse_cy / 100) % 10;
        coord_buf[9] = '0' + (mouse_cy / 10) % 10;
        coord_buf[10] = '0' + (mouse_cy % 10);
        coord_buf[11] = 0;
    } else {
        str_copy(coord_buf, "X:--- Y:---", sizeof(coord_buf));
    }
    mode13_draw_string(268, 3, coord_buf, C_YELLOW);
}

static void draw_palette_bar(void) {
    draw_bevel_box(0, PALETTE_Y, MODE13_WIDTH, MODE13_HEIGHT - PALETTE_Y, 0);

    /* Color Swatches (2 rows of 16 colors) */
    for (int i = 0; i < 16; i++) {
        int x = 52 + i * 15;

        /* Row 1 */
        int y1 = 173;
        mode13_draw_rect(x, y1, 14, 9, C_BLACK);
        mode13_fill_rect(x + 1, y1 + 1, 12, 7, i);

        /* Row 2 */
        int y2 = 184;
        mode13_draw_rect(x, y2, 14, 9, C_BLACK);
        mode13_fill_rect(x + 1, y2 + 1, 12, 7, PALETTE_ROW2[i]);
    }

    /* Status line */
    mode13_fill_rect(2, 194, MODE13_WIDTH - 4, 6, C_LIGHT_GREY);
    mode13_draw_string(4, 194, status_msg, C_BLACK);
}

static void draw_canvas_frame(void) {
    /* Canvas recessed border */
    mode13_draw_rect(CANVAS_X - 1, CANVAS_Y - 1, CANVAS_W + 2, CANVAS_H + 2, C_DARK_GREY);
    mode13_draw_line(CANVAS_X - 1, CANVAS_Y + CANVAS_H + 1, CANVAS_X + CANVAS_W + 1, CANVAS_Y + CANVAS_H + 1, C_WHITE);
    mode13_draw_line(CANVAS_X + CANVAS_W + 1, CANVAS_Y - 1, CANVAS_X + CANVAS_W + 1, CANVAS_Y + CANVAS_H + 1, C_WHITE);

    /* Canvas drop shadow */
    mode13_fill_rect(CANVAS_X + CANVAS_W + 2, CANVAS_Y + 2, 2, CANVAS_H, C_DARK_GREY);
    mode13_fill_rect(CANVAS_X + 2, CANVAS_Y + CANVAS_H + 2, CANVAS_W + 2, 2, C_DARK_GREY);

    /* Blit canvas buffer to screen */
    for (int y = 0; y < CANVAS_H; y++) {
        for (int x = 0; x < CANVAS_W; x++) {
            MODE13_VRAM[(CANVAS_Y + y) * MODE13_WIDTH + (CANVAS_X + x)] = canvas[y * CANVAS_W + x];
        }
    }
}

static void draw_full_gui(int mouse_cx, int mouse_cy) {
    mode13_clear(C_LIGHT_GREY);
    draw_top_bar(mouse_cx, mouse_cy);
    draw_toolbar_buttons();
    draw_canvas_frame();
    draw_palette_bar();
}

static void update_status(const char* msg) {
    str_copy(status_msg, msg, sizeof(status_msg));
    mouse_hide_cursor();
    mode13_fill_rect(2, 194, MODE13_WIDTH - 4, 6, C_LIGHT_GREY);
    mode13_draw_string(4, 194, status_msg, C_BLACK);
    mouse_draw_cursor();
}

/* ================================================================ */
/*                     Modal Input Dialog                           */
/* ================================================================ */

static int prompt_filename(const char* prompt_title, char* out_name, int max_len) {
    int dlg_w = 200;
    int dlg_h = 50;
    int dlg_x = (MODE13_WIDTH - dlg_w) / 2;
    int dlg_y = (MODE13_HEIGHT - dlg_h) / 2;

    mouse_hide_cursor();
    draw_bevel_box(dlg_x, dlg_y, dlg_w, dlg_h, 0);
    mode13_fill_rect(dlg_x + 2, dlg_y + 2, dlg_w - 4, 12, C_BLUE);
    mode13_draw_string(dlg_x + 6, dlg_y + 4, prompt_title, C_WHITE);

    draw_bevel_box(dlg_x + 10, dlg_y + 20, dlg_w - 20, 14, 1);
    mode13_draw_string(dlg_x + 10, dlg_y + 38, "[Enter: OK  |  ESC: Cancel]", C_DARK_GREY);

    char edit_buf[KFS_NAME_MAX + 1];
    str_copy(edit_buf, out_name, sizeof(edit_buf));
    int edit_len = str_len(edit_buf);

    int confirmed = 0;

    while (1) {
        mode13_fill_rect(dlg_x + 12, dlg_y + 22, dlg_w - 24, 10, C_WHITE);
        mode13_draw_string(dlg_x + 14, dlg_y + 23, edit_buf, C_BLACK);
        mode13_draw_char(dlg_x + 14 + edit_len * 8, dlg_y + 23, '_', C_BLUE);

        if (keyboard_has_char()) {
            char c = keyboard_getchar();
            if (c == KEY_ENTER) {
                confirmed = 1;
                break;
            } else if (c == KEY_ESC) {
                confirmed = 0;
                break;
            } else if (c == KEY_BACKSPACE) {
                if (edit_len > 0) {
                    edit_buf[--edit_len] = 0;
                }
            } else if (c >= 32 && c <= 126 && edit_len < max_len - 1 && edit_len < KFS_NAME_MAX) {
                if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
                edit_buf[edit_len++] = c;
                edit_buf[edit_len] = 0;
            }
        }
    }

    if (confirmed && edit_len > 0) {
        str_copy(out_name, edit_buf, max_len);
    }

    /* Redraw full GUI */
    draw_full_gui(-1, -1);
    mouse_draw_cursor();
    return confirmed;
}

/* ================================================================ */
/*                     BMP File Format (8-bit 256c)                 */
/* ================================================================ */

#pragma pack(push, 1)
typedef struct {
    uint16_t bfType;          /* 0x4D42 ("BM") */
    uint32_t bfSize;          /* Total file size */
    uint16_t bfReserved1;     /* 0 */
    uint16_t bfReserved2;     /* 0 */
    uint32_t bfOffBits;       /* Offset to pixel array */
} BmpFileHeader;

typedef struct {
    uint32_t biSize;          /* 40 */
    int32_t  biWidth;         /* Width in pixels */
    int32_t  biHeight;        /* Height in pixels */
    uint16_t biPlanes;        /* 1 */
    uint16_t biBitCount;      /* 8 (256 colors) */
    uint32_t biCompression;   /* 0 (BI_RGB) */
    uint32_t biSizeImage;     /* Image size */
    int32_t  biXPelsPerMeter; /* 2835 */
    int32_t  biYPelsPerMeter; /* 2835 */
    uint32_t biClrUsed;       /* 256 */
    uint32_t biClrImportant;  /* 256 */
} BmpInfoHeader;

typedef struct {
    uint8_t rgbBlue;
    uint8_t rgbGreen;
    uint8_t rgbRed;
    uint8_t rgbReserved;
} BmpRgbQuad;
#pragma pack(pop)

/* Static export/import buffer for BMP serialization (37,078 bytes) */
static uint8_t bmp_buffer[sizeof(BmpFileHeader) + sizeof(BmpInfoHeader) + 1024 + CANVAS_W * CANVAS_H];

int kaint_save_bmp(const char* filename) {
    if (!filename || !filename[0]) return 0;

    uint32_t pixel_bytes = CANVAS_W * CANVAS_H;
    uint32_t total_size = sizeof(BmpFileHeader) + sizeof(BmpInfoHeader) + 1024 + pixel_bytes;

    BmpFileHeader* bfh = (BmpFileHeader*)bmp_buffer;
    bfh->bfType = 0x4D42; /* "BM" */
    bfh->bfSize = total_size;
    bfh->bfReserved1 = 0;
    bfh->bfReserved2 = 0;
    bfh->bfOffBits = sizeof(BmpFileHeader) + sizeof(BmpInfoHeader) + 1024;

    BmpInfoHeader* bih = (BmpInfoHeader*)(bmp_buffer + sizeof(BmpFileHeader));
    bih->biSize = sizeof(BmpInfoHeader);
    bih->biWidth = CANVAS_W;
    bih->biHeight = CANVAS_H; /* Positive = bottom to top */
    bih->biPlanes = 1;
    bih->biBitCount = 8;
    bih->biCompression = 0;
    bih->biSizeImage = pixel_bytes;
    bih->biXPelsPerMeter = 2835;
    bih->biYPelsPerMeter = 2835;
    bih->biClrUsed = 256;
    bih->biClrImportant = 256;

    /* Read hardware VGA DAC palette (0..255) */
    BmpRgbQuad* pal = (BmpRgbQuad*)(bmp_buffer + sizeof(BmpFileHeader) + sizeof(BmpInfoHeader));
    outb(0x3C7, 0);
    for (int i = 0; i < 256; i++) {
        uint8_t r6 = inb(0x3C9);
        uint8_t g6 = inb(0x3C9);
        uint8_t b6 = inb(0x3C9);
        pal[i].rgbRed   = (uint8_t)((r6 << 2) | (r6 >> 4));
        pal[i].rgbGreen = (uint8_t)((g6 << 2) | (g6 >> 4));
        pal[i].rgbBlue  = (uint8_t)((b6 << 2) | (b6 >> 4));
        pal[i].rgbReserved = 0;
    }

    /* Write pixel array bottom-up */
    uint8_t* dst_px = bmp_buffer + bfh->bfOffBits;
    for (int y = CANVAS_H - 1; y >= 0; y--) {
        for (int x = 0; x < CANVAS_W; x++) {
            *dst_px++ = canvas[y * CANVAS_W + x];
        }
    }

    /* Save to KirillFS */
    (void)kfs_touch(filename);
    kfs_result_t res = kfs_write_binary(filename, bmp_buffer, total_size);
    if (res == KFS_OK) {
        sound_soft_note(523, 40);
        sound_soft_note(659, 40);
        sound_soft_note(784, 80);
        return 1;
    }

    sound_soft_note(200, 100);
    return 0;
}

int kaint_load_bmp(const char* filename) {
    if (!filename || !filename[0]) return 0;

    const char* raw_data = 0;
    int size = 0;
    kfs_result_t res = kfs_read(filename, &raw_data, &size);
    if (res != KFS_OK || size < 54) return 0;

    const uint8_t* p = (const uint8_t*)raw_data;
    const BmpFileHeader* bfh = (const BmpFileHeader*)p;
    if (bfh->bfType != 0x4D42) return 0; /* Not 'BM' */

    const BmpInfoHeader* bih = (const BmpInfoHeader*)(p + sizeof(BmpFileHeader));
    int bmp_w = bih->biWidth;
    int bmp_h = bih->biHeight;
    int bottom_up = 1;
    if (bmp_h < 0) {
        bottom_up = 0;
        bmp_h = -bmp_h;
    }

    if (bih->biBitCount != 8) {
        return 0; /* Currently supports 8-bit indexed BMP */
    }

    /* Load Palette into DAC */
    const BmpRgbQuad* pal = (const BmpRgbQuad*)(p + sizeof(BmpFileHeader) + sizeof(BmpInfoHeader));
    outb(0x3C8, 0);
    for (int i = 0; i < 256; i++) {
        outb(0x3C9, pal[i].rgbRed >> 2);
        outb(0x3C9, pal[i].rgbGreen >> 2);
        outb(0x3C9, pal[i].rgbBlue >> 2);
    }

    /* Read Pixels */
    const uint8_t* src_px = p + bfh->bfOffBits;
    int row_stride = (bmp_w + 3) & ~3;

    /* Clear canvas first with palette 0 */
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) canvas[i] = 15;

    int copy_w = int_min(bmp_w, CANVAS_W);
    int copy_h = int_min(bmp_h, CANVAS_H);

    for (int y = 0; y < copy_h; y++) {
        int src_y = bottom_up ? (bmp_h - 1 - y) : y;
        const uint8_t* row = src_px + src_y * row_stride;
        for (int x = 0; x < copy_w; x++) {
            canvas[y * CANVAS_W + x] = row[x];
        }
    }

    sound_soft_note(659, 40);
    sound_soft_note(880, 80);
    return 1;
}

/* ================================================================ */
/*                       Main Interactive Loop                      */
/* ================================================================ */

void kaint_start(const char* initial_file) {
    /* 1. Enter VGA Mode 13h (320x200 256 colors) */
    mode13_enter();
    mouse_init();
    keyboard_init();

    /* 2. Initialize default state */
    primary_color = C_BLACK;
    secondary_color = C_WHITE;
    current_tool = TOOL_PENCIL;
    is_dragging = 0;
    preview_active = 0;
    text_mode_active = 0;

    /* Fill canvas with secondary color (default white) */
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) {
        canvas[i] = secondary_color;
    }

    if (initial_file && initial_file[0]) {
        str_copy(current_filename, initial_file, sizeof(current_filename));
        if (kaint_load_bmp(initial_file)) {
            str_copy(status_msg, "Loaded image successfully.", sizeof(status_msg));
        } else {
            str_copy(status_msg, "File not found. New canvas created.", sizeof(status_msg));
        }
    } else {
        str_copy(current_filename, "ART.BMP", sizeof(current_filename));
        str_copy(status_msg, "Kaint ready. Left-click to draw.", sizeof(status_msg));
    }

    mouse_set_position(CANVAS_X + CANVAS_W / 2, CANVAS_Y + CANVAS_H / 2);

    /* 3. Draw initial GUI */
    draw_full_gui(CANVAS_W / 2, CANVAS_H / 2);
    mouse_draw_cursor();

    sound_soft_note(440, 40);
    sound_soft_note(660, 40);
    sound_soft_note(880, 70);

    int prev_mouse_btn_l = 0;
    int prev_mouse_btn_r = 0;
    int last_reported_cx = -999;
    int last_reported_cy = -999;
    int blink_counter = 0;

    for (;;) {
        blink_counter++;

        /* -------------------------------------------------------- */
        /* A. Poll Hardware (Mouse & Keyboard)                      */
        /* -------------------------------------------------------- */
        mouse_poll();

        int cur_l = mouse_btn_left;
        int cur_r = mouse_btn_right;

        int cx = mouse_x - CANVAS_X;
        int cy = mouse_y - CANVAS_Y;
        int in_canvas = (cx >= 0 && cx < CANVAS_W && cy >= 0 && cy < CANVAS_H);

        /* Update header bar coordinates when mouse moves */
        if (in_canvas && (cx != last_reported_cx || cy != last_reported_cy)) {
            last_reported_cx = cx;
            last_reported_cy = cy;
            mouse_hide_cursor();
            draw_top_bar(cx, cy);
            mouse_draw_cursor();
        } else if (!in_canvas && last_reported_cx != -1) {
            last_reported_cx = -1;
            last_reported_cy = -1;
            mouse_hide_cursor();
            draw_top_bar(-1, -1);
            mouse_draw_cursor();
        }

        /* -------------------------------------------------------- */
        /* B. Text Mode Handling                                    */
        /* -------------------------------------------------------- */
        if (text_mode_active) {
            /* Blinking cursor */
            if ((blink_counter % 30) == 0) {
                mouse_hide_cursor();
                int tx = CANVAS_X + text_start_cx + text_cursor * 8;
                int ty = CANVAS_Y + text_start_cy;
                uint8_t cur_col = ((blink_counter / 30) % 2) ? primary_color : secondary_color;
                mode13_draw_char(tx, ty, '_', cur_col);
                mouse_draw_cursor();
            }

            if (keyboard_has_char()) {
                char ch = keyboard_getchar();
                if (ch == KEY_ENTER || ch == KEY_ESC) {
                    /* Commit text */
                    mouse_hide_cursor();
                    mode13_draw_char(CANVAS_X + text_start_cx + text_cursor * 8, CANVAS_Y + text_start_cy, ' ', secondary_color);
                    text_mode_active = 0;
                    update_status("Text placed on canvas.");
                    sound_soft_note(880, 30);
                    mouse_draw_cursor();
                } else if (ch == KEY_BACKSPACE) {
                    if (text_cursor > 0) {
                        mouse_hide_cursor();
                        text_cursor--;
                        for (int dy = 0; dy < 8; dy++) {
                            for (int dx = 0; dx < 8; dx++) {
                                canvas_put_pixel(text_start_cx + text_cursor * 8 + dx, text_start_cy + dy, secondary_color);
                            }
                        }
                        text_buf[text_cursor] = 0;
                        mouse_draw_cursor();
                    }
                } else if (ch >= 32 && ch <= 126 && text_cursor < 28 && (text_start_cx + (text_cursor + 1) * 8 < CANVAS_W)) {
                    mouse_hide_cursor();
                    int tx = text_start_cx + text_cursor * 8;
                    int ty = text_start_cy;
                    /* Draw onto canvas and screen */
                    mode13_draw_char(CANVAS_X + tx, CANVAS_Y + ty, ch, primary_color);
                    /* Copy 8x8 font pixels to canvas buffer */
                    for (int dy = 0; dy < 8; dy++) {
                        for (int dx = 0; dx < 8; dx++) {
                            uint8_t px = mode13_get_pixel(CANVAS_X + tx + dx, CANVAS_Y + ty + dy);
                            if (px == primary_color) {
                                canvas[(ty + dy) * CANVAS_W + (tx + dx)] = primary_color;
                            }
                        }
                    }
                    text_buf[text_cursor++] = ch;
                    text_buf[text_cursor] = 0;
                    sound_soft_note(1000, 10);
                    mouse_draw_cursor();
                }
            }

            /* Click outside terminates text mode */
            if ((cur_l && !prev_mouse_btn_l) || (cur_r && !prev_mouse_btn_r)) {
                mouse_hide_cursor();
                mode13_draw_char(CANVAS_X + text_start_cx + text_cursor * 8, CANVAS_Y + text_start_cy, ' ', secondary_color);
                text_mode_active = 0;
                update_status("Text committed.");
                mouse_draw_cursor();
            }
        }

        /* -------------------------------------------------------- */
        /* C. Mouse Button Down (Start Action)                      */
        /* -------------------------------------------------------- */
        else if ((cur_l && !prev_mouse_btn_l) || (cur_r && !prev_mouse_btn_r)) {
            int is_right = cur_r && !prev_mouse_btn_r;
            uint8_t active_draw_color = is_right ? secondary_color : primary_color;

            /* 1. Canvas Clicks */
            if (in_canvas) {
                if (current_tool == TOOL_PICKER) {
                    /* Color Picker Eyedropper */
                    uint8_t picked = canvas_get_pixel(cx, cy);
                    if (is_right) secondary_color = picked;
                    else primary_color = picked;

                    mouse_hide_cursor();
                    draw_toolbar_buttons();
                    current_tool = TOOL_PENCIL;
                    draw_top_bar(cx, cy);
                    update_status("Color picked.");
                    sound_soft_note(1200, 20);
                    mouse_draw_cursor();
                }
                else if (current_tool == TOOL_BUCKET) {
                    /* Flood Fill */
                    mouse_hide_cursor();
                    canvas_flood_fill(cx, cy, active_draw_color);
                    update_status("Area filled.");
                    sound_soft_note(450, 20);
                    sound_soft_note(600, 30);
                    mouse_draw_cursor();
                }
                else if (current_tool == TOOL_TEXT) {
                    /* Text tool activation */
                    text_mode_active = 1;
                    text_start_cx = cx;
                    text_start_cy = cy;
                    text_cursor = 0;
                    text_buf[0] = 0;
                    update_status("Text mode: type text, [Enter] finish.");
                    sound_soft_note(700, 20);
                }
                else {
                    /* Start Dragging */
                    is_dragging = 1;
                    drag_button = is_right ? 2 : 1;
                    drag_start_x = cx;
                    drag_start_y = cy;
                    last_drawn_x = cx;
                    last_drawn_y = cy;

                    mouse_hide_cursor();
                    if (current_tool == TOOL_PENCIL) {
                        canvas_put_pixel(cx, cy, active_draw_color);
                    } else if (current_tool == TOOL_BRUSH) {
                        canvas_draw_brush_pixel(cx, cy, active_draw_color, 1);
                    } else if (current_tool == TOOL_ERASER) {
                        canvas_draw_brush_pixel(cx, cy, secondary_color, 2);
                    }
                    mouse_draw_cursor();
                }
            }

            /* 2. Toolbar Button Clicks */
            else if (mouse_x >= 2 && mouse_x < 48 && mouse_y >= 16 && mouse_y < 152) {
                int col = (mouse_x - 2) / 23;
                int row = (mouse_y - 16) / 17;
                if (col >= 0 && col < 2 && row >= 0 && row < 8) {
                    int btn_id = row * 2 + col;
                    sound_soft_note(900, 15);

                    if (btn_id < TOOL_COUNT) {
                        current_tool = (kaint_tool_t)btn_id;
                        mouse_hide_cursor();
                        draw_toolbar_buttons();
                        draw_top_bar(cx, cy);
                        update_status(TOOL_NAMES[current_tool]);
                        mouse_draw_cursor();
                    } else if (btn_id == 11) {
                        /* Clr (Clear canvas) */
                        mouse_hide_cursor();
                        for (int i = 0; i < CANVAS_W * CANVAS_H; i++) canvas[i] = secondary_color;
                        draw_canvas_frame();
                        update_status("Canvas cleared.");
                        sound_soft_note(350, 40);
                        mouse_draw_cursor();
                    } else if (btn_id == 12) {
                        /* Save BMP */
                        if (prompt_filename("Save Canvas to BMP", current_filename, sizeof(current_filename))) {
                            if (kaint_save_bmp(current_filename)) {
                                update_status("Image saved to KirillFS.");
                            } else {
                                update_status("ERROR: Save failed!");
                            }
                        }
                    } else if (btn_id == 13) {
                        /* Open BMP */
                        if (prompt_filename("Load BMP from KirillFS", current_filename, sizeof(current_filename))) {
                            if (kaint_load_bmp(current_filename)) {
                                mouse_hide_cursor();
                                draw_canvas_frame();
                                update_status("Image loaded.");
                                mouse_draw_cursor();
                            } else {
                                update_status("ERROR: Could not load BMP!");
                            }
                        }
                    } else if (btn_id == 14) {
                        /* New Canvas */
                        mouse_hide_cursor();
                        for (int i = 0; i < CANVAS_W * CANVAS_H; i++) canvas[i] = secondary_color;
                        str_copy(current_filename, "UNTITLED.BMP", sizeof(current_filename));
                        draw_full_gui(cx, cy);
                        update_status("New canvas ready.");
                        sound_soft_note(600, 30);
                        mouse_draw_cursor();
                    } else if (btn_id == 15) {
                        /* Exit to Shell */
                        break;
                    }
                }
            }

            /* 3. Bottom Palette Clicks */
            else if (mouse_y >= 173 && mouse_y < 193 && mouse_x >= 52 && mouse_x < 52 + 16 * 15) {
                int col = (mouse_x - 52) / 15;
                if (col >= 0 && col < 16) {
                    uint8_t selected;
                    if (mouse_y < 184) {
                        selected = (uint8_t)col; /* Row 1 */
                    } else {
                        selected = PALETTE_ROW2[col]; /* Row 2 */
                    }

                    if (is_right) {
                        secondary_color = selected;
                        update_status("Secondary color changed.");
                    } else {
                        primary_color = selected;
                        update_status("Primary color changed.");
                    }

                    sound_soft_note(1100, 10);
                    mouse_hide_cursor();
                    draw_toolbar_buttons();
                    mouse_draw_cursor();
                }
            }
        }

        /* -------------------------------------------------------- */
        /* D. Mouse Dragging (In Progress)                          */
        /* -------------------------------------------------------- */
        else if (is_dragging && (cur_l || cur_r)) {
            uint8_t active_draw_color = (drag_button == 2) ? secondary_color : primary_color;
            int cur_cx = int_min(int_max(0, cx), CANVAS_W - 1);
            int cur_cy = int_min(int_max(0, cy), CANVAS_H - 1);

            if (current_tool == TOOL_PENCIL) {
                mouse_hide_cursor();
                canvas_draw_line(last_drawn_x, last_drawn_y, cur_cx, cur_cy, active_draw_color);
                last_drawn_x = cur_cx;
                last_drawn_y = cur_cy;
                mouse_draw_cursor();
            } else if (current_tool == TOOL_BRUSH) {
                mouse_hide_cursor();
                canvas_draw_brush_line(last_drawn_x, last_drawn_y, cur_cx, cur_cy, active_draw_color, 1);
                last_drawn_x = cur_cx;
                last_drawn_y = cur_cy;
                mouse_draw_cursor();
            } else if (current_tool == TOOL_ERASER) {
                mouse_hide_cursor();
                canvas_draw_brush_line(last_drawn_x, last_drawn_y, cur_cx, cur_cy, secondary_color, 2);
                last_drawn_x = cur_cx;
                last_drawn_y = cur_cy;
                mouse_draw_cursor();
            } else {
                /* Shape Preview */
                mouse_hide_cursor();
                preview_draw_shape(drag_start_x, drag_start_y, cur_cx, cur_cy, current_tool, active_draw_color);
                mouse_draw_cursor();
            }
        }

        /* -------------------------------------------------------- */
        /* E. Mouse Button Released (Commit Shape)                  */
        /* -------------------------------------------------------- */
        else if (is_dragging && !cur_l && !cur_r) {
            uint8_t active_draw_color = (drag_button == 2) ? secondary_color : primary_color;
            int cur_cx = int_min(int_max(0, cx), CANVAS_W - 1);
            int cur_cy = int_min(int_max(0, cy), CANVAS_H - 1);

            mouse_hide_cursor();
            preview_restore();

            if (current_tool == TOOL_LINE) {
                canvas_draw_line(drag_start_x, drag_start_y, cur_cx, cur_cy, active_draw_color);
            } else if (current_tool == TOOL_RECT) {
                canvas_draw_rect(drag_start_x, drag_start_y, cur_cx, cur_cy, active_draw_color, 0);
            } else if (current_tool == TOOL_FILL_RECT) {
                canvas_draw_rect(drag_start_x, drag_start_y, cur_cx, cur_cy, active_draw_color, 1);
            } else if (current_tool == TOOL_CIRCLE) {
                int dx = cur_cx - drag_start_x;
                int dy = cur_cy - drag_start_y;
                int r = int_sqrt(dx * dx + dy * dy);
                canvas_draw_circle(drag_start_x, drag_start_y, r, active_draw_color, 0);
            } else if (current_tool == TOOL_FILL_CIRCLE) {
                int dx = cur_cx - drag_start_x;
                int dy = cur_cy - drag_start_y;
                int r = int_sqrt(dx * dx + dy * dy);
                canvas_draw_circle(drag_start_x, drag_start_y, r, active_draw_color, 1);
            }

            is_dragging = 0;
            sound_soft_note(700, 10);
            mouse_draw_cursor();
        }

        prev_mouse_btn_l = cur_l;
        prev_mouse_btn_r = cur_r;

        /* Move cursor on screen */
        mouse_update();

        /* -------------------------------------------------------- */
        /* F. Keyboard Hotkeys                                      */
        /* -------------------------------------------------------- */
        if (!text_mode_active && keyboard_has_char()) {
            char k = keyboard_getchar();
            if (k == KEY_ESC || k == 'q' || k == 'Q') {
                break;
            } else if (k == 'p' || k == 'P') {
                current_tool = TOOL_PENCIL;
                mouse_hide_cursor(); draw_toolbar_buttons(); draw_top_bar(cx, cy); update_status("Tool: Pencil"); mouse_draw_cursor();
            } else if (k == 'b' || k == 'B') {
                current_tool = TOOL_BRUSH;
                mouse_hide_cursor(); draw_toolbar_buttons(); draw_top_bar(cx, cy); update_status("Tool: Brush"); mouse_draw_cursor();
            } else if (k == 'l' || k == 'L') {
                current_tool = TOOL_LINE;
                mouse_hide_cursor(); draw_toolbar_buttons(); draw_top_bar(cx, cy); update_status("Tool: Line"); mouse_draw_cursor();
            } else if (k == 'r' || k == 'R') {
                current_tool = TOOL_RECT;
                mouse_hide_cursor(); draw_toolbar_buttons(); draw_top_bar(cx, cy); update_status("Tool: Rectangle"); mouse_draw_cursor();
            } else if (k == 'f' || k == 'F') {
                current_tool = TOOL_FILL_RECT;
                mouse_hide_cursor(); draw_toolbar_buttons(); draw_top_bar(cx, cy); update_status("Tool: Filled Rect"); mouse_draw_cursor();
            } else if (k == 'c' || k == 'C') {
                current_tool = TOOL_CIRCLE;
                mouse_hide_cursor(); draw_toolbar_buttons(); draw_top_bar(cx, cy); update_status("Tool: Circle"); mouse_draw_cursor();
            } else if (k == 'o' || k == 'O') {
                current_tool = TOOL_FILL_CIRCLE;
                mouse_hide_cursor(); draw_toolbar_buttons(); draw_top_bar(cx, cy); update_status("Tool: Filled Circle"); mouse_draw_cursor();
            } else if (k == 'k' || k == 'K') {
                current_tool = TOOL_BUCKET;
                mouse_hide_cursor(); draw_toolbar_buttons(); draw_top_bar(cx, cy); update_status("Tool: Bucket Fill"); mouse_draw_cursor();
            } else if (k == 't' || k == 'T') {
                current_tool = TOOL_TEXT;
                mouse_hide_cursor(); draw_toolbar_buttons(); draw_top_bar(cx, cy); update_status("Tool: Text (click canvas)"); mouse_draw_cursor();
            } else if (k == 'e' || k == 'E') {
                current_tool = TOOL_ERASER;
                mouse_hide_cursor(); draw_toolbar_buttons(); draw_top_bar(cx, cy); update_status("Tool: Eraser"); mouse_draw_cursor();
            } else if (k == 'i' || k == 'I') {
                current_tool = TOOL_PICKER;
                mouse_hide_cursor(); draw_toolbar_buttons(); draw_top_bar(cx, cy); update_status("Tool: Picker Eyedropper"); mouse_draw_cursor();
            } else if (k == 's' || k == 'S') {
                if (prompt_filename("Save Canvas to BMP", current_filename, sizeof(current_filename))) {
                    if (kaint_save_bmp(current_filename)) {
                        update_status("Image saved to KirillFS.");
                    } else {
                        update_status("ERROR: Save failed!");
                    }
                }
            } else if (k == 'n' || k == 'N') {
                mouse_hide_cursor();
                for (int i = 0; i < CANVAS_W * CANVAS_H; i++) canvas[i] = secondary_color;
                str_copy(current_filename, "UNTITLED.BMP", sizeof(current_filename));
                draw_full_gui(cx, cy);
                update_status("New canvas ready.");
                sound_soft_note(600, 30);
                mouse_draw_cursor();
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

void kaint_init_default_bmp(void) {
    if (kfs_touch("demo.bmp") == KFS_OK) {
        /* Clear canvas buffer to white */
        for (int i = 0; i < CANVAS_W * CANVAS_H; i++) canvas[i] = C_WHITE;

        /* Blue header card */
        for (int y = 8; y <= 32; y++) {
            for (int x = 8; x <= CANVAS_W - 9; x++) {
                canvas[y * CANVAS_W + x] = C_BLUE;
            }
        }

        /* 16 color test strips */
        for (int i = 0; i < 16; i++) {
            int x1 = 12 + i * 13;
            int x2 = x1 + 11;
            for (int y = 40; y <= 65; y++) {
                for (int x = x1; x <= x2; x++) {
                    canvas[y * CANVAS_W + x] = (uint8_t)i;
                }
            }
            for (int y = 70; y <= 95; y++) {
                for (int x = x1; x <= x2; x++) {
                    canvas[y * CANVAS_W + x] = PALETTE_ROW2[i];
                }
            }
        }

        /* Sample colored circles and box */
        canvas_draw_circle(36, 122, 16, C_RED, 1);
        canvas_draw_circle(36, 122, 16, C_BLACK, 0);

        canvas_draw_circle(84, 122, 16, C_GREEN, 1);
        canvas_draw_circle(84, 122, 16, C_BLACK, 0);

        canvas_draw_circle(132, 122, 16, C_LIGHT_CYAN, 1);
        canvas_draw_circle(132, 122, 16, C_BLACK, 0);

        for (int y = 106; y <= 138; y++) {
            for (int x = 160; x <= 220; x++) {
                canvas[y * CANVAS_W + x] = C_YELLOW;
            }
        }
        for (int x = 160; x <= 220; x++) {
            canvas[106 * CANVAS_W + x] = C_BLACK;
            canvas[138 * CANVAS_W + x] = C_BLACK;
        }
        for (int y = 106; y <= 138; y++) {
            canvas[y * CANVAS_W + 160] = C_BLACK;
            canvas[y * CANVAS_W + 220] = C_BLACK;
        }

        kaint_save_bmp("demo.bmp");
    }
}
