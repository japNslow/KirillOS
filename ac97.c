#include "ac97.h"

/* ---- PCI configuration space ---- */
#define PCI_ADDR        0xCF8
#define PCI_DATA        0xCFC
#define AC97_CLASS_CODE 0x040100

/* ---- AC97 Codec (mixer) registers — offsets from BAR0 ---- */
#define CODEC_RESET       0x00
#define CODEC_MASTER_VOL  0x02   /* bit 15 = mute, 5:0/12:8 = attenuation */
#define CODEC_AUXOUT_VOL  0x04   /* headphone / aux out                    */
#define CODEC_PCMOUT_VOL  0x18   /* PCM out volume                         */
#define CODEC_EXT_CTRL    0x2A   /* bit 0 = VRA (Variable Rate Audio)      */
#define CODEC_PCM_RATE    0x2C   /* front DAC sample rate                  */

/* ---- Bus-master (NABM) registers — offsets from BAR1 ---- */
#define PO_BDBAR  0x10   /* Buffer Descriptor Base Address  (32-bit)  */
#define PO_CIV    0x14   /* Current Index Value             (8-bit RO)*/
#define PO_LVI    0x15   /* Last Valid Index                 (8-bit)   */
#define PO_SR     0x16   /* Status Register                 (16-bit)  */
#define PO_PICB   0x18   /* Position in Current Buffer      (16-bit RO)*/
#define PO_CR     0x1B   /* Control Register                (8-bit)   */
#define GLOB_CNT  0x2C   /* Global Control                  (32-bit)  */
#define GLOB_STA  0x30   /* Global Status                   (32-bit)  */

/* CR bits */
#define CR_RUN    0x01   /* Run/Pause Bus Master */
#define CR_RESET  0x02   /* Reset Registers      */
#define CR_IOCE   0x10   /* IOC Enable           */

/* SR bits (write-1-to-clear) */
#define SR_LVBCI  0x02   /* Last Valid Buffer Completion */
#define SR_BCIS   0x04   /* Buffer Completion (IOC)      */
#define SR_CLEAR  0x1C   /* clear all status bits        */

/* GLOB_STA bits */
#define GS_CODEC_READY  (1u << 8)  /* Primary Codec Ready */

/* Audio parameters */
#define BDL_COUNT       32
#define FRAMES_PER_BDL  2048
#define SAMPLE_RATE     48000   /* AC97 native rate — works with or without VRA */

/* ---- Buffer Descriptor List entry ---- */
typedef struct {
    uint32_t addr;       /* physical address of PCM buffer */
    uint16_t samples;    /* number of 16-bit samples       */
    uint16_t flags;      /* bit 14 = IOC, bit 15 = BUP     */
} __attribute__((packed)) bdl_entry_t;

/* ---- Static buffers (must be in physical memory, no paging) ---- */
static volatile int16_t pcm_buf[BDL_COUNT][FRAMES_PER_BDL * 2]
    __attribute__((aligned(4096)));
static volatile bdl_entry_t bdl_table[BDL_COUNT]
    __attribute__((aligned(4096)));

static uint16_t mix_base;   /* BAR0 — mixer I/O base  */
static uint16_t bm_base;    /* BAR1 — bus-master base  */
static int      rdy;

/* ================================================================ */
/*                         I/O helpers                              */
/* ================================================================ */
static inline void io_outl(uint16_t p, uint32_t v)
    { __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(p)); }
static inline uint32_t io_inl(uint16_t p)
    { uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void io_outw(uint16_t p, uint16_t v)
    { __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(p)); }
static inline uint16_t io_inw(uint16_t p)
    { uint16_t v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void io_outb(uint16_t p, uint8_t v)
    { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline uint8_t io_inb(uint16_t p)
    { uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }

static void small_delay(void) {
    for (volatile int i = 0; i < 30000; i++)
        __asm__ volatile("nop");
}

/* ================================================================ */
/*                         PCI helpers                              */
/* ================================================================ */
static uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    io_outl(PCI_ADDR, 0x80000000u |
        ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
        ((uint32_t)fn << 8)  | (reg & 0xFC));
    return io_inl(PCI_DATA);
}

static void pci_write(uint8_t bus, uint8_t dev, uint8_t fn,
                      uint8_t reg, uint32_t val) {
    io_outl(PCI_ADDR, 0x80000000u |
        ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
        ((uint32_t)fn << 8)  | (reg & 0xFC));
    io_outl(PCI_DATA, val);
}

/* Scan PCI buses 0-7 for AC97 audio controller (class 04.01.00) */
static int pci_find_ac97(uint8_t *ob, uint8_t *od, uint8_t *of) {
    for (uint8_t b = 0; b < 8; b++)
        for (uint8_t d = 0; d < 32; d++)
            for (uint8_t f = 0; f < 8; f++) {
                uint32_t id = pci_read(b, d, f, 0);
                if (id == 0xFFFFFFFF) continue;
                uint32_t cls = pci_read(b, d, f, 8) >> 8;
                if (cls == AC97_CLASS_CODE) {
                    *ob = b; *od = d; *of = f;
                    return 1;
                }
            }
    return 0;
}

/* ================================================================ */
/*  Reset the PCM-Out bus-master channel and clear status           */
/* ================================================================ */
static void po_reset(void) {
    io_outb(bm_base + PO_CR, CR_RESET);
    for (int i = 0; i < 500; i++) {
        if (!(io_inb(bm_base + PO_CR) & CR_RESET)) break;
        small_delay();
    }
    io_outw(bm_base + PO_SR, SR_CLEAR);
}

/* ================================================================ */
/*                     Public API                                   */
/* ================================================================ */

int ac97_init(void) {
    uint8_t bus, dev, fn;
    rdy = 0;

    if (!pci_find_ac97(&bus, &dev, &fn))
        return 0;

    /* Enable I/O space access + bus-mastering in PCI command reg */
    pci_write(bus, dev, fn, 0x04,
              pci_read(bus, dev, fn, 0x04) | 0x05);

    /* Read BARs */
    uint32_t bar0 = pci_read(bus, dev, fn, 0x10) & ~3u;
    uint32_t bar1 = pci_read(bus, dev, fn, 0x14) & ~3u;
    if (!bar0 || !bar1) return 0;
    mix_base = (uint16_t)bar0;
    bm_base  = (uint16_t)bar1;

    /* ---- Controller cold reset ---- */
    io_outl(bm_base + GLOB_CNT, 0x00);     /* deassert cold-reset */
    small_delay();
    io_outl(bm_base + GLOB_CNT, 0x02);     /* assert cold-reset → run */
    small_delay();

    /* Wait for Primary Codec Ready */
    for (int i = 0; i < 2000; i++) {
        if (io_inl(bm_base + GLOB_STA) & GS_CODEC_READY) break;
        small_delay();
    }
    if (!(io_inl(bm_base + GLOB_STA) & GS_CODEC_READY))
        return 0;

    /* ---- Reset PCM-Out channel ---- */
    po_reset();

    /* ---- Codec setup ----
     * After reset the codec comes up MUTED (master vol = 0x8000).
     * We must explicitly set all volumes to unmuted maximum.        */
    io_outw(mix_base + CODEC_RESET, 0);     /* soft-reset codec regs */
    small_delay();

    /* FIX #1: set Master Volume — without this, output is MUTED */
    io_outw(mix_base + CODEC_MASTER_VOL, 0x0000);   /* max, unmuted */
    io_outw(mix_base + CODEC_AUXOUT_VOL, 0x0000);   /* headphone    */
    io_outw(mix_base + CODEC_PCMOUT_VOL, 0x0808);   /* PCM: -12 dB  */

    /* FIX #3: enable Variable Rate Audio before setting rate */
    io_outw(mix_base + CODEC_EXT_CTRL,
            io_inw(mix_base + CODEC_EXT_CTRL) | 0x0001);
    small_delay();

    /* Set PCM front DAC sample rate (48 kHz = native, always works) */
    io_outw(mix_base + CODEC_PCM_RATE, SAMPLE_RATE);
    small_delay();

    rdy = 1;
    return 1;
}

int ac97_ready(void) {
    return rdy;
}

uint16_t ac97_get_mixer_base(void) { return mix_base; }
uint16_t ac97_get_bm_base(void)    { return bm_base; }

void ac97_stop(void) {
    if (!rdy) return;
    io_outb(bm_base + PO_CR, 0);           /* stop DMA engine */
    io_outw(bm_base + PO_SR, SR_CLEAR);    /* ack all status   */
}

void ac97_play_tone(uint32_t freq, uint32_t ms) {
    if (!rdy || freq < 20 || freq > 18000) return;

    uint32_t total = ((uint32_t)SAMPLE_RATE * ms) / 1000;
    if (total > (uint32_t)BDL_COUNT * FRAMES_PER_BDL)
        total = (uint32_t)BDL_COUNT * FRAMES_PER_BDL;

    /* How many buffers do we actually need? */
    int bufs_needed = (int)((total + FRAMES_PER_BDL - 1) / FRAMES_PER_BDL);
    if (bufs_needed < 1) bufs_needed = 1;
    if (bufs_needed > BDL_COUNT) bufs_needed = BDL_COUNT;

    /*
     * Phase accumulator (16-bit precision).
     * step = freq * 65536 / sample_rate
     * Each sample: phase += step;  ramp = (uint16_t)phase
     * ramp cycles 0..65535 at the correct audio frequency.
     */
    uint32_t step  = (freq * 65536u) / SAMPLE_RATE;
    uint32_t phase = 0;

    /* Stop & reset channel before reprogramming */
    io_outb(bm_base + PO_CR, 0);
    po_reset();

    /* ---- Fill only the needed PCM buffers (stereo triangle wave) ---- */
    for (int b = 0; b < bufs_needed; b++) {
        for (uint32_t f = 0; f < FRAMES_PER_BDL; f++) {
            uint32_t sample_idx = (uint32_t)b * FRAMES_PER_BDL + f;
            int16_t val = 0;

            if (sample_idx < total) {
                uint16_t ramp = (uint16_t)phase;
                val = (ramp < 32768) ? (int16_t)(ramp - 16384)
                                     : (int16_t)(49152 - ramp);
                phase += step;
            }
            pcm_buf[b][f * 2]     = val;   /* left channel  */
            pcm_buf[b][f * 2 + 1] = val;   /* right channel */
        }

        bdl_table[b].addr    = (uint32_t)(uintptr_t)&pcm_buf[b][0];
        bdl_table[b].samples = (uint16_t)(FRAMES_PER_BDL * 2); /* stereo */
        bdl_table[b].flags   = (b == bufs_needed - 1) ? 0x4000 : 0;
    }

    /* ---- Program DMA and start playback ---- */
    io_outl(bm_base + PO_BDBAR, (uint32_t)(uintptr_t)bdl_table);
    io_outb(bm_base + PO_LVI,   (uint8_t)(bufs_needed - 1));
    io_outw(bm_base + PO_SR,    SR_CLEAR);
    io_outb(bm_base + PO_CR,    CR_RUN | CR_IOCE);

    /*
     * NOTE: This function returns immediately after starting DMA.
     * The caller (sound.c) handles timing via PIT-based delay()
     * and calls ac97_stop() when done.  This avoids the inaccurate
     * nop-based busy-wait that was causing choppy audio.
     */
}
