#ifndef KIRILLOS_KIDI_H
#define KIRILLOS_KIDI_H

#include "kmidi.h"

static inline void kidi_open(const char* name) {
    kmidi_open(name);
}

#endif
