#include "vga.h"

static uint8_t  color = 0;
static size_t   row = 0;
static size_t   col = 0;

static inline uint8_t make_color(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}

static inline uint16_t make_entry(char c, uint8_t col) {
    return (uint16_t)c | ((uint16_t)col << 8);
}

void vga_init(void) {
    color = make_color(VGA_LIGHT_GREY, VGA_BLACK);
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    color = make_color(fg, bg);
}

void vga_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[y * VGA_WIDTH + x] = make_entry(' ', color);
    row = 0;
    col = 0;
}

static void scroll(void) {
    if (row < VGA_HEIGHT) return;
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[y * VGA_WIDTH + x] = VGA_MEMORY[(y + 1) * VGA_WIDTH + x];
    for (size_t x = 0; x < VGA_WIDTH; x++)
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = make_entry(' ', color);
    row = VGA_HEIGHT - 1;
}

void vga_putchar(char c) {
    if (c == '\n') { vga_newline(); return; }
    if (c == '\r') { col = 0; return; }
    if (c == '\b') { vga_backspace(); return; }
    VGA_MEMORY[row * VGA_WIDTH + col] = make_entry(c, color);
    if (++col >= VGA_WIDTH) { col = 0; row++; scroll(); }
}

void vga_write(const char* str) {
    while (*str) vga_putchar(*str++);
}

void vga_newline(void) {
    col = 0;
    row++;
    scroll();
}

void vga_backspace(void) {
    if (col == 0 && row == 0) return;
    if (col == 0) { row--; col = VGA_WIDTH - 1; }
    else col--;
    VGA_MEMORY[row * VGA_WIDTH + col] = make_entry(' ', color);
}

void vga_write_hex(uint32_t value, int digits) {
    static const char hex[] = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; i--)
        vga_putchar(hex[(value >> (i * 4)) & 0xF]);
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void vga_set_cursor(size_t r, size_t c) {
    if (r >= VGA_HEIGHT) r = VGA_HEIGHT - 1;
    if (c >= VGA_WIDTH) c = VGA_WIDTH - 1;
    row = r;
    col = c;
    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}
