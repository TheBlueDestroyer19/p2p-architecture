#include <stdint.h>
#include <stddef.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>

#include "packet.h"
#include "protocol.h"
#include "network.h"

void free_packet(Packet *packet) {

    if (packet == NULL)
        return;

    free(packet->payload);

    packet->payload = NULL;
    memset(&packet->header, 0, sizeof(PacketHeader));
}

int send_packet(int sockfd, const Packet *packet) {

    if (packet == NULL)
        return -1;

    const PacketHeader *header = &packet->header;

    // Validate payload length
    if (header->payload_length > MAX_PAYLOAD_SIZE)
        return -1;

    // A non-zero payload requires a valid pointer
    if (header->payload_length > 0 && packet->payload == NULL)
        return -1;

    // Serialize the header
    uint8_t buffer[HEADER_SIZE];

    if (serialize_header(header, buffer) < 0)
        return -1;

    // Send the fixed-size header
    if (send_all(sockfd, buffer, HEADER_SIZE) < 0)
        return -1;

    // Send the payload, if present
    if (header->payload_length > 0) {
        if (send_all(sockfd,
                     packet->payload,
                     header->payload_length) < 0)
            return -1;
    }

    return 0;
}

int recv_packet(int sockfd, Packet *packet) {

    if (packet == NULL)
        return -1;

    packet->payload = NULL;

    // Step 1: Receive the fixed-size header
    uint8_t buffer[HEADER_SIZE];

    ssize_t received = recv_all(sockfd, buffer, HEADER_SIZE);

    if (received == 0)
        return 0;  // Clean connection closure

    if (received < 0 || received != HEADER_SIZE)
        return -1; // Error or incomplete header

    // Step 2: Deserialize and validate the header
    if (deserialize_header(buffer, &packet->header) < 0)
        return -1;

    uint32_t payload_length = packet->header.payload_length;

    // Step 3: Handle zero-length payload
    if (payload_length == 0) {
        packet->payload = NULL;
        return 1;
    }

    // Step 4: Allocate memory for payload
    packet->payload = malloc(payload_length);

    if (packet->payload == NULL)
        return -1;

    // Step 5: Receive the complete payload
    received = recv_all(sockfd,
                        packet->payload,
                        payload_length);

    if (received != (ssize_t)payload_length) {
        free(packet->payload);
        packet->payload = NULL;
        return -1;
    }

    return 1;
}
