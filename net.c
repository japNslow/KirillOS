#include "net.h"
#include "e1000.h"
#include "vga.h"
#include "keyboard.h"

#define ETH_ARP   0x0806
#define ETH_IP    0x0800
#define IP_ICMP   1
#define IP_TCP    6
#define IP_UDP    17
#define TCP_FIN   0x01
#define TCP_SYN   0x02
#define TCP_RST   0x04
#define TCP_ACK   0x10
#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5
#define PING_ID   0x4B49
#define DNS_PORT  49153
#define ARP_SLOTS 8

/* All addresses are kept in host byte order */
static uint8_t  my_mac[6];
static uint32_t my_ip, netmask, gateway, dns_server;
static uint32_t rx_count, tx_count;

static uint8_t frame[E1000_MAX_FRAME];   /* last received frame */
static uint8_t tx[E1000_MAX_FRAME];      /* frame under construction */
static uint8_t out[1472];                /* L4 packet built by commands */

static struct { uint32_t ip; uint8_t mac[6]; } arp_cache[ARP_SLOTS];
static int arp_next;

static uint16_t udp_wait_port;           /* deliver UDP for this port into udp_buf */
static uint8_t  udp_buf[1472];
static int      udp_len, udp_got;

static uint32_t tcp_peer;                /* deliver the reply to our SYN */
static uint16_t tcp_peer_port, tcp_local_port;
static uint8_t  tcp_flags;
static uint32_t tcp_their_seq, tcp_their_ack;
static int      tcp_got;

static uint16_t ping_seq;
static int      ping_got;
static uint8_t  ping_ttl;

static const uint8_t broadcast_mac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/* ================================================================ */
/*                Millisecond clock from PIT channel 0              */
/* ================================================================ */
static uint32_t now_ms;
static uint16_t last_pit;

static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t r;
    __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

/* PIT ch0 counts down once per ms (set up by sound_init); a reload means 1 ms passed */
static void tick(void) {
    outb(0x43, 0x00);
    uint16_t c = inb(0x40);
    c |= (uint16_t)inb(0x40) << 8;
    if (c > last_pit) now_ms++;
    last_pit = c;
}

/* ================================================================ */
/*                          Byte helpers                            */
/* ================================================================ */
static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t* p, uint32_t v) { put16(p, (uint16_t)(v >> 16)); put16(p + 2, (uint16_t)v); }
static uint16_t get16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t get32(const uint8_t* p) { return ((uint32_t)get16(p) << 16) | get16(p + 2); }

static void copy(uint8_t* d, const uint8_t* s, int n) { while (n-- > 0) *d++ = *s++; }
static void zero(uint8_t* d, int n) { while (n-- > 0) *d++ = 0; }

static uint16_t checksum(const uint8_t* p, int n) {
    uint32_t sum = 0;
    for (; n > 1; n -= 2, p += 2) sum += get16(p);
    if (n) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static void print_dec(uint32_t v) {
    char b[10];
    int n = 0;
    do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) vga_putchar(b[--n]);
}

void net_print_ip(uint32_t ip) {
    for (int i = 3; i >= 0; i--) {
        print_dec((ip >> (i * 8)) & 0xFF);
        if (i) vga_putchar('.');
    }
}

static void print_mac(const uint8_t* m) {
    for (int i = 0; i < 6; i++) {
        vga_write_hex(m[i], 2);
        if (i < 5) vga_putchar(':');
    }
}

static int parse_ip(const char* s, uint32_t* ip) {
    uint32_t v = 0;
    for (int part = 0; part < 4; part++) {
        uint32_t n = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9' && digits < 4) { n = n * 10 + (uint32_t)(*s++ - '0'); digits++; }
        if (!digits || n > 255) return 0;
        v = (v << 8) | n;
        if (part < 3 && *s++ != '.') return 0;
    }
    if (*s && *s != ' ') return 0;
    *ip = v;
    return 1;
}

/* ================================================================ */
/*                        Frame transmission                        */
/* ================================================================ */
static void eth_send(const uint8_t* dst, uint16_t type, int payload) {
    copy(tx, dst, 6);
    copy(tx + 6, my_mac, 6);
    put16(tx + 12, type);
    int len = 14 + payload;
    if (len < 60) { zero(tx + len, 60 - len); len = 60; }
    if (e1000_send(tx, (uint16_t)len)) tx_count++;
}

/* L4 packet must already be at tx + 34 */
static void ip_build(const uint8_t* dst_mac, uint32_t dst, uint8_t proto, int len) {
    static uint16_t ident;
    uint8_t* ip = tx + 14;
    ip[0] = 0x45; ip[1] = 0;
    put16(ip + 2, (uint16_t)(20 + len));
    put16(ip + 4, ++ident);
    put16(ip + 6, 0);
    ip[8] = 64; ip[9] = proto;
    put16(ip + 10, 0);
    put32(ip + 12, my_ip);
    put32(ip + 16, dst);
    put16(ip + 10, checksum(ip, 20));
    eth_send(dst_mac, ETH_IP, 20 + len);
}

static void arp_send(uint16_t op, const uint8_t* dst_mac, const uint8_t* tha, uint32_t tpa) {
    uint8_t* a = tx + 14;
    put16(a, 1); put16(a + 2, ETH_IP);
    a[4] = 6; a[5] = 4;
    put16(a + 6, op);
    copy(a + 8, my_mac, 6);
    put32(a + 14, my_ip);
    copy(a + 18, tha, 6);
    put32(a + 24, tpa);
    eth_send(dst_mac, ETH_ARP, 28);
}

static void arp_store(uint32_t ip, const uint8_t* mac) {
    int slot = -1;
    for (int i = 0; i < ARP_SLOTS; i++) if (arp_cache[i].ip == ip) slot = i;
    if (slot < 0) { slot = arp_next; arp_next = (arp_next + 1) % ARP_SLOTS; }
    arp_cache[slot].ip = ip;
    copy(arp_cache[slot].mac, mac, 6);
}

static const uint8_t* arp_lookup(uint32_t ip) {
    for (int i = 0; i < ARP_SLOTS; i++)
        if (ip && arp_cache[i].ip == ip) return arp_cache[i].mac;
    return 0;
}

/* ================================================================ */
/*                          Frame reception                         */
/* ================================================================ */
static void handle_arp(const uint8_t* a, int len) {
    if (len < 28 || get16(a) != 1 || get16(a + 2) != ETH_IP) return;
    uint32_t spa = get32(a + 14);
    if (spa) arp_store(spa, a + 8);
    if (get16(a + 6) == 1 && my_ip && get32(a + 24) == my_ip)
        arp_send(2, a + 8, a + 8, spa);
}

static void handle_ip(const uint8_t* ip, int len, const uint8_t* src_mac) {
    if (len < 20 || (ip[0] >> 4) != 4) return;
    int hl = (ip[0] & 0x0F) * 4;
    int total = get16(ip + 2);
    if (total > len || total < hl) return;
    uint32_t src = get32(ip + 12), dst = get32(ip + 16);
    if (my_ip && dst != my_ip && dst != 0xFFFFFFFF) return;

    const uint8_t* p = ip + hl;
    int plen = total - hl;

    if (ip[9] == IP_ICMP && plen >= 8) {
        if (p[0] == 8 && my_ip) {                         /* echo request: answer it */
            if (plen > (int)sizeof(tx) - 34) return;
            copy(tx + 34, p, plen);
            tx[34] = 0;
            put16(tx + 36, 0);
            put16(tx + 36, checksum(tx + 34, plen));
            ip_build(src_mac, src, IP_ICMP, plen);
        } else if (p[0] == 0 && get16(p + 4) == PING_ID && get16(p + 6) == ping_seq) {
            ping_ttl = ip[8];
            ping_got = 1;
        }
    } else if (ip[9] == IP_TCP && plen >= 20) {
        if (tcp_local_port && !tcp_got && src == tcp_peer &&
            get16(p) == tcp_peer_port && get16(p + 2) == tcp_local_port) {
            tcp_their_seq = get32(p + 4);
            tcp_their_ack = get32(p + 8);
            tcp_flags = p[13];
            tcp_got = 1;
        }
    } else if (ip[9] == IP_UDP && plen >= 8) {
        if (udp_wait_port && get16(p + 2) == udp_wait_port && !udp_got) {
            int n = get16(p + 4) - 8;
            if (n < 0 || n > plen - 8) return;
            if (n > (int)sizeof(udp_buf)) n = sizeof(udp_buf);
            copy(udp_buf, p + 8, n);
            udp_len = n;
            udp_got = 1;
        }
    }
}

static void net_poll(void) {
    int n;
    tick();
    while ((n = e1000_receive(frame)) > 0) {
        rx_count++;
        if (n < 14) continue;
        uint16_t type = get16(frame + 12);
        if (type == ETH_ARP) handle_arp(frame + 14, n - 14);
        else if (type == ETH_IP) handle_ip(frame + 14, n - 14, frame + 6);
        tick();
    }
}

/* Poll until *flag is set, the time runs out, or ESC is pressed */
static int wait_flag(volatile int* flag, uint32_t ms) {
    uint32_t start = now_ms;
    while (now_ms - start < ms) {
        net_poll();
        if (*flag) return 1;
        if (keyboard_take_esc()) return -1;
    }
    return 0;
}

static const uint8_t* resolve_mac(uint32_t dst) {
    if (dst == 0xFFFFFFFF) return broadcast_mac;
    uint32_t hop = ((dst ^ my_ip) & netmask) ? gateway : dst;
    for (int attempt = 0; attempt < 3; attempt++) {
        const uint8_t* m = arp_lookup(hop);
        if (m) return m;
        arp_send(1, broadcast_mac, (const uint8_t*)"\0\0\0\0\0\0", hop);
        uint32_t start = now_ms;
        while (now_ms - start < 500) {
            net_poll();
            if ((m = arp_lookup(hop))) return m;
        }
    }
    return 0;
}

/* Send the L4 packet in out[0..len) */
static int ip_send(uint32_t dst, uint8_t proto, int len) {
    const uint8_t* mac = resolve_mac(dst);
    if (!mac) return 0;
    uint8_t dst_mac[6];
    copy(dst_mac, mac, 6);
    copy(tx + 34, out, len);
    ip_build(dst_mac, dst, proto, len);
    return 1;
}

/* UDP payload must be at out + 8 */
static int udp_send(uint32_t dst, uint16_t sport, uint16_t dport, int payload) {
    put16(out, sport);
    put16(out + 2, dport);
    put16(out + 4, (uint16_t)(8 + payload));
    put16(out + 6, 0);                                    /* checksum optional in IPv4 */
    return ip_send(dst, IP_UDP, 8 + payload);
}

/* Bare TCP segment (no payload) with the mandatory pseudo-header checksum */
static int tcp_send(uint32_t dst, uint16_t sport, uint16_t dport,
                    uint32_t seq, uint32_t ack, uint8_t flags) {
    uint8_t* t = out + 12;                                /* pseudo-header in out[0..12) */
    zero(t, 24);
    put16(t, sport);
    put16(t + 2, dport);
    put32(t + 4, seq);
    put32(t + 8, ack);
    t[12] = (flags & TCP_SYN) ? 0x60 : 0x50;              /* header length: 24 with MSS, else 20 */
    t[13] = flags;
    put16(t + 14, (flags & TCP_RST) ? 0 : 1460);          /* window */
    int len = 20;
    if (flags & TCP_SYN) { t[20] = 2; t[21] = 4; put16(t + 22, 1460); len = 24; }

    put32(out, my_ip);
    put32(out + 4, dst);
    out[8] = 0; out[9] = IP_TCP;
    put16(out + 10, (uint16_t)len);
    put16(t + 16, checksum(out, 12 + len));

    for (int i = 0; i < len; i++) out[i] = t[i];          /* drop the pseudo-header */
    return ip_send(dst, IP_TCP, len);
}

/* ================================================================ */
/*                               DHCP                               */
/* ================================================================ */
#define DHCP_XID 0x4B4F5321

static const uint8_t* dhcp_option(const uint8_t* opts, int len, uint8_t code) {
    int i = 0;
    while (i < len && opts[i] != 255) {
        if (opts[i] == 0) { i++; continue; }
        if (i + 1 >= len) break;
        if (opts[i] == code) return opts + i + 2;
        i += 2 + opts[i + 1];
    }
    return 0;
}

/* Send one DHCP message and wait for a reply of the wanted type */
static int dhcp_exchange(uint8_t type, uint32_t req_ip, uint32_t server, uint8_t want) {
    uint8_t* d = out + 8;
    zero(d, 300);
    d[0] = 1; d[1] = 1; d[2] = 6;
    put32(d + 4, DHCP_XID);
    put16(d + 10, 0x8000);
    copy(d + 28, my_mac, 6);
    put32(d + 236, 0x63825363);
    uint8_t* o = d + 240;
    *o++ = 53; *o++ = 1; *o++ = type;
    if (req_ip) { *o++ = 50; *o++ = 4; put32(o, req_ip); o += 4; }
    if (server) { *o++ = 54; *o++ = 4; put32(o, server); o += 4; }
    *o++ = 55; *o++ = 3; *o++ = 1; *o++ = 3; *o++ = 6;
    *o++ = 255;

    udp_wait_port = 68;
    udp_got = 0;
    if (!udp_send(0xFFFFFFFF, 68, 67, 300)) return 0;

    uint32_t start = now_ms;
    while (now_ms - start < 2000) {
        if (wait_flag(&udp_got, 2000 - (now_ms - start)) <= 0) break;
        const uint8_t* m = dhcp_option(udp_buf + 240, udp_len - 240, 53);
        if (udp_len > 240 && udp_buf[0] == 2 && get32(udp_buf + 4) == DHCP_XID && m && *m == want)
            return 1;
        udp_got = 0;
    }
    return 0;
}

int net_dhcp(int verbose) {
    if (!e1000_present()) return 0;
    my_ip = 0;
    if (verbose) vga_write("DHCP: discover...");
    if (!dhcp_exchange(DHCP_DISCOVER, 0, 0, DHCP_OFFER)) {
        udp_wait_port = 0;
        if (verbose) vga_write(" no offer\n");
        return 0;
    }
    uint32_t offered = get32(udp_buf + 16);
    const uint8_t* sid = dhcp_option(udp_buf + 240, udp_len - 240, 54);
    uint32_t server = sid ? get32(sid) : 0;
    if (verbose) { vga_write(" offer "); net_print_ip(offered); vga_write(", request..."); }

    if (!dhcp_exchange(DHCP_REQUEST, offered, server, DHCP_ACK)) {
        udp_wait_port = 0;
        if (verbose) vga_write(" no ack\n");
        return 0;
    }
    udp_wait_port = 0;
    const uint8_t* opts = udp_buf + 240;
    int olen = udp_len - 240;
    const uint8_t* v;
    my_ip = get32(udp_buf + 16);
    netmask = (v = dhcp_option(opts, olen, 1)) ? get32(v) : 0xFFFFFF00;
    gateway = (v = dhcp_option(opts, olen, 3)) ? get32(v) : 0;
    dns_server = (v = dhcp_option(opts, olen, 6)) ? get32(v) : gateway;
    if (verbose) { vga_write(" ack\n"); net_ifconfig(); }
    return 1;
}

/* ================================================================ */
/*                               DNS                                */
/* ================================================================ */
static int dns_skip_name(const uint8_t* p, int pos, int len) {
    while (pos < len) {
        if (p[pos] == 0) return pos + 1;
        if ((p[pos] & 0xC0) == 0xC0) return pos + 2;
        pos += p[pos] + 1;
    }
    return -1;
}

static int dns_resolve(const char* name, uint32_t* ip) {
    if (!my_ip || !dns_server) return 0;
    uint8_t* q = out + 8;
    zero(q, 12);
    put16(q, 0x4B4F);
    put16(q + 2, 0x0100);                                 /* recursion desired */
    put16(q + 4, 1);
    int pos = 12;
    while (*name && *name != ' ') {
        int label = pos++, n = 0;
        while (*name && *name != '.' && *name != ' ' && n < 63) { q[pos++] = (uint8_t)*name++; n++; }
        if (!n || pos > 260) return 0;
        q[label] = (uint8_t)n;
        if (*name == '.') name++;
    }
    q[pos++] = 0;
    put16(q + pos, 1); put16(q + pos + 2, 1);             /* type A, class IN */
    pos += 4;

    udp_wait_port = DNS_PORT;
    udp_got = 0;
    int ok = udp_send(dns_server, DNS_PORT, 53, pos) && wait_flag(&udp_got, 3000) > 0;
    udp_wait_port = 0;
    if (!ok || udp_len < 12 || get16(udp_buf) != 0x4B4F || (udp_buf[3] & 0x0F)) return 0;

    int answers = get16(udp_buf + 6);
    int p = dns_skip_name(udp_buf, 12, udp_len);
    if (p < 0) return 0;
    p += 4;
    while (answers-- > 0 && p > 0 && p < udp_len) {
        p = dns_skip_name(udp_buf, p, udp_len);
        if (p < 0 || p + 10 > udp_len) return 0;
        uint16_t type = get16(udp_buf + p);
        uint16_t rdlen = get16(udp_buf + p + 8);
        p += 10;
        if (type == 1 && rdlen == 4 && p + 4 <= udp_len) { *ip = get32(udp_buf + p); return 1; }
        p += rdlen;
    }
    return 0;
}

/* ================================================================ */
/*                          Shell commands                          */
/* ================================================================ */
int net_init(void) {
    if (!e1000_init()) return 0;
    copy(my_mac, e1000_mac(), 6);
    tick();
    return 1;
}

uint32_t net_ip(void) { return my_ip; }

void net_ifconfig(void) {
    if (!e1000_present()) { vga_write("net: no network card (start QEMU with -nic user,model=e1000)\n"); return; }
    vga_write("eth0  e1000  mac "); print_mac(my_mac);
    vga_write(e1000_link_up() ? "  link up\n" : "  link DOWN\n");
    vga_write("      ip "); net_print_ip(my_ip);
    vga_write("  mask "); net_print_ip(netmask);
    vga_write("\n      gw "); net_print_ip(gateway);
    vga_write("  dns "); net_print_ip(dns_server);
    vga_write("\n      rx "); print_dec(rx_count);
    vga_write(" frames  tx "); print_dec(tx_count);
    vga_write(" frames\n");
}

static int lookup_host(const char* host, uint32_t* ip) {
    if (parse_ip(host, ip)) return 1;
    if (dns_resolve(host, ip)) return 1;
    vga_write("Cannot resolve ");
    vga_write(host);
    vga_newline();
    return 0;
}

void net_host(const char* name) {
    uint32_t ip;
    if (!my_ip) { vga_write("net: no IP address, run 'dhcp' first\n"); return; }
    if (!lookup_host(name, &ip)) return;
    vga_write(name);
    vga_write(" has address ");
    net_print_ip(ip);
    vga_newline();
}

void net_ping(const char* host, int count) {
    uint32_t dst;
    int received = 0;
    if (!my_ip) { vga_write("net: no IP address, run 'dhcp' first\n"); return; }
    if (!lookup_host(host, &dst)) return;

    vga_write("PING ");
    net_print_ip(dst);
    vga_write(" (32 bytes of data, ESC to stop)\n");

    for (int i = 1; i <= count; i++) {
        ping_seq = (uint16_t)i;
        ping_got = 0;
        out[0] = 8; out[1] = 0;
        put16(out + 2, 0);
        put16(out + 4, PING_ID);
        put16(out + 6, ping_seq);
        for (int j = 0; j < 32; j++) out[8 + j] = (uint8_t)('a' + j % 23);
        put16(out + 2, checksum(out, 40));

        uint32_t start = now_ms;
        if (!ip_send(dst, IP_ICMP, 40)) { vga_write("Destination unreachable (no ARP reply)\n"); return; }
        int r = wait_flag(&ping_got, 1000);
        if (r < 0) break;
        if (r) {
            received++;
            vga_write("Reply from ");
            net_print_ip(dst);
            vga_write(": seq=");
            print_dec(ping_seq);
            vga_write(" ttl=");
            print_dec(ping_ttl);
            vga_write(" time=");
            print_dec(now_ms - start);
            vga_write(" ms\n");
            static int never;                             /* pace pings 1 s apart */
            uint32_t spent = now_ms - start;
            if (i < count && spent < 1000 && wait_flag(&never, 1000 - spent) < 0) break;
        } else {
            vga_write("Request timed out: seq=");
            print_dec(ping_seq);
            vga_newline();
        }
    }
    vga_write("--- ");
    print_dec(received);
    vga_write(" of ");
    print_dec(count);
    vga_write(" replies received ---\n");
}

/* SYN -> SYN-ACK (open) or RST (closed); slirp only answers SYN-ACK after a real host connect */
void net_tcping(const char* host, uint16_t port, int count) {
    static uint16_t next_port = 50000;
    static int never;
    uint32_t dst;
    int answered = 0;
    if (!my_ip) { vga_write("net: no IP address, run 'dhcp' first\n"); return; }
    if (!lookup_host(host, &dst)) return;

    vga_write("TCPING ");
    net_print_ip(dst);
    vga_putchar(':');
    print_dec(port);
    vga_write(" (ESC to stop)\n");

    for (int i = 1; i <= count; i++) {
        uint16_t sport = next_port++;
        if (next_port > 60000) next_port = 50000;
        uint32_t iss = 0x4B000000u + ((uint32_t)sport << 8);
        tcp_peer = dst;
        tcp_peer_port = port;
        tcp_local_port = sport;
        tcp_got = 0;

        uint32_t start = now_ms;
        if (!tcp_send(dst, sport, port, iss, 0, TCP_SYN)) {
            vga_write("Destination unreachable (no ARP reply)\n");
            break;
        }
        int r = wait_flag(&tcp_got, 3000);
        uint32_t spent = now_ms - start;
        tcp_local_port = 0;
        if (r < 0) break;

        if (r && (tcp_flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK) && tcp_their_ack == iss + 1) {
            tcp_send(dst, sport, port, iss + 1, 0, TCP_RST);   /* abort the handshake */
            answered++;
            vga_write("Connected: seq=");
        } else if (r && (tcp_flags & TCP_RST)) {
            answered++;
            vga_write("Port closed (RST): seq=");
        } else {
            vga_write("Timed out (3000 ms): seq=");
            print_dec((uint32_t)i);
            vga_newline();
            continue;
        }
        print_dec((uint32_t)i);
        vga_write(" time=");
        print_dec(spent);
        vga_write(" ms\n");
        if (i < count && spent < 1000 && wait_flag(&never, 1000 - spent) < 0) break;
    }
    tcp_local_port = 0;
    vga_write("--- ");
    print_dec((uint32_t)answered);
    vga_write(" of ");
    print_dec((uint32_t)count);
    vga_write(" answered ---\n");
}
