#ifndef VGA_H
#define VGA_H
#include <stddef.h>
#include <stdint.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((uint16_t*)0xB8000)

enum vga_color {
    VGA_BLACK = 0, VGA_BLUE, VGA_GREEN, VGA_CYAN,
    VGA_RED, VGA_MAGENTA, VGA_BROWN, VGA_LIGHT_GREY,
    VGA_DARK_GREY, VGA_LIGHT_BLUE, VGA_LIGHT_GREEN,
    VGA_LIGHT_CYAN, VGA_LIGHT_RED, VGA_LIGHT_MAGENTA,
    VGA_LIGHT_BROWN, VGA_WHITE,
};
#define VGA_YELLOW VGA_LIGHT_BROWN

void vga_init(void);
void vga_clear(void);
void vga_putchar(char c);
void vga_write(const char* str);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_backspace(void);
void vga_newline(void);
void vga_write_hex(uint32_t value, int digits);
void vga_set_cursor(size_t r, size_t c);

#endif