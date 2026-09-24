#ifndef KIRILLOS_SOUND_H
#define KIRILLOS_SOUND_H

#include <stdint.h>

void sound_init(void);
void sound_stop(void);
void sound_note(uint32_t frequency, uint32_t milliseconds);
void sound_soft_note(uint32_t frequency, uint32_t milliseconds);
void sound_play_demo(void);

#endif
