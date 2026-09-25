#include "sound.h"
#include "ac97.h"

#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL2 0x42
#define SPEAKER_PORT 0x61
#define PIT_BASE_HZ 1193182
#define PIT_MS_DIVISOR 1193

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint16_t pit0_count(void) {
    uint16_t count;
    outb(PIT_COMMAND, 0x00);
    count = inb(PIT_CHANNEL0);
    count |= (uint16_t)inb(PIT_CHANNEL0) << 8;
    return count;
}

#include "keyboard.h"

static void delay(uint32_t milliseconds) {
    while (milliseconds--) {
        uint16_t previous = pit0_count();
        uint16_t current;
        uint32_t timeout = 100000;
        do {
            current = pit0_count();
            if (current > previous) break;
        } while (--timeout);

        keyboard_check_hardware();
    }
}

void sound_init(void) {
    outb(PIT_COMMAND, 0x34);
    outb(PIT_CHANNEL0, (uint8_t)PIT_MS_DIVISOR);
    outb(PIT_CHANNEL0, (uint8_t)(PIT_MS_DIVISOR >> 8));
    sound_stop();
    ac97_init();
}

void sound_stop(void) {
    outb(SPEAKER_PORT, inb(SPEAKER_PORT) & 0xFC);
}

void sound_note(uint32_t frequency, uint32_t milliseconds) {
    uint16_t divisor;
    uint8_t speaker;

    if (frequency < 20 || frequency > 20000) {
        sound_stop();
        delay(milliseconds);
        return;
    }
    divisor = (uint16_t)(PIT_BASE_HZ / frequency);
    outb(PIT_COMMAND, 0xB6);
    outb(PIT_CHANNEL2, (uint8_t)divisor);
    outb(PIT_CHANNEL2, (uint8_t)(divisor >> 8));
    speaker = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, speaker | 0x03);
    delay(milliseconds);
    sound_stop();
}

void sound_soft_note(uint32_t frequency, uint32_t milliseconds) {
    if (ac97_ready()) {
        ac97_play_tone(frequency, milliseconds);   /* starts DMA, returns immediately */
        delay(milliseconds);                       /* accurate PIT-based wait         */
        ac97_stop();                               /* stop DMA                        */
        return;
    }
    sound_note(frequency, milliseconds);
}

static void note(uint32_t frequency, uint32_t duration) {
    sound_soft_note(frequency, duration);
    delay(15);
}

void sound_play_demo(void) {
    static const uint16_t melody[] = {
        262, 330, 392, 523, 392, 330,
        294, 349, 440, 587, 440, 349,
        330, 392, 494, 659, 494, 392
    };
    for (unsigned int i = 0; i < sizeof(melody) / sizeof(melody[0]); i++)
        note(melody[i], 120);
}
