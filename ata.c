#include "ata.h"

#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_SECTOR_CNT 0x1F2
#define ATA_LBA0       0x1F3
#define ATA_LBA1       0x1F4
#define ATA_LBA2       0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_CONTROL    0x3F6
#define ATA_CMD_READ   0x20
#define ATA_CMD_WRITE  0x30
#define ATA_CMD_IDENT  0xEC

static int available;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static int wait_ready(void) {
    uint8_t status;
    for (int i = 0; i < 1000000; i++) {
        status = inb(ATA_STATUS);
        if (status == 0xFF) return 0;
        if (!(status & 0x80) && (status & 0x08)) return 1;
        if (status & 0x01) return 0;
    }
    return 0;
}

static int transfer(uint32_t lba, uint8_t* buffer, int write) {
    if (!available || lba > 0x0FFFFFFF) return 0;
    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, 1);
    outb(ATA_LBA0, (uint8_t)lba);
    outb(ATA_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_LBA2, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND, write ? ATA_CMD_WRITE : ATA_CMD_READ);
    if (!wait_ready()) return 0;

    for (int i = 0; i < 256; i++) {
        if (write) {
            uint16_t word = buffer[i * 2] | ((uint16_t)buffer[i * 2 + 1] << 8);
            __asm__ volatile ("outw %0, %%dx" : : "a"(word), "d"((uint16_t)ATA_DATA));
        } else {
            uint16_t word = inw(ATA_DATA);
            buffer[i * 2] = (uint8_t)word;
            buffer[i * 2 + 1] = (uint8_t)(word >> 8);
        }
    }
    if (write) inb(ATA_STATUS);
    return 1;
}

int ata_init(void) {
    outb(ATA_CONTROL, 0);
    outb(ATA_DRIVE, 0xE0);
    outb(ATA_SECTOR_CNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENT);
    if (inb(ATA_STATUS) == 0xFF) {
        available = 0;
        return 0;
    }
    available = wait_ready();
    if (available) {
        for (int i = 0; i < 256; i++) (void)inw(ATA_DATA);
    }
    return available;
}

int ata_read28(uint32_t lba, uint8_t* buffer) {
    return transfer(lba, buffer, 0);
}

int ata_write28(uint32_t lba, const uint8_t* buffer) {
    return transfer(lba, (uint8_t*)buffer, 1);
}
