#include <stdint.h>
#include <stddef.h>
#include "protocol.h"

int serialize_header(const PacketHeader *header,
                     uint8_t buffer[HEADER_SIZE]) {

    if (header == NULL || buffer == NULL)
        return -1;

    if (header->version != PROTOCOL_VERSION)
        return -1;

    if (header->payload_length > MAX_PAYLOAD_SIZE)
        return -1;

    // Byte 0: Protocol version
    buffer[0] = header->version;

    // Bytes 1-2: Message type (16-bit)
    buffer[1] = (uint8_t)(header->type >> 8);
    buffer[2] = (uint8_t)(header->type);

    // Bytes 3-10: Transfer ID (64-bit)
    for (int i = 0; i < 8; i++) 
        buffer[3 + i] = (uint8_t)(header->transfer_id >> (56 - 8 * i));
    

    // Bytes 11-14: Sequence number (32-bit)
    for (int i = 0; i < 4; i++) 
        buffer[11 + i] = (uint8_t)(header->sequence_number >> (24 - 8 * i));
    

    // Bytes 15-18: Payload length (32-bit)
    for (int i = 0; i < 4; i++) 
        buffer[15 + i] = (uint8_t)(header->payload_length >> (24 - 8 * i));
    

    return 0;
}

int deserialize_header(const uint8_t buffer[HEADER_SIZE],
                       PacketHeader *header) {

    if (buffer == NULL || header == NULL)
        return -1;

    // Byte 0: Protocol version
    header->version = buffer[0];

    if (header->version != PROTOCOL_VERSION)
        return -1;

    // Bytes 1-2: Message type
    header->type =
        ((uint16_t)buffer[1] << 8) |
        (uint16_t)buffer[2];

    // Bytes 3-10: Transfer ID
    header->transfer_id = 0;

    for (int i = 0; i < 8; i++) {
        header->transfer_id =
            (header->transfer_id << 8) | buffer[3 + i];
    }

    // Bytes 11-14: Sequence number
    header->sequence_number = 0;

    for (int i = 0; i < 4; i++) {
        header->sequence_number =
            (header->sequence_number << 8) | buffer[11 + i];
    }

    // Bytes 15-18: Payload length
    header->payload_length = 0;

    for (int i = 0; i < 4; i++) {
        header->payload_length =
            (header->payload_length << 8) | buffer[15 + i];
    }

    // Validate payload size
    if (header->payload_length > MAX_PAYLOAD_SIZE)
        return -1;

    return 0;
}


