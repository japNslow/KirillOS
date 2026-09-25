#include "keyboard.h"
#include <stdint.h>

#define KB_DATA   0x60
#define KB_STATUS 0x64

static const char kbd_us[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/',
    0,  '*', 0,  ' ',
};

static const char kbd_us_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
    0,  '|','Z','X','C','V','B','N','M','<','>','?',
    0,  '*', 0,  ' ',
};

#define BUF_SIZE 256
static volatile char     buf[BUF_SIZE];
static volatile uint32_t buf_head = 0;
static volatile uint32_t buf_tail = 0;
static int shift = 0;
static int control = 0;
static int extended = 0;

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void push_char(char c) {
    uint32_t next = (buf_head + 1) % BUF_SIZE;
    if (next == buf_tail) return;   /* buffer full */
    buf[buf_head] = c;
    buf_head = next;
}

void keyboard_handle_scancode(uint8_t sc) {
    if (sc == 0xE0) {
        extended = 1;
        return;
    }

    if (sc & 0x80) { /* release */
        uint8_t rel = sc & 0x7F;
        if (!extended) {
            if (rel == 0x2A || rel == 0x36) shift = 0;
            if (rel == 0x1D) control = 0;
        } else {
            if (rel == 0x1D) control = 0; /* Right Ctrl */
        }
        extended = 0;
        return;
    }

    if (extended) {
        extended = 0;
        if (sc == 0x1D) { control = 1; return; } /* Right Ctrl */
        if (sc == 0x48) { push_char(KEY_UP); return; }
        if (sc == 0x50) { push_char(KEY_DOWN); return; }
        if (sc == 0x4B) { push_char(KEY_LEFT); return; }
        if (sc == 0x4D) { push_char(KEY_RIGHT); return; }
        if (sc == 0x47) { push_char(KEY_HOME); return; }
        if (sc == 0x4F) { push_char(KEY_END); return; }
        if (sc == 0x49) { push_char(KEY_PGUP); return; }
        if (sc == 0x51) { push_char(KEY_PGDN); return; }
        if (sc == 0x52) { push_char(KEY_INS); return; }
        if (sc == 0x53) { push_char(KEY_DEL); return; }
        return;
    }

    if (sc == 0x2A || sc == 0x36) { shift = 1; return; }
    if (sc == 0x1D) { control = 1; return; }
    if (sc == 0x01) { push_char(KEY_ESC); return; }

    char c = shift ? kbd_us_shift[sc] : kbd_us[sc];
    if (control) {
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 1);
        else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 1);
        else if (c == '\\' || c == '|') c = 28; /* Ctrl+\ (FS) */
    }
    if (c) push_char(c);
}

void keyboard_init(void) {
    while (inb(KB_STATUS) & 0x02);
}

static void keyboard_check_hardware(void) {
    while (inb(KB_STATUS) & 0x01) {
        uint8_t sc = inb(KB_DATA);
        keyboard_handle_scancode(sc);
    }
}

int keyboard_has_char(void) {
    keyboard_check_hardware();
    return buf_head != buf_tail;
}

char keyboard_poll(void) {
    keyboard_check_hardware();
    if (buf_head == buf_tail) return 0;
    char c = buf[buf_tail];
    buf_tail = (buf_tail + 1) % BUF_SIZE;
    return c;
}

char keyboard_getchar(void) {
    while (1) {
        keyboard_check_hardware();
        if (buf_head != buf_tail)
            return keyboard_poll();
    }
}