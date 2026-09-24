#ifndef KHEX_H
#define KHEX_H

#include <stdint.h>
#include <stddef.h>

#define KHEX_MAGIC 0x4B484558   /* 'KHEX' in little endian */
#define KHEX_LOAD_ADDR 0x50000

/* System API passed to executable binaries */
typedef struct {
    uint32_t version;             /* 0x0002 */

    /* Display output */
    void (*print)(const char* str);
    void (*putchar)(char c);
    void (*clear)(void);
    void (*set_color)(uint8_t fg, uint8_t bg);
    void (*set_cursor)(size_t row, size_t col);
    void (*draw_char)(uint32_t row, uint32_t col, char ch, uint8_t color);

    /* Keyboard input */
    char (*getchar)(void);
    int  (*has_char)(void);
    char (*poll_char)(void);

    /* Timing & Sound */
    void (*delay)(uint32_t ms);
    void (*beep)(uint32_t freq, uint32_t ms);

    /* System & Random */
    uint32_t (*rand)(void);

    /* Filesystem */
    int (*fs_read)(const char* name, const char** data, int* size);
    int (*fs_write)(const char* name, const char* data);
} khex_api_t;

/* KHEX Kernel Functions */
void khex_init(void);
int  khex_run_file(const char* filename);
int  khex_run_buffer(const void* code, int size);
void khex_info(const char* filename);
void khex_list(void);
void khex_init_default_apps(void);

#endif
