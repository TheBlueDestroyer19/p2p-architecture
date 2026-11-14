
#include "metadata.h"
#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int validate_filename(const char *filename, size_t length) {

    if (filename == NULL || length == 0 ||
        length > MAX_FILENAME_LENGTH)
        return -1;

    if ((length == 1 && filename[0] == '.') ||
        (length == 2 && filename[0] == '.' && filename[1] == '.'))
        return -1;

    for (size_t i = 0; i < length; i++) {

        unsigned char ch = (unsigned char)filename[i];

        if (ch == '\0' || ch == '/' || ch == '\\')
            return -1;

        /* Reject control characters. */
        if (ch < 32 || ch == 127)
            return -1;
    }

    return 0;
}


int serialize_metadata(
    const char *filename,
    uint64_t file_size,
    uint8_t **payload,
    uint32_t *payload_length
) {

    if (filename == NULL || payload == NULL ||
        payload_length == NULL)
        return -1;

    *payload = NULL;
    *payload_length = 0;

    /* Find filename length without reading beyond the limit. */
    size_t filename_length = 0;

    while (filename_length <= MAX_FILENAME_LENGTH &&
           filename[filename_length] != '\0') {
        filename_length++;
    }

    if (validate_filename(filename, filename_length) < 0) {
        fprintf(stderr, "Invalid filename.\n");
        return -1;
    }

    uint32_t total_length =
        METADATA_FIXED_SIZE + (uint32_t)filename_length;

    if (total_length > MAX_PAYLOAD_SIZE)
        return -1;

    uint8_t *buffer = malloc(total_length);

    if (buffer == NULL) {
        perror("malloc");
        return -1;
    }

    /* Filename length: 2 bytes, big-endian. */
    buffer[0] = (uint8_t)(filename_length >> 8);
    buffer[1] = (uint8_t)(filename_length);

    /* File size: 8 bytes, big-endian. */
    for (int i = 0; i < 8; i++) {
        buffer[2 + i] =
            (uint8_t)(file_size >> (56 - 8 * i));
    }

    /* Copy filename bytes. */
    memcpy(buffer + METADATA_FIXED_SIZE,
           filename, filename_length);

    *payload = buffer;
    *payload_length = total_length;

    return 0;
}


int deserialize_metadata(
    const uint8_t *payload,
    uint32_t payload_length,
    FileMetadata *metadata
) {

    if (payload == NULL || metadata == NULL)
        return -1;

    if (payload_length < METADATA_FIXED_SIZE ||
        payload_length > MAX_PAYLOAD_SIZE)
        return -1;

    /* Decode filename length. */
    uint16_t filename_length =
        ((uint16_t)payload[0] << 8) |
        (uint16_t)payload[1];

    if (filename_length == 0 ||
        filename_length > MAX_FILENAME_LENGTH)
        return -1;

    /* Payload must contain exactly the declared filename length. */
    if (payload_length != (uint32_t)METADATA_FIXED_SIZE + filename_length)
        return -1;

    const char *filename =
        (const char *)(payload + METADATA_FIXED_SIZE);

    if (validate_filename(filename, filename_length) < 0) {
        fprintf(stderr, "Received invalid filename.\n");
        return -1;
    }

    /* Decode file size. */
    uint64_t file_size = 0;

    for (int i = 0; i < 8; i++) {
        file_size = (file_size << 8) | payload[2 + i];
    }

    memcpy(metadata->filename, filename, filename_length);
    metadata->filename[filename_length] = '\0';

    metadata->file_size = file_size;

    return 0;
}

