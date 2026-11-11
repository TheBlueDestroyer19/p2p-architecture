#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include "sender.h"
#include "packet.h"
#include "protocol.h"
#include "transfer.h"
#include "metadata.h"

#define PATH_BUFFER_SIZE 4096

static int wait_for_ack(int sockfd, uint64_t transfer_id,
                        uint32_t sequence_number) {

    Packet ack = {0};

    int result = recv_packet(sockfd, &ack);

    if (result != 1) {
        fprintf(stderr, "Failed to receive ACK.\n");
        return -1;
    }

    int valid =
        ack.header.version == PROTOCOL_VERSION &&
        ack.header.type == MSG_ACK &&
        ack.header.transfer_id == transfer_id &&
        ack.header.sequence_number == sequence_number &&
        ack.header.payload_length == 0;

    free_packet(&ack);

    if (!valid) {
        fprintf(stderr, "Invalid or unexpected ACK.\n");
        return -1;
    }

    return 0;
}


/* Send a packet and wait for its corresponding ACK. */
static int send_with_ack(int sockfd, const Packet *packet) {

    if (send_packet(sockfd, packet) < 0) {
        fprintf(stderr, "Packet transmission failed.\n");
        return -1;
    }

    return wait_for_ack(
        sockfd,
        packet->header.transfer_id,
        packet->header.sequence_number
    );
}


/* Extract the filename from a local path. */
static const char *get_filename(const char *path) {

    const char *separator = strrchr(path, '/');

    return separator == NULL ? path : separator + 1;
}


/* Open a regular file and determine its size. */
static int open_input_file(const char *path, FILE **file,
                           uint64_t *file_size) {

    struct stat info;

    if (stat(path, &info) < 0) {
        perror("stat");
        return -1;
    }

    if (!S_ISREG(info.st_mode) || info.st_size < 0) {
        fprintf(stderr, "Path is not a regular file.\n");
        return -1;
    }

    FILE *fp = fopen(path, "rb");

    if (fp == NULL) {
        perror("fopen");
        return -1;
    }

    *file_size = (uint64_t)info.st_size;
    *file = fp;

    return 0;
}


/* Begin a transfer by sending serialized file metadata. */
static int send_transfer_start(int sockfd, TransferState *state,
                               const char *filename,
                               uint64_t file_size) {

    uint64_t transfer_id;

    if (generate_transfer_id(&transfer_id) < 0)
        return -1;

    if (start_transfer(state, transfer_id) < 0)
        return -1;

    uint8_t *payload = NULL;
    uint32_t payload_length = 0;

    if (serialize_metadata(
            filename,
            file_size,
            &payload,
            &payload_length) < 0) {

        abort_transfer(state);
        return -1;
    }

    Packet packet = {0};

    packet.header.version = PROTOCOL_VERSION;
    packet.header.type = MSG_TRANSFER_START;
    packet.header.transfer_id = state->transfer_id;
    packet.header.sequence_number = state->next_sequence_number;
    packet.header.payload_length = payload_length;
    packet.payload = payload;

    printf("Starting transfer: %s (%llu bytes)\n",
           filename, (unsigned long long)file_size);

    int result = send_with_ack(sockfd, &packet);

    /* The metadata serializer allocated this buffer. */
    free(payload);

    if (result < 0) {
        abort_transfer(state);
        return -1;
    }

    if (advance_transfer_sequence(state) < 0) {
        abort_transfer(state);
        return -1;
    }

    printf("START acknowledged.\n");
    return 0;
}


/* Send one binary chunk. */
static int send_data(int sockfd, TransferState *state,
                     const uint8_t *buffer, uint32_t length) {

    if (state->status != TRANSFER_ACTIVE ||
        state->next_sequence_number >= UINT32_MAX - 1U)
        return -1;

    Packet packet = {0};

    packet.header.version = PROTOCOL_VERSION;
    packet.header.type = MSG_DATA;
    packet.header.transfer_id = state->transfer_id;
    packet.header.sequence_number = state->next_sequence_number;
    packet.header.payload_length = length;
    packet.payload = (uint8_t *)buffer;

    if (send_with_ack(sockfd, &packet) < 0)
        return -1;

    return advance_transfer_sequence(state);
}


/* Finish the transfer after all file data is sent. */

static int send_transfer_end(int sockfd, TransferState *state,
                             const uint8_t *digest) {

    if (state->status != TRANSFER_ACTIVE || digest == NULL)
        return -1;

    Packet packet = {0};

    packet.header.version = PROTOCOL_VERSION;
    packet.header.type = MSG_TRANSFER_END;
    packet.header.transfer_id = state->transfer_id;
    packet.header.sequence_number = state->next_sequence_number;
    packet.header.payload_length = 32;
    packet.payload = (uint8_t *)digest;

    printf("Sending TRANSFER_END with SHA-256 digest...\n");

    if (send_with_ack(sockfd, &packet) < 0)
        return -1;

    if (complete_transfer(state) < 0)
        return -1;

    printf("Transfer completed and acknowledged.\n");
    return 0;
}



/* Send a connection-level disconnect request. */
static int send_disconnect(int sockfd) {

    Packet packet = {0};

    packet.header.version = PROTOCOL_VERSION;
    packet.header.type = MSG_DISCONNECT;
    packet.header.transfer_id = 0;
    packet.header.sequence_number = 0;
    packet.header.payload_length = 0;

    if (send_with_ack(sockfd, &packet) < 0)
        return -1;

    printf("Disconnect acknowledged.\n");
    return 0;
}


/* Transfer one complete file over the existing connection. */
static int send_file(int sockfd, const char *path) {

    FILE *file = NULL;
    uint64_t file_size = 0;

    if (open_input_file(path, &file, &file_size) < 0)
        return -1;

    const char *filename = get_filename(path);

    /*
     * Initialize SHA-256.
     */
    EVP_MD_CTX *hash_ctx = EVP_MD_CTX_new();

    if (hash_ctx == NULL) {
        fprintf(stderr, "Failed to create SHA-256 context.\n");
        fclose(file);
        return -1;
    }

    if (EVP_DigestInit_ex(hash_ctx, EVP_sha256(), NULL) != 1) {
        fprintf(stderr, "Failed to initialize SHA-256.\n");
        EVP_MD_CTX_free(hash_ctx);
        fclose(file);
        return -1;
    }

    TransferState state;
    init_transfer_state(&state);

    if (send_transfer_start(
            sockfd, &state, filename, file_size) < 0) {

        EVP_MD_CTX_free(hash_ctx);
        fclose(file);
        return -1;
    }

    uint8_t buffer[MAX_PAYLOAD_SIZE];
    uint64_t remaining = file_size;

    while (remaining > 0) {

        size_t chunk_size =
            remaining < sizeof(buffer)
                ? (size_t)remaining
                : sizeof(buffer);

        size_t bytes_read =
            fread(buffer, 1, chunk_size, file);

        if (bytes_read != chunk_size) {
            fprintf(stderr, "Failed to read file data.\n");
            abort_transfer(&state);
            EVP_MD_CTX_free(hash_ctx);
            fclose(file);
            return -1;
        }

        /*
         * Send the chunk and wait for its ACK.
         */
        if (send_data(
                sockfd, &state, buffer,
                (uint32_t)bytes_read) < 0) {

            fprintf(stderr, "Failed to transmit file data.\n");
            abort_transfer(&state);
            EVP_MD_CTX_free(hash_ctx);
            fclose(file);
            return -1;
        }

        /*
         * Hash the successfully acknowledged chunk.
         */
        if (EVP_DigestUpdate(
                hash_ctx, buffer, bytes_read) != 1) {

            fprintf(stderr, "SHA-256 update failed.\n");
            abort_transfer(&state);
            EVP_MD_CTX_free(hash_ctx);
            fclose(file);
            return -1;
        }

        remaining -= bytes_read;
    }

    if (fgetc(file) != EOF || ferror(file)) {
        fprintf(stderr, "File changed or read failed during transfer.\n");
        abort_transfer(&state);
        EVP_MD_CTX_free(hash_ctx);
        fclose(file);
        return -1;
    }

    fclose(file);

    /*
     * Produce the final SHA-256 digest.
     */
    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned int digest_length = 0;

    if (EVP_DigestFinal_ex(
            hash_ctx, digest, &digest_length) != 1 ||
        digest_length != 32) {

        fprintf(stderr, "SHA-256 finalization failed.\n");
        abort_transfer(&state);
        EVP_MD_CTX_free(hash_ctx);
        return -1;
    }

    EVP_MD_CTX_free(hash_ctx);

    printf("SHA-256 calculated successfully.\n");

    /*
     * Send digest in TRANSFER_END.
     */
    if (send_transfer_end(sockfd, &state, digest) < 0) {
        fprintf(stderr, "File transfer finalization failed.\n");
        abort_transfer(&state);
        return -1;
    }

    return 0;
}



int send_loop(int sockfd) {

    char path[PATH_BUFFER_SIZE];

    printf("Connected. Enter a file path to send.\n");
    printf("Type /quit to disconnect.\n\n");

    while (1) {

        printf("File: ");
        fflush(stdout);

        if (fgets(path, sizeof(path), stdin) == NULL) {
            printf("\nInput closed. Disconnecting...\n");
            return send_disconnect(sockfd);
        }

        size_t length = strlen(path);

        if (length > 0 && path[length - 1] == '\n') {
            path[length - 1] = '\0';
        } else if (!feof(stdin)) {
            int ch;

            while ((ch = getchar()) != '\n' && ch != EOF)
                ;

            fprintf(stderr, "Input path too long.\n");
            continue;
        }

        if (strcmp(path, "/quit") == 0)
            return send_disconnect(sockfd);

        if (path[0] == '\0')
            continue;

        if (send_file(sockfd, path) < 0) {
            fprintf(stderr, "File transfer failed.\n");

            /*
             * Once START may have been transmitted, the
             * server's transfer state is uncertain.
             * Close this session rather than sending
             * another transfer on the same connection.
             */
            return -1;
        }
    }
}

