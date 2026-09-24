#ifndef KIRILLOS_AC97_H
#define KIRILLOS_AC97_H

#include <stdint.h>

int ac97_init(void);
int ac97_ready(void);
void ac97_stop(void);
void ac97_play_tone(uint32_t frequency, uint32_t milliseconds);
uint16_t ac97_get_mixer_base(void);
uint16_t ac97_get_bm_base(void);

#endif
