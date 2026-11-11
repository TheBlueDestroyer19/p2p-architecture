#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#include "protocol.h"

typedef struct {
    PacketHeader header;
    uint8_t *payload;
} Packet;

int send_packet(int sockfd, const Packet *packet);

int recv_packet(int sockfd, Packet *packet);

void free_packet(Packet *packet);

#endif
