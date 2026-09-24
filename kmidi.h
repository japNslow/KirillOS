#ifndef KMIDI_H
#define KMIDI_H

#include <stdint.h>

void kmidi_open(const char* filename);
void kmidi_play_file(const char* filename);
void kmidi_init_default_songs(void);

#endif
