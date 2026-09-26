#ifndef KIRILLOS_KAINT_H
#define KIRILLOS_KAINT_H

#include <stdint.h>

#define KAINT_CANVAS_W 240
#define KAINT_CANVAS_H 150

/**
 * Starts the Kaint graphical paint program in VGA Mode 13h (320x200 256c).
 * If initial_file is non-NULL and non-empty (e.g. "art.bmp"), attempts to load it.
 * Otherwise creates a blank white canvas.
 */
void kaint_start(const char* initial_file);

/**
 * Seeds a default demo.bmp file into KirillFS if not already present.
 */
void kaint_init_default_bmp(void);

/**
 * Exports the 240x150 canvas to an 8-bit Windows Bitmap (.bmp) file in KirillFS.
 * Includes complete 256-color Mode 13h DAC palette.
 * Returns 1 on success, 0 on error.
 */
int kaint_save_bmp(const char* filename);

/**
 * Loads an 8-bit Windows Bitmap (.bmp) file from KirillFS into the canvas buffer.
 * Restores DAC palette and pixel data.
 * Returns 1 on success, 0 on error.
 */
int kaint_load_bmp(const char* filename);

#endif /* KIRILLOS_KAINT_H */
