#include "e1000.h"

/* Intel 82540EM (QEMU's default NIC), polled, no interrupts */

#define REG_CTRL   0x0000
#define REG_STATUS 0x0008
#define REG_EERD   0x0014
#define REG_ICR    0x00C0
#define REG_IMC    0x00D8
#define REG_RCTL   0x0100
#define REG_TCTL   0x0400
#define REG_TIPG   0x0410
#define REG_RDBAL  0x2800
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818
#define REG_TDBAL  0x3800
#define REG_TDBAH  0x3804
#define REG_TDLEN  0x3808
#define REG_TDH    0x3810
#define REG_TDT    0x3818
#define REG_MTA    0x5200
#define REG_RAL    0x5400
#define REG_RAH    0x5404

#define CTRL_ASDE  (1u << 5)
#define CTRL_SLU   (1u << 6)
#define CTRL_RST   (1u << 26)
#define RCTL_EN    (1u << 1)
#define RCTL_BAM   (1u << 15)
#define RCTL_SECRC (1u << 26)
#define TCTL_EN    (1u << 1)
#define TCTL_PSP   (1u << 3)
#define CMD_EOP    0x01
#define CMD_IFCS   0x02
#define CMD_RS     0x08
#define STAT_DD    0x01

#define RX_COUNT 32
#define TX_COUNT 8
#define BUF_SIZE 2048

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed)) rx_desc_t;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed)) tx_desc_t;

static volatile rx_desc_t rx_ring[RX_COUNT] __attribute__((aligned(16)));
static volatile tx_desc_t tx_ring[TX_COUNT] __attribute__((aligned(16)));
static uint8_t rx_bufs[RX_COUNT][BUF_SIZE] __attribute__((aligned(16)));
static uint8_t tx_bufs[TX_COUNT][BUF_SIZE] __attribute__((aligned(16)));

static volatile uint8_t* mmio;
static uint8_t mac[6];
static int rx_cur, tx_cur;

static inline void outl(uint16_t port, uint32_t v) {
    __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t r;
    __asm__ volatile("inl %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

static uint32_t pci_read(uint8_t dev, uint8_t fn, uint8_t reg) {
    outl(0xCF8, 0x80000000u | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (reg & 0xFC));
    return inl(0xCFC);
}

static void pci_write(uint8_t dev, uint8_t fn, uint8_t reg, uint32_t v) {
    outl(0xCF8, 0x80000000u | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (reg & 0xFC));
    outl(0xCFC, v);
}

static uint32_t rd(uint32_t reg) { return *(volatile uint32_t*)(mmio + reg); }
static void wr(uint32_t reg, uint32_t v) { *(volatile uint32_t*)(mmio + reg) = v; }

static int eeprom_read(uint8_t addr, uint16_t* out) {
    wr(REG_EERD, ((uint32_t)addr << 8) | 1);
    for (int i = 0; i < 100000; i++) {
        uint32_t v = rd(REG_EERD);
        if (v & (1u << 4)) { *out = (uint16_t)(v >> 16); return 1; }
    }
    return 0;
}

static void read_mac(void) {
    uint16_t w[3];
    if (eeprom_read(0, &w[0]) && eeprom_read(1, &w[1]) && eeprom_read(2, &w[2])) {
        for (int i = 0; i < 3; i++) {
            mac[i * 2] = (uint8_t)w[i];
            mac[i * 2 + 1] = (uint8_t)(w[i] >> 8);
        }
        return;
    }
    uint32_t lo = rd(REG_RAL), hi = rd(REG_RAH);
    for (int i = 0; i < 4; i++) mac[i] = (uint8_t)(lo >> (i * 8));
    mac[4] = (uint8_t)hi;
    mac[5] = (uint8_t)(hi >> 8);
}

int e1000_init(void) {
    uint8_t dev = 0, fn = 0;
    int found = 0;
    for (uint8_t d = 0; d < 32 && !found; d++) {
        for (uint8_t f = 0; f < 8; f++) {
            uint32_t id = pci_read(d, f, 0);
            if (id == 0x100E8086 || id == 0x100F8086) { dev = d; fn = f; found = 1; break; }
        }
    }
    if (!found) return 0;

    /* Memory space + bus mastering for descriptor DMA */
    pci_write(dev, fn, 0x04, pci_read(dev, fn, 0x04) | 0x06);
    mmio = (volatile uint8_t*)(uintptr_t)(pci_read(dev, fn, 0x10) & ~0xFu);

    wr(REG_IMC, 0xFFFFFFFF);
    wr(REG_CTRL, rd(REG_CTRL) | CTRL_RST);
    for (int i = 0; i < 100000 && (rd(REG_CTRL) & CTRL_RST); i++) { }
    wr(REG_IMC, 0xFFFFFFFF);
    (void)rd(REG_ICR);
    wr(REG_CTRL, rd(REG_CTRL) | CTRL_SLU | CTRL_ASDE);

    read_mac();
    wr(REG_RAL, mac[0] | (mac[1] << 8) | (mac[2] << 16) | ((uint32_t)mac[3] << 24));
    wr(REG_RAH, mac[4] | (mac[5] << 8) | (1u << 31));
    for (int i = 0; i < 128; i++) wr(REG_MTA + i * 4, 0);

    for (int i = 0; i < RX_COUNT; i++) {
        rx_ring[i].addr = (uint32_t)(uintptr_t)rx_bufs[i];
        rx_ring[i].status = 0;
    }
    wr(REG_RDBAL, (uint32_t)(uintptr_t)rx_ring);
    wr(REG_RDBAH, 0);
    wr(REG_RDLEN, sizeof(rx_ring));
    wr(REG_RDH, 0);
    wr(REG_RDT, RX_COUNT - 1);
    rx_cur = 0;
    wr(REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);  /* 2048-byte buffers */

    for (int i = 0; i < TX_COUNT; i++) {
        tx_ring[i].addr = (uint32_t)(uintptr_t)tx_bufs[i];
        tx_ring[i].status = STAT_DD;
        tx_ring[i].cmd = 0;
    }
    wr(REG_TDBAL, (uint32_t)(uintptr_t)tx_ring);
    wr(REG_TDBAH, 0);
    wr(REG_TDLEN, sizeof(tx_ring));
    wr(REG_TDH, 0);
    wr(REG_TDT, 0);
    tx_cur = 0;
    wr(REG_TIPG, 0x0060200A);
    wr(REG_TCTL, TCTL_EN | TCTL_PSP | (0x10u << 4) | (0x40u << 12));
    return 1;
}

int e1000_present(void) { return mmio != 0; }
int e1000_link_up(void) { return mmio && (rd(REG_STATUS) & 2); }
const uint8_t* e1000_mac(void) { return mac; }
uint32_t e1000_mmio_base(void) { return (uint32_t)(uintptr_t)mmio; }

int e1000_send(const void* data, uint16_t length) {
    if (!mmio || length > E1000_MAX_FRAME) return 0;
    volatile tx_desc_t* d = &tx_ring[tx_cur];
    for (int i = 0; i < 1000000 && !(d->status & STAT_DD); i++) { }
    if (!(d->status & STAT_DD)) return 0;

    const uint8_t* src = (const uint8_t*)data;
    for (int i = 0; i < length; i++) tx_bufs[tx_cur][i] = src[i];
    d->length = length;
    d->cmd = CMD_EOP | CMD_IFCS | CMD_RS;
    d->status = 0;
    tx_cur = (tx_cur + 1) % TX_COUNT;
    wr(REG_TDT, tx_cur);
    return 1;
}

int e1000_receive(uint8_t* buffer) {
    if (!mmio) return 0;
    volatile rx_desc_t* d = &rx_ring[rx_cur];
    if (!(d->status & STAT_DD)) return 0;

    int length = d->length;
    if (length > E1000_MAX_FRAME) length = 0;  /* oversized: drop */
    for (int i = 0; i < length; i++) buffer[i] = rx_bufs[rx_cur][i];
    d->status = 0;
    wr(REG_RDT, rx_cur);
    rx_cur = (rx_cur + 1) % RX_COUNT;
    return length;
}
