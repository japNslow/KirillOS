#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

#define KEY_ENTER      '\n'
#define KEY_BACKSPACE  '\b'
#define KEY_TAB        '\t'
#define KEY_ESC        27
#define KEY_SAVE       0x13   /* Ctrl+S */
#define KEY_QUIT       0x11   /* Ctrl+Q */
#define KEY_PLAY       0x10   /* Ctrl+P */

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
char    keyboard_getchar(void);       /* blocking read */
int     keyboard_has_char(void);
char    keyboard_poll(void);          /* 0 if empty */
void    keyboard_handle_scancode(uint8_t sc);

#endif