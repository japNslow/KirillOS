#ifndef KIRILLOS_NET_H
#define KIRILLOS_NET_H

#include <stdint.h>

/* Minimal polled IPv4 stack over e1000: ARP, ICMP echo, UDP, DHCP, DNS, TCP SYN probe */

int  net_init(void);                 /* 1 if a NIC is up */
int  net_dhcp(int verbose);          /* 1 on lease */
void net_ifconfig(void);
void net_ping(const char* host, int count);
void net_host(const char* name);
void net_tcping(const char* host, uint16_t port, int count);
uint32_t net_ip(void);
void net_print_ip(uint32_t ip);

#endif
