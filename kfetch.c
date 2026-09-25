#include "kfetch.h"
#include "vga.h"
#include "kirillfs.h"
#include "ac97.h"
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg | 0x80); /* disable NMI */
    return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t val) {
    return (uint8_t)(((val >> 4) * 10) + (val & 0x0F));
}

static void rtc_read(uint8_t* hour, uint8_t* min, uint8_t* sec,
                     uint8_t* day, uint8_t* month, uint16_t* year) {
    /* Wait if RTC update in progress */
    uint32_t timeout = 10000;
    while ((cmos_read(0x0A) & 0x80) && --timeout);

    *sec   = cmos_read(0x00);
    *min   = cmos_read(0x02);
    *hour  = cmos_read(0x04);
    *day   = cmos_read(0x07);
    *month = cmos_read(0x08);
    *year  = cmos_read(0x09);

    uint8_t reg_b = cmos_read(0x0B);
    /* BCD mode check */
    if (!(reg_b & 0x04)) {
        *sec   = bcd_to_bin(*sec);
        *min   = bcd_to_bin(*min);
        *hour  = (uint8_t)(bcd_to_bin((uint8_t)(*hour & 0x7F)) | (*hour & 0x80));
        *day   = bcd_to_bin(*day);
        *month = bcd_to_bin(*month);
        *year  = bcd_to_bin((uint8_t)*year);
    }
    /* 12-hour mode check */
    if (!(reg_b & 0x02) && (*hour & 0x80)) {
        *hour = (uint8_t)(((*hour & 0x7F) + 12) % 24);
    }
    *year += 2000;
}

static void print_2d(uint8_t val) {
    vga_putchar((char)('0' + ((val / 10) % 10)));
    vga_putchar((char)('0' + (val % 10)));
}

static void print_4d(uint16_t val) {
    print_2d((uint8_t)(val / 100));
    print_2d((uint8_t)(val % 100));
}

static void print_dec(int val) {
    if (val == 0) { vga_putchar('0'); return; }
    char s[10];
    int len = 0;
    while (val > 0) {
        s[len++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (len > 0) vga_putchar(s[--len]);
}

void kfetch_date(void) {
    uint8_t h, m, s, d, mo;
    uint16_t y;
    rtc_read(&h, &m, &s, &d, &mo, &y);
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write("Date: ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    print_4d(y); vga_putchar('-');
    print_2d(mo); vga_putchar('-');
    print_2d(d);
    vga_newline();
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

void kfetch_time(void) {
    uint8_t h, m, s, d, mo;
    uint16_t y;
    rtc_read(&h, &m, &s, &d, &mo, &y);
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write("Time: ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    print_2d(h); vga_putchar(':');
    print_2d(m); vga_putchar(':');
    print_2d(s);
    vga_write(" (CMOS RTC)");
    vga_newline();
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static const char* LOGO[] = {
    "  _  ___      _ _ _  ____   _____ ",
    " | |/ (_)    (_) | |/ __ \\ / ____|",
    " | ' / _ _ __ _| | | |  | | (___  ",
    " |  < | | '__| | | | |  | |\\___ \\ ",
    " | . \\| | |  | | | | |__| |____) |",
    " |_|\\_\\_|_|  |_|_|_|\\____/|_____/ ",
    "                                  ",
    "                                  "
};

void kfetch_print(void) {
    uint8_t h, m, s, d, mo;
    uint16_t y;
    rtc_read(&h, &m, &s, &d, &mo, &y);

    vga_newline();

    /* Line 0: Header user@kirillos */
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write(LOGO[0]);
    vga_write("   ");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_write("kirill");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_write("@");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write("kirillos\n");

    /* Line 1: Separator */
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write(LOGO[1]);
    vga_write("   ");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write("----------------------------\n");

    /* Line 2: OS */
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write(LOGO[2]);
    vga_write("   ");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write("OS:       ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write("KirillOS v0.2 (x86 32-bit)\n");

    /* Line 3: Kernel */
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write(LOGO[3]);
    vga_write("   ");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write("Kernel:   ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_write("Monolithic ELF32 + kboot MBR\n");

    /* Line 4: Date & Time */
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write(LOGO[4]);
    vga_write("   ");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write("RTC Time: ");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    print_4d(y); vga_putchar('-'); print_2d(mo); vga_putchar('-'); print_2d(d);
    vga_write(" ");
    print_2d(h); vga_putchar(':'); print_2d(m); vga_putchar(':'); print_2d(s);
    vga_newline();

    /* Line 5: Shell & Display */
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write(LOGO[5]);
    vga_write("   ");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write("Display:  ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write("VGA 80x25 Text & Mode 13h (320x200x256)\n");

    /* Line 6: Audio */
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write(LOGO[6]);
    vga_write("   ");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write("Audio:    ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    if (ac97_ready()) {
        vga_write("Intel AC'97 (DMA 48kHz) + PC Speaker\n");
    } else {
        vga_write("PC Speaker 8254 PIT (AC'97 ready)\n");
    }

    /* Line 7: Storage & KirillFS */
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write(LOGO[7]);
    vga_write("   ");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write("Disk/FS:  ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_write("ATA PIO 64 MiB (KirillFS: ");
    print_dec(kfs_file_count());
    vga_write(kfs_is_persistent() ? "/32 files, Disk LBA 321)\n" : "/32 files, RAM)\n");

    /* Line 8: Memory */
    vga_write("                                     ");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write("Memory:   ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write("128 MiB RAM (KHeap: 64 KB Arena)\n");

    /* Line 9: Color Palette Blocks */
    vga_write("                                     ");
    for (uint8_t c = 0; c < 8; c++) {
        vga_set_color(c, c);
        vga_write("   ");
        vga_set_color(VGA_BLACK, VGA_BLACK);
        vga_putchar(' ');
    }
    vga_newline();

    vga_write("                                     ");
    for (uint8_t c = 8; c < 16; c++) {
        vga_set_color(c, c);
        vga_write("   ");
        vga_set_color(VGA_BLACK, VGA_BLACK);
        vga_putchar(' ');
    }
    vga_newline();
    vga_newline();

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}
