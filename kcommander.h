#ifndef KCOMMANDER_H
#define KCOMMANDER_H

#include <stdint.h>

/**
 * Launches Kirill Commander:
 * - Switches to VGA Mode 13h (320x200 256 colors)
 * - Initializes PS/2 mouse driver and keyboard
 * - Displays classic Norton/Total Commander dual-pane blue GUI
 * - Supports mouse hover, single/double click, scrollbar, F1..F6 actions
 * - Restores text mode on exit or when launching external apps
 */
void kcommander_start(void);

#endif /* KCOMMANDER_H */
