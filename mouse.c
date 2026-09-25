#include "mouse.h"
#include "mode13.h"
#include "keyboard.h"
#include <stdint.h>
#include <stddef.h>

/* ================================================================ */
/*                     8042 Controller Ports                        */
/* ================================================================ */
#define KBD_STATUS_PORT 0x64
#define KBD_CMD_PORT    0x64
#define KBD_DATA_PORT   0x60

/* ================================================================ */
/*                     Mouse Global State                           */
/* ================================================================ */
int mouse_x = 160;
int mouse_y = 100;
int mouse_btn_left = 0;
int mouse_btn_right = 0;
int mouse_btn_middle = 0;

static uint8_t mouse_cycle = 0;
static uint8_t mouse_packet[3];

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

static inline void io_wait(void) {
    outb(0x80, 0);
}

static void mouse_wait_write(void) {
    uint32_t timeout = 100000;
    while ((inb(KBD_STATUS_PORT) & 0x02) && --timeout) {
        io_wait();
    }
}

static int mouse_wait_read(void) {
    uint32_t timeout = 100000;
    while (!(inb(KBD_STATUS_PORT) & 0x01) && --timeout) {
        io_wait();
    }
    return timeout > 0;
}

static uint8_t mouse_write_device(uint8_t val) {
    mouse_wait_write();
    outb(KBD_CMD_PORT, 0xD4);
    mouse_wait_write();
    outb(KBD_DATA_PORT, val);
    if (mouse_wait_read()) {
        return inb(KBD_DATA_PORT);
    }
    return 0;
}

/* ================================================================ */
/*                 Driver Initialization & Polling                  */
/* ================================================================ */

int mouse_init(void) {
    mouse_x = MODE13_WIDTH / 2;
    mouse_y = MODE13_HEIGHT / 2;
    mouse_btn_left = 0;
    mouse_btn_right = 0;
    mouse_btn_middle = 0;
    mouse_cycle = 0;

    // Drain any leftover bytes in controller data buffer
    for (int i = 0; i < 32; i++) {
        if (inb(KBD_STATUS_PORT) & 0x01) {
            (void)inb(KBD_DATA_PORT);
        } else {
            break;
        }
    }

    // 1. Enable auxiliary device (PS/2 mouse port)
    mouse_wait_write();
    outb(KBD_CMD_PORT, 0xA8);

    // 2. Read Controller Configuration Byte (command 0x20)
    mouse_wait_write();
    outb(KBD_CMD_PORT, 0x20);
    if (!mouse_wait_read()) return 0;
    uint8_t status = inb(KBD_DATA_PORT);

    // 3. Set bit 1 (enable mouse IRQ12), clear bit 5 (disable clock -> 0 enables clock)
    status |= 0x02;
    status &= ~0x20;

    // 4. Write Controller Configuration Byte back (command 0x60)
    mouse_wait_write();
    outb(KBD_CMD_PORT, 0x60);
    mouse_wait_write();
    outb(KBD_DATA_PORT, status);

    // 5. Reset mouse to default settings (0xF6)
    mouse_write_device(0xF6);

    // 6. Enable Data Reporting (0xF4)
    mouse_write_device(0xF4);

    // Drain any remaining ACK bytes
    for (int i = 0; i < 16; i++) {
        if (inb(KBD_STATUS_PORT) & 0x01) {
            (void)inb(KBD_DATA_PORT);
        } else {
            break;
        }
    }

    return 1;
}

int mouse_poll(void) {
    int updated = 0;

    while (inb(KBD_STATUS_PORT) & 0x01) {
        uint8_t status = inb(KBD_STATUS_PORT);
        uint8_t data = inb(KBD_DATA_PORT);

        // Bit 5: 1 = auxiliary device (mouse), 0 = keyboard
        if (!(status & 0x20)) {
            // Forward keyboard scancode to keyboard driver so typing remains intact
            keyboard_handle_scancode(data);
            continue;
        }

        switch (mouse_cycle) {
            case 0:
                // Bit 3 of byte 0 MUST be 1 in standard PS/2 mouse packet header
                if (!(data & 0x08)) {
                    break;
                }
                mouse_packet[0] = data;
                mouse_cycle = 1;
                break;

            case 1:
                mouse_packet[1] = data;
                mouse_cycle = 2;
                break;

            case 2:
                mouse_packet[2] = data;
                mouse_cycle = 0;

                uint8_t flags = mouse_packet[0];

                // Buttons
                mouse_btn_left   = (flags & 0x01) ? 1 : 0;
                mouse_btn_right  = (flags & 0x02) ? 1 : 0;
                mouse_btn_middle = (flags & 0x04) ? 1 : 0;

                // X movement: bit 4 is sign bit
                int dx = (int)mouse_packet[1];
                if (flags & 0x10) {
                    dx -= 256;
                }

                // Y movement: bit 5 is sign bit
                int dy = (int)mouse_packet[2];
                if (flags & 0x20) {
                    dy -= 256;
                }

                // Discard movement on overflow
                if (flags & 0x40) dx = 0;
                if (flags & 0x80) dy = 0;

                // Update position (in PS/2 positive dy is UP, on screen Y increases DOWN)
                mouse_x += dx;
                mouse_y -= dy;

                // Clamp to screen boundaries (320x200 Mode 13h)
                if (mouse_x < 0) mouse_x = 0;
                if (mouse_x >= MODE13_WIDTH) mouse_x = MODE13_WIDTH - 1;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_y >= MODE13_HEIGHT) mouse_y = MODE13_HEIGHT - 1;

                updated = 1;
                break;
        }
    }

    return updated;
}

/* ================================================================ */
/*             Mode 13h Cursor Rendering & Background Buffer        */
/* ================================================================ */

/*
 * Classic 11x16 GUI Arrow Cursor:
 * 0 = Transparent
 * 1 = Black border (color 0)
 * 2 = White interior (color 15)
 */
static const uint8_t cursor_shape[MOUSE_CURSOR_H][MOUSE_CURSOR_W] = {
    { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0 },
    { 1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0 },
    { 1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0 },
    { 1, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0 },
    { 1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0 },
    { 1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0 },
    { 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0 },
    { 1, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1 },
    { 1, 2, 2, 1, 2, 2, 1, 0, 0, 0, 0 },
    { 1, 2, 1, 0, 1, 2, 2, 1, 0, 0, 0 },
    { 1, 1, 0, 0, 0, 1, 2, 2, 1, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0 }
};

static uint8_t cursor_bg[MOUSE_CURSOR_H][MOUSE_CURSOR_W];
static int cursor_saved_x = -1;
static int cursor_saved_y = -1;
static int cursor_visible = 0;

void mouse_draw_cursor(void) {
    if (cursor_visible) {
        mouse_hide_cursor();
    }

    cursor_saved_x = mouse_x;
    cursor_saved_y = mouse_y;

    // 1. Save background pixels under the cursor
    for (int cy = 0; cy < MOUSE_CURSOR_H; cy++) {
        for (int cx = 0; cx < MOUSE_CURSOR_W; cx++) {
            int px = cursor_saved_x + cx;
            int py = cursor_saved_y + cy;
            if (px >= 0 && px < MODE13_WIDTH && py >= 0 && py < MODE13_HEIGHT) {
                cursor_bg[cy][cx] = MODE13_VRAM[py * MODE13_WIDTH + px];
            } else {
                cursor_bg[cy][cx] = 0;
            }
        }
    }

    // 2. Draw cursor pixels (white interior with black border)
    for (int cy = 0; cy < MOUSE_CURSOR_H; cy++) {
        for (int cx = 0; cx < MOUSE_CURSOR_W; cx++) {
            uint8_t shape = cursor_shape[cy][cx];
            if (shape == 0) continue; // Transparent

            int px = cursor_saved_x + cx;
            int py = cursor_saved_y + cy;
            if (px >= 0 && px < MODE13_WIDTH && py >= 0 && py < MODE13_HEIGHT) {
                uint8_t color = (shape == 2) ? 15 : 0; // 15=White, 0=Black
                MODE13_VRAM[py * MODE13_WIDTH + px] = color;
            }
        }
    }

    cursor_visible = 1;
}

void mouse_hide_cursor(void) {
    if (!cursor_visible) return;

    // Restore background pixels
    for (int cy = 0; cy < MOUSE_CURSOR_H; cy++) {
        for (int cx = 0; cx < MOUSE_CURSOR_W; cx++) {
            uint8_t shape = cursor_shape[cy][cx];
            if (shape == 0) continue; // Only restore pixels modified by cursor

            int px = cursor_saved_x + cx;
            int py = cursor_saved_y + cy;
            if (px >= 0 && px < MODE13_WIDTH && py >= 0 && py < MODE13_HEIGHT) {
                MODE13_VRAM[py * MODE13_WIDTH + px] = cursor_bg[cy][cx];
            }
        }
    }

    cursor_visible = 0;
}

void mouse_update(void) {
    if (!cursor_visible) {
        mouse_draw_cursor();
    } else if (cursor_saved_x != mouse_x || cursor_saved_y != mouse_y) {
        mouse_hide_cursor();
        mouse_draw_cursor();
    }
}

void mouse_set_position(int x, int y) {
    if (x < 0) x = 0;
    if (x >= MODE13_WIDTH) x = MODE13_WIDTH - 1;
    if (y < 0) y = 0;
    if (y >= MODE13_HEIGHT) y = MODE13_HEIGHT - 1;

    if (cursor_visible) {
        mouse_hide_cursor();
        mouse_x = x;
        mouse_y = y;
        mouse_draw_cursor();
    } else {
        mouse_x = x;
        mouse_y = y;
    }
}

int mouse_is_visible(void) {
    return cursor_visible;
}
