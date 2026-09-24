#ifndef KDHE_H
#define KDHE_H

#include <stdint.h>

void kdhe_open(const char* filename);
void kdhe_open_disk(uint32_t lba);

#endif
