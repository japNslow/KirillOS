#ifndef KIRILLOS_ATA_H
#define KIRILLOS_ATA_H

#include <stdint.h>

int ata_init(void);
int ata_read28(uint32_t lba, uint8_t* buffer);
int ata_write28(uint32_t lba, const uint8_t* buffer);

#endif
