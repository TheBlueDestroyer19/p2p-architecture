
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "receiver.h"
#include "packet.h"
#include "protocol.h"
#include "transfer.h"
#include "metadata.h"
#include "file_receiver.h"


/*
 * Send an ACK for a successfully processed packet.
 */
static int send_ack(
    int client_fd,
    uint64_t transfer_id,
    uint32_t sequence_number
) {

    Packet ack = {0};

    ack.header.version = PROTOCOL_VERSION;
    ack.header.type = MSG_ACK;
    ack.header.transfer_id = transfer_id;
    ack.header.sequence_number = sequence_number;
    ack.header.payload_length = 0;
    ack.payload = NULL;

    return send_packet(client_fd, &ack);
}


/*
 * Main receiving loop for one connected client.
 */
int receive_loop(int client_fd) {

    TransferState state;
    FileMetadata metadata = {0};
    FileReceiver file_receiver;

    init_transfer_state(&state);

    /*
     * Initialize destination directory.
     */
    if (file_receiver_init(&file_receiver) < 0) {

        fprintf(stderr, "Failed to initialize file receiver.\n");
        return -1;
    }

    int status = 0;

    while (1) {

        Packet packet = {0};

        /*
         * Receive a complete packet.
         */
        int result = recv_packet(client_fd, &packet);

        if (result == 0) {

            printf("Client disconnected unexpectedly.\n");
            goto cleanup;
        }

        if (result < 0) {

            fprintf(stderr, "Packet reception failed.\n");

            status = -1;
            goto cleanup;
        }

        /*
         * Process received packet.
         */
        switch (packet.header.type) {


            /* -------------------------------- */
            /* TRANSFER START                   */
            /* -------------------------------- */

            case MSG_TRANSFER_START: {

                printf("\nReceived TRANSFER_START.\n");

                /*
                 * Validate transfer ID and sequence.
                 */
                if (packet.header.sequence_number != 0 ||
                    packet.header.transfer_id == 0) {

                    fprintf(stderr,
                            "Invalid TRANSFER_START header.\n");

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                /*
                 * Decode filename and file size.
                 */
                if (deserialize_metadata(
                        packet.payload,
                        packet.header.payload_length,
                        &metadata
                    ) < 0) {

                    fprintf(stderr,
                            "Invalid TRANSFER_START metadata.\n");

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                /*
                 * Initialize transfer state.
                 */
                if (start_transfer(
                        &state,
                        packet.header.transfer_id
                    ) < 0) {

                    fprintf(stderr,
                            "Failed to initialize transfer state.\n");

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                /*
                 * Create destination file and initialize SHA-256.
                 */
                if (file_receiver_begin(
                        &file_receiver,
                        &metadata
                    ) < 0) {

                    fprintf(stderr,
                            "Failed to create destination file.\n");

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                printf("Transfer ID: %llu\n",
                       (unsigned long long)state.transfer_id);

                printf("Filename: %s\n",
                       metadata.filename);

                printf("Expected size: %llu bytes\n",
                       (unsigned long long)metadata.file_size);

                /*
                 * Acknowledge START only after initialization.
                 */
                if (send_ack(
                        client_fd,
                        packet.header.transfer_id,
                        packet.header.sequence_number
                    ) < 0) {

                    fprintf(stderr,
                            "Failed to acknowledge START.\n");

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                if (advance_transfer_sequence(&state) < 0) {

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                break;
            }


            /* -------------------------------- */
            /* DATA                             */
            /* -------------------------------- */

            case MSG_DATA: {

                /*
                 * Validate transfer ID and sequence.
                 */
                if (validate_transfer_packet(
                        &state,
                        packet.header.transfer_id,
                        packet.header.sequence_number
                    ) < 0 ||
                    packet.header.payload_length == 0) {

                    fprintf(stderr,
                            "Invalid DATA packet or sequence.\n");

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                /*
                 * Write the received chunk and update SHA-256.
                 */
                if (file_receiver_write(
                        &file_receiver,
                        packet.payload,
                        packet.header.payload_length
                    ) < 0) {

                    fprintf(stderr,
                            "Failed to write DATA chunk.\n");

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                printf(
                    "Received chunk: seq=%u, size=%u, "
                    "total=%llu/%llu bytes\n",
                    packet.header.sequence_number,
                    packet.header.payload_length,
                    (unsigned long long)file_receiver.received_size,
                    (unsigned long long)file_receiver.expected_size
                );

                /*
                 * Acknowledge only after successful writing.
                 */
                if (send_ack(
                        client_fd,
                        packet.header.transfer_id,
                        packet.header.sequence_number
                    ) < 0) {

                    fprintf(stderr, "Failed to send DATA ACK.\n");

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                if (advance_transfer_sequence(&state) < 0) {

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                break;
            }


            /* -------------------------------- */
            /* TRANSFER END                     */
            /* -------------------------------- */

            case MSG_TRANSFER_END: {

                /*
                 * END must carry exactly 32 digest bytes.
                 */
                if (packet.header.payload_length !=
                        SHA256_DIGEST_SIZE ||
                    validate_transfer_packet(
                        &state,
                        packet.header.transfer_id,
                        packet.header.sequence_number
                    ) < 0) {

                    fprintf(stderr,
                            "Invalid TRANSFER_END packet.\n");

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                printf("\nReceived TRANSFER_END.\n");

                /*
                 * Verify file size and SHA-256.
                 */
                if (file_receiver_finish(
                        &file_receiver,
                        packet.payload,
                        packet.header.payload_length
                    ) < 0) {

                    fprintf(stderr,
                            "File integrity verification failed.\n");

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                /*
                 * File is now complete and verified.
                 */
                printf("Transfer completed successfully.\n");

                /*
                 * Acknowledge END only after successful verification.
                 */
                if (send_ack(
                        client_fd,
                        packet.header.transfer_id,
                        packet.header.sequence_number
                    ) < 0) {

                    fprintf(stderr,
                            "Failed to acknowledge TRANSFER_END.\n");

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                /*
                 * Reset transfer state.
                 */
                if (complete_transfer(&state) < 0) {

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                memset(&metadata, 0, sizeof(metadata));

                break;
            }


            /* -------------------------------- */
            /* DISCONNECT                       */
            /* -------------------------------- */

            case MSG_DISCONNECT: {

                printf("\nClient requested disconnection.\n");

                /*
                 * DISCONNECT is connection-level.
                 */
                if (packet.header.payload_length != 0 ||
                    packet.header.transfer_id != 0 ||
                    packet.header.sequence_number != 0) {

                    fprintf(stderr,
                            "Invalid DISCONNECT packet.\n");

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                /*
                 * Discard any incomplete transfer.
                 */
                file_receiver_abort(&file_receiver);
                abort_transfer(&state);

                memset(&metadata, 0, sizeof(metadata));

                /*
                 * Acknowledge disconnect.
                 */
                if (send_ack(client_fd, 0, 0) < 0) {

                    fprintf(stderr,
                            "Failed to acknowledge disconnection.\n");

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                printf("Disconnect request acknowledged.\n");

                free_packet(&packet);
                goto cleanup;
            }


            /* -------------------------------- */
            /* UNKNOWN MESSAGE                  */
            /* -------------------------------- */

            default: {

                fprintf(stderr,
                        "Unexpected message type: 0x%04X\n",
                        packet.header.type);

                free_packet(&packet);
                status = -1;
                goto cleanup;
            }
        }

        /*
         * Release successfully processed packet.
         */
        free_packet(&packet);
    }


cleanup:

    /*
     * Reset transfer state and release resources.
     *
     * If the file is incomplete, it is removed.
     * Successfully completed files are preserved.
     */
    abort_transfer(&state);

    file_receiver_destroy(&file_receiver);

    return status;
}

