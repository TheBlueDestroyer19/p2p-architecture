#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* Protocol configuration */
#define PROTOCOL_VERSION 1
#define HEADER_SIZE 19
#define MAX_PAYLOAD_SIZE 65536

/* Message types */
typedef enum {
    MSG_DATA          = 0x0001,
    MSG_ACK           = 0x0002,
    MSG_ERROR         = 0x0003,
    MSG_TRANSFER_START = 0x0004,
    MSG_TRANSFER_END   = 0x0005,
    MSG_DISCONNECT = 0x0006
} MessageType;

/* Logical packet header */
typedef struct {
    uint8_t  version;
    uint16_t type;
    uint64_t transfer_id;
    uint32_t sequence_number;
    uint32_t payload_length;
} PacketHeader;

int serialize_header(const PacketHeader *header,uint8_t buffer[HEADER_SIZE]);
int deserialize_header(const uint8_t buffer[HEADER_SIZE], PacketHeader *header);

#endif

