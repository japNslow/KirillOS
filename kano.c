#include "kano.h"
#include "keyboard.h"
#include "kirillfs.h"
#include "vga.h"

#define KANO_BUFFER_SIZE 512

static char buffer[KANO_BUFFER_SIZE];
static int length;
static int dirty;
static const char* filename;
static const char* editor_title;

static void redraw(void) {
    vga_clear();
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write(editor_title);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_write(filename);
    if (dirty) vga_write(" *");
    vga_newline();
    vga_write("----------------------------------------\n");
    vga_write(buffer);
    vga_newline();
    vga_set_color(VGA_BLACK, VGA_LIGHT_GREY);
    vga_write(" Ctrl+S save   Ctrl+Q quit ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void save_file(void) {
    char saved[KFS_DATA_MAX];
    for (int i = 0; i <= length; i++) saved[i] = buffer[i];
    if (kfs_write(filename, saved) == KFS_OK) {
        dirty = 0;
        redraw();
    } else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_write("\nSave failed: file is too large\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

void kano_open_named(const char* name, const char* title) {
    const char* data;
    int size;
    filename = name;
    editor_title = title;
    length = 0;
    dirty = 0;

    if (kfs_read(name, &data, &size) == KFS_OK) {
        if (size >= KANO_BUFFER_SIZE) size = KANO_BUFFER_SIZE - 1;
        for (int i = 0; i < size; i++) buffer[i] = data[i];
        length = size;
        buffer[length] = 0;
    } else if (kfs_touch(name) != KFS_OK) {
        vga_write("kano: cannot create file\n");
        return;
    }

    redraw();
    for (;;) {
        char c = keyboard_getchar();
        if (c == KEY_SAVE) {
            save_file();
        } else if (c == KEY_QUIT) {
            if (dirty) {
                vga_write("\nUnsaved changes. Press Ctrl+S first.\n");
            } else {
                vga_clear();
                return;
            }
        } else if (c == KEY_BACKSPACE) {
            if (length > 0) {
                length--;
                buffer[length] = 0;
                dirty = 1;
                redraw();
            }
        } else if (c == KEY_ENTER) {
            if (length < KANO_BUFFER_SIZE - 1) {
                buffer[length++] = '\n';
                buffer[length] = 0;
                dirty = 1;
                redraw();
            }
        } else if (c >= 32 && c < 127 && length < KANO_BUFFER_SIZE - 1) {
            buffer[length++] = c;
            buffer[length] = 0;
            dirty = 1;
            redraw();
        }
    }
}

void kano_open(const char* name) {
    kano_open_named(name, "KANO - KirillOS Nano        ");
}
