
#ifndef FILE_RECEIVER_H
#define FILE_RECEIVER_H

#include <stdint.h>
#include <openssl/evp.h>

#include "metadata.h"

#define RECEIVED_DIRECTORY "p2p_recieved"
#define SHA256_DIGEST_SIZE 32

/* Original filename + '_' + 16 hexadecimal transaction ID characters. */
#define STORED_FILENAME_MAX_LENGTH (MAX_FILENAME_LENGTH + 17)

typedef struct {
    int directory_fd;
    int file_fd;

    uint64_t expected_size;
    uint64_t received_size;

    char filename[STORED_FILENAME_MAX_LENGTH + 1];

    EVP_MD_CTX *hash_ctx;

    int active;
} FileReceiver;

/* Initialize the receiving directory. */
int file_receiver_init(FileReceiver *receiver);

/* Create a destination file using metadata and transaction ID. */
int file_receiver_begin(
    FileReceiver *receiver,
    const FileMetadata *metadata,
    uint64_t transfer_id
);

/* Write one received DATA chunk and update SHA-256. */
int file_receiver_write(
    FileReceiver *receiver,
    const uint8_t *data,
    uint32_t length
);

/* Verify size and SHA-256, then finalize the file. */
int file_receiver_finish(
    FileReceiver *receiver,
    const uint8_t *expected_digest,
    uint32_t digest_length
);

/* Remove incomplete file and reset transfer state. */
void file_receiver_abort(FileReceiver *receiver);

/* Release receiver resources. */
void file_receiver_destroy(FileReceiver *receiver);

#endif

