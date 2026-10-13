#ifndef KIRILLOS_E1000_H
#define KIRILLOS_E1000_H

#include <stdint.h>

#define E1000_MAX_FRAME 1518

int e1000_init(void);                         /* 1 if a NIC was found */
int e1000_present(void);
int e1000_link_up(void);
const uint8_t* e1000_mac(void);
uint32_t e1000_mmio_base(void);
int e1000_send(const void* data, uint16_t length);
int e1000_receive(uint8_t* buffer);          /* frame length, 0 if none */

#endif
