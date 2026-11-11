
#ifndef METADATA_H
#define METADATA_H

#include <stdint.h>

#define MAX_FILENAME_LENGTH 255
#define METADATA_FIXED_SIZE 10

typedef struct {
    char filename[MAX_FILENAME_LENGTH + 1];
    uint64_t file_size;
} FileMetadata;

/*
 * Serialize filename and file size into a metadata payload.
 * The caller owns the allocated payload and must free it.
 */
int serialize_metadata(
    const char *filename,
    uint64_t file_size,
    uint8_t **payload,
    uint32_t *payload_length
);

/*
 * Deserialize a metadata payload into FileMetadata.
 */
int deserialize_metadata(
    const uint8_t *payload,
    uint32_t payload_length,
    FileMetadata *metadata
);

#endif

