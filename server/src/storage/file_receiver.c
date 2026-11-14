
#define _GNU_SOURCE

#include "file_receiver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <openssl/crypto.h>


/*
 * Initialize the receiving directory.
 */
int file_receiver_init(FileReceiver *receiver) {

    if (receiver == NULL)
        return -1;

    memset(receiver, 0, sizeof(*receiver));

    receiver->directory_fd = -1;
    receiver->file_fd = -1;
    receiver->hash_ctx = NULL;

    /*
     * Create directory if it doesn't exist.
     */
    if (mkdir(RECEIVED_DIRECTORY, 0755) < 0 &&
        errno != EEXIST) {

        perror("mkdir");
        return -1;
    }

    /*
     * Open the directory.
     * All destination files will be created relative to this FD.
     */
    receiver->directory_fd = open(
        RECEIVED_DIRECTORY,
        O_RDONLY | O_DIRECTORY | O_CLOEXEC
    );

    if (receiver->directory_fd < 0) {
        perror("open destination directory");
        return -1;
    }

    return 0;
}


/*
 * Begin receiving a new file.
 */

/*
 * Begin receiving a new file.
 * The transaction ID is appended to the destination filename.
 */
int file_receiver_begin(
    FileReceiver *receiver,
    const FileMetadata *metadata,
    uint64_t transfer_id
) {

    if (receiver == NULL || metadata == NULL ||
        receiver->directory_fd < 0 ||
        receiver->active ||
        transfer_id == 0)
        return -1;

    size_t filename_length =
        strnlen(metadata->filename, MAX_FILENAME_LENGTH + 1);

    if (filename_length == 0 ||
        filename_length > MAX_FILENAME_LENGTH ||
        strcmp(metadata->filename, ".") == 0 ||
        strcmp(metadata->filename, "..") == 0) {

        fprintf(stderr, "Invalid destination filename.\n");
        return -1;
    }

    /*
     * Validate the original filename.
     */
    for (size_t i = 0; i < filename_length; i++) {

        unsigned char ch =
            (unsigned char)metadata->filename[i];

        if (ch == '/' || ch == '\\' ||
            ch < 32 || ch == 127) {

            fprintf(stderr, "Invalid destination filename.\n");
            return -1;
        }
    }

    /*
     * Identify the extension.
     *
     * Examples:
     * report.pdf       -> .pdf
     * archive.tar.gz   -> .gz
     * README           -> no extension
     * .env             -> no extension
     */
    const char *extension =
        strrchr(metadata->filename, '.');

    if (extension == NULL ||
        extension == metadata->filename ||
        extension[1] == '\0') {

        extension = NULL;
    }

    size_t base_length = extension
        ? (size_t)(extension - metadata->filename)
        : filename_length;

    /*
     * Generate the destination filename.
     *
     * Example:
     * report.pdf
     * ->
     * report_00000000a1b2c3d4.pdf
     */
    char stored_filename[STORED_FILENAME_MAX_LENGTH + 1];

    int name_length = snprintf(
        stored_filename,
        sizeof(stored_filename),
        "%.*s_%016llx%s",
        (int)base_length,
        metadata->filename,
        (unsigned long long)transfer_id,
        extension ? extension : ""
    );

    if (name_length < 0 ||
        name_length > STORED_FILENAME_MAX_LENGTH) {

        fprintf(stderr, "Generated filename is too long.\n");
        return -1;
    }

    /*
     * Create destination file.
     *
     * O_EXCL prevents overwriting existing files.
     * O_NOFOLLOW prevents following symbolic links.
     */
    int fd = openat(
        receiver->directory_fd,
        stored_filename,
        O_WRONLY | O_CREAT | O_EXCL |
        O_NOFOLLOW | O_CLOEXEC,
        0600
    );

    if (fd < 0) {
        perror("openat destination file");
        return -1;
    }

    receiver->file_fd = fd;

    /*
     * Initialize SHA-256 context.
     */
    receiver->hash_ctx = EVP_MD_CTX_new();

    if (receiver->hash_ctx == NULL) {

        fprintf(stderr, "Failed to allocate SHA-256 context.\n");

        close(receiver->file_fd);
        receiver->file_fd = -1;

        unlinkat(receiver->directory_fd, stored_filename, 0);

        return -1;
    }

    if (EVP_DigestInit_ex(
            receiver->hash_ctx,
            EVP_sha256(),
            NULL
        ) != 1) {

        fprintf(stderr, "Failed to initialize SHA-256.\n");

        EVP_MD_CTX_free(receiver->hash_ctx);
        receiver->hash_ctx = NULL;

        close(receiver->file_fd);
        receiver->file_fd = -1;

        unlinkat(receiver->directory_fd, stored_filename, 0);

        return -1;
    }

    /*
     * Initialize receiver state.
     */
    receiver->expected_size = metadata->file_size;
    receiver->received_size = 0;

    memcpy(
        receiver->filename,
        stored_filename,
        (size_t)name_length + 1
    );

    receiver->active = 1;

    printf("Original filename: %s\n", metadata->filename);

    printf("Created destination file: %s/%s\n",
           RECEIVED_DIRECTORY,
           receiver->filename);

    return 0;
}



/*
 * Write a received DATA chunk.
 */
int file_receiver_write(
    FileReceiver *receiver,
    const uint8_t *data,
    uint32_t length
) {

    if (receiver == NULL ||
        !receiver->active ||
        receiver->file_fd < 0 ||
        receiver->hash_ctx == NULL ||
        (length > 0 && data == NULL))
        return -1;

    /*
     * Prevent receiving more bytes than announced.
     */
    if (receiver->received_size > receiver->expected_size ||
        (uint64_t)length >
        receiver->expected_size - receiver->received_size) {

        fprintf(stderr,
                "Received data exceeds expected file size.\n");

        return -1;
    }

    size_t written = 0;

    /*
     * Handle partial writes.
     */
    while (written < length) {

        ssize_t result = write(
            receiver->file_fd,
            data + written,
            length - written
        );

        if (result < 0) {

            if (errno == EINTR)
                continue;

            perror("write");
            return -1;
        }

        if (result == 0) {

            fprintf(stderr, "File write returned zero.\n");
            return -1;
        }

        written += (size_t)result;
    }

    /*
     * Update SHA-256 using the received bytes.
     *
     * Only update after the entire chunk has been written.
     */
    if (EVP_DigestUpdate(
            receiver->hash_ctx,
            data,
            length
        ) != 1) {

        fprintf(stderr, "SHA-256 update failed.\n");
        return -1;
    }

    receiver->received_size += length;

    return 0;
}


/*
 * Verify file size and SHA-256, then finalize the file.
 */
int file_receiver_finish(
    FileReceiver *receiver,
    const uint8_t *expected_digest,
    uint32_t digest_length
) {

    if (receiver == NULL ||
        !receiver->active ||
        receiver->file_fd < 0 ||
        receiver->hash_ctx == NULL ||
        expected_digest == NULL ||
        digest_length != SHA256_DIGEST_SIZE)
        return -1;

    /*
     * Verify total received bytes.
     */
    if (receiver->received_size != receiver->expected_size) {

        fprintf(stderr,
                "File size mismatch!\n"
                "Expected: %llu bytes\n"
                "Received: %llu bytes\n",
                (unsigned long long)receiver->expected_size,
                (unsigned long long)receiver->received_size);

        return -1;
    }

    /*
     * Finalize server-side SHA-256.
     */
    uint8_t actual_digest[EVP_MAX_MD_SIZE];
    unsigned int actual_length = 0;

    if (EVP_DigestFinal_ex(
            receiver->hash_ctx,
            actual_digest,
            &actual_length
        ) != 1 ||
        actual_length != SHA256_DIGEST_SIZE) {

        fprintf(stderr, "SHA-256 finalization failed.\n");
        return -1;
    }

    /*
     * Compare the calculated digest against
     * the digest supplied by the client.
     */
    if (CRYPTO_memcmp(
            actual_digest,
            expected_digest,
            SHA256_DIGEST_SIZE
        ) != 0) {

        fprintf(stderr, "SHA-256 verification FAILED!\n");
        return -1;
    }

    printf("SHA-256 verification successful!\n");

    /*
     * Release hash context.
     */
    EVP_MD_CTX_free(receiver->hash_ctx);
    receiver->hash_ctx = NULL;

    /*
     * Flush file contents to disk.
     */
    if (fsync(receiver->file_fd) < 0) {

        perror("fsync");
        return -1;
    }

    /*
     * Close the destination file.
     */
    int fd = receiver->file_fd;
    receiver->file_fd = -1;

    if (close(fd) < 0) {

        perror("close");
        return -1;
    }

    receiver->active = 0;

    printf("\nFile successfully received: %s/%s\n",
           RECEIVED_DIRECTORY,
           receiver->filename);

    printf("Total bytes received: %llu\n",
           (unsigned long long)receiver->received_size);

    return 0;
}


/*
 * Abort an incomplete transfer.
 */
void file_receiver_abort(FileReceiver *receiver) {

    if (receiver == NULL)
        return;

    /*
     * Release SHA-256 context.
     */
    if (receiver->hash_ctx != NULL) {

        EVP_MD_CTX_free(receiver->hash_ctx);
        receiver->hash_ctx = NULL;
    }

    /*
     * Close the destination file.
     */
    if (receiver->file_fd >= 0) {

        close(receiver->file_fd);
        receiver->file_fd = -1;
    }

    /*
     * Remove incomplete file.
     *
     * A completed file has active == 0 and is preserved.
     */
    if (receiver->active &&
        receiver->directory_fd >= 0) {

        if (unlinkat(
                receiver->directory_fd,
                receiver->filename,
                0
            ) < 0) {

            perror("unlinkat");
        } else {

            printf("Removed incomplete file: %s\n",
                   receiver->filename);
        }
    }

    receiver->active = 0;
    receiver->expected_size = 0;
    receiver->received_size = 0;
    receiver->filename[0] = '\0';
}


/*
 * Release all receiver resources.
 */
void file_receiver_destroy(FileReceiver *receiver) {

    if (receiver == NULL)
        return;

    file_receiver_abort(receiver);

    if (receiver->directory_fd >= 0) {

        close(receiver->directory_fd);
        receiver->directory_fd = -1;
    }
}

