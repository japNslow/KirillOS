#ifndef KIRILLOS_MOUSE_H
#define KIRILLOS_MOUSE_H

#include <stdint.h>

#define MOUSE_CURSOR_W 11
#define MOUSE_CURSOR_H 16

/* ================================================================ */
/*                     Mouse State & Coordinates                    */
/* ================================================================ */
extern int mouse_x;
extern int mouse_y;
extern int mouse_btn_left;
extern int mouse_btn_right;
extern int mouse_btn_middle;

/* ================================================================ */
/*                     Hardware Driver Interface                    */
/* ================================================================ */

/**
 * Initializes the PS/2 auxiliary controller (8042) and PS/2 mouse:
 * - Enables auxiliary device (0xA8)
 * - Sets command byte to enable mouse interrupts & clock (0x20/0x60)
 * - Sets default settings (0xF6)
 * - Enables data reporting (0xF4)
 * Returns 1 on success, 0 on failure.
 */
int mouse_init(void);

/**
 * Disables PS/2 mouse stream reporting (0xF5) and auxiliary port clock (0xA7).
 * Flushes buffer and resets packet cycle.
 */
void mouse_disable(void);

/**
 * Feeds a single byte from the 8042 auxiliary port into the mouse packet state machine.
 */
void mouse_handle_byte(uint8_t data);

/**
 * Non-blocking poll for incoming PS/2 data packets.
 * Forwards keyboard data to keyboard driver.
 * Updates mouse_x, mouse_y, and button states upon full 3-byte packet.
 * Returns 1 if mouse state updated, 0 otherwise.
 */
int mouse_poll(void);

/**
 * Sets current mouse pointer coordinates (clamped to 320x200).
 */
void mouse_set_position(int x, int y);

/* ================================================================ */
/*              Mode 13h (320x200 256-color) Cursor Rendering       */
/* ================================================================ */

/**
 * Draws the 11x16 arrow cursor at current (mouse_x, mouse_y),
 * saving the background pixels underneath.
 */
void mouse_draw_cursor(void);

/**
 * Restores original background pixels, hiding the cursor.
 */
void mouse_hide_cursor(void);

/**
 * Updates cursor position on screen without leaving visual artifacts:
 * hides previous cursor, restores background, and draws at new position.
 */
void mouse_update(void);

/**
 * Returns 1 if cursor is currently visible on screen, 0 otherwise.
 */
int mouse_is_visible(void);

#endif /* KIRILLOS_MOUSE_H */
