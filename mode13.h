#ifndef MODE13_H
#define MODE13_H

#include <stdint.h>
#include <stddef.h>

#define MODE13_WIDTH  320
#define MODE13_HEIGHT 200
#define MODE13_VRAM   ((uint8_t*)0xA0000)

void mode13_init(void);
void mode13_enter(void);
void mode13_exit(void);

void mode13_clear(uint8_t color);
void mode13_put_pixel(int x, int y, uint8_t color);
uint8_t mode13_get_pixel(int x, int y);

void mode13_draw_line(int x0, int y0, int x1, int y1, uint8_t color);
void mode13_draw_rect(int x, int y, int w, int h, uint8_t color);
void mode13_fill_rect(int x, int y, int w, int h, uint8_t color);
void mode13_draw_circle(int xc, int yc, int r, uint8_t color);
void mode13_fill_circle(int xc, int yc, int r, uint8_t color);

void mode13_draw_char(int x, int y, char c, uint8_t color);
void mode13_draw_string(int x, int y, const char* str, uint8_t color);

void mode13_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void mode13_demo(void);

#endif
