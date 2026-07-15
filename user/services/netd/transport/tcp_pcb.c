#include "tcp_pcb.h"

tcp_pcb_t tcp_pcbs[TCP_MAX_PCB];

int tcp_pcb_alloc(void) {
    for (int i = 0; i < TCP_MAX_PCB; i++) {
        if (!tcp_pcbs[i].active) {
            tcp_pcbs[i].active = true;
            return i;
        }
    }
    return ERR_NOMEM;
}

void tcp_pcb_free(int h) {
    if (h < 0 || h > TCP_MAX_PCB - 1) return;
    tcp_pcbs[h].active = false;
}

int tcp_pcb_find(ipv4_addr_t local_ip, port_t local_port,
                 ipv4_addr_t remote_ip, port_t remote_port) {
    for (int i = 0; i < TCP_MAX_PCB; i++) {
        if (tcp_pcbs[i].local_ip == local_ip &&
            tcp_pcbs[i].local_port == local_port &&
            tcp_pcbs[i].remote_ip == remote_ip &&
            tcp_pcbs[i].remote_port == remote_port &&
            tcp_pcbs[i].active) {
            return i;
        }
    }
    return ERR_NOENT;
}

int tcp_pcb_find_listener(ipv4_addr_t local_ip, port_t local_port) {
    for (int i = 0; i < TCP_MAX_PCB; i++) {
        if (tcp_pcbs[i].active &&
            tcp_pcbs[i].state == TCP_LISTENING &&
            tcp_pcbs[i].local_ip == local_ip &&
            tcp_pcbs[i].local_port == local_port) {
            return i;
        }
    }
    return ERR_NOENT;
}
