#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

#define KEY_ENTER      '\n'
#define KEY_BACKSPACE  '\b'
#define KEY_TAB        '\t'
#define KEY_ESC        27
/* Control Keys (ASCII 1-26) */
#define KEY_CTRL_A     1
#define KEY_CTRL_B     2
#define KEY_CTRL_C     3
#define KEY_CTRL_D     4
#define KEY_CTRL_E     5
#define KEY_CTRL_F     6
#define KEY_CTRL_G     7
#define KEY_CTRL_H     8
#define KEY_CTRL_I     9
#define KEY_CTRL_J     10
#define KEY_CTRL_K     11
#define KEY_CTRL_L     12
#define KEY_CTRL_M     13
#define KEY_CTRL_N     14
#define KEY_CTRL_O     15
#define KEY_CTRL_P     16
#define KEY_CTRL_Q     17
#define KEY_CTRL_R     18
#define KEY_CTRL_S     19
#define KEY_CTRL_T     20
#define KEY_CTRL_U     21
#define KEY_CTRL_V     22
#define KEY_CTRL_W     23
#define KEY_CTRL_X     24
#define KEY_CTRL_Y     25
#define KEY_CTRL_Z     26

#define KEY_SAVE       KEY_CTRL_S   /* Ctrl+S = 0x13 */
#define KEY_QUIT       KEY_CTRL_Q   /* Ctrl+Q = 0x11 */
#define KEY_PLAY       KEY_CTRL_P   /* Ctrl+P = 0x10 */


/* Extended navigation keys */
#define KEY_UP         ((char)0x80)
#define KEY_DOWN       ((char)0x81)
#define KEY_LEFT       ((char)0x82)
#define KEY_RIGHT      ((char)0x83)
#define KEY_HOME       ((char)0x84)
#define KEY_END        ((char)0x85)
#define KEY_PGUP       ((char)0x86)
#define KEY_PGDN       ((char)0x87)
#define KEY_INS        ((char)0x88)
#define KEY_DEL        ((char)0x89)

void    keyboard_init(void);
void    keyboard_check_hardware(void);
char    keyboard_getchar(void);       /* blocking read */
int     keyboard_has_char(void);
char    keyboard_poll(void);          /* 0 if empty */
void    keyboard_handle_scancode(uint8_t sc);

#endif