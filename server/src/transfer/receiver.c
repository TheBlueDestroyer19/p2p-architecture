#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "receiver.h"
#include "packet.h"
#include "protocol.h"
#include "transfer.h"
#include "metadata.h"
#include "file_receiver.h"
#include "connection_manager.h"


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
 *
 * Each worker invokes this function independently.
 * slot_id identifies the client's connection-table entry.
 */
int receive_loop(int client_fd, int slot_id) {

    TransferState state;
    FileMetadata metadata = {0};
    FileReceiver file_receiver;

    init_transfer_state(&state);

    /*
     * Initialize destination directory.
     */
    if (file_receiver_init(&file_receiver) < 0) {
        fprintf(stderr,
                "[Client %d] Failed to initialize file receiver.\n",
                slot_id);
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

            printf("[Client %d] Client disconnected.\n", slot_id);

            goto cleanup;
        }

        if (result < 0) {

            fprintf(stderr,
                    "[Client %d] Packet reception failed.\n",
                    slot_id);

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

                printf("\n[Client %d] Received TRANSFER_START.\n",
                       slot_id);

                /*
                 * Validate transfer ID and sequence.
                 */
                if (packet.header.sequence_number != 0 ||
                    packet.header.transfer_id == 0) {

                    fprintf(stderr,
                            "[Client %d] Invalid TRANSFER_START header.\n",
                            slot_id);

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
                            "[Client %d] Invalid TRANSFER_START metadata.\n",
                            slot_id);

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                /*
                 * Atomically reserve the connection for this transfer.
                 *
                 * This operation checks the shutdown state and changes
                 * the client activity to CLIENT_TRANSFERRING under
                 * the connection manager mutex.
                 */
                int reservation =
                    connection_manager_try_start_transfer(slot_id);

                if (reservation == 1) {

                    printf("[Client %d] Transfer rejected: "
                           "server is shutting down.\n",
                           slot_id);

                    free_packet(&packet);
                    goto cleanup;
                }

                if (reservation < 0) {

                    fprintf(stderr,
                            "[Client %d] Failed to reserve connection "
                            "for transfer.\n",
                            slot_id);

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
                            "[Client %d] Failed to initialize transfer state.\n",
                            slot_id);

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                /*
                 * Create destination file and initialize SHA-256.
                 */
                if (file_receiver_begin(
                        &file_receiver,
                        &metadata,
                        packet.header.transfer_id
                    ) < 0) {

                    fprintf(stderr,
                            "[Client %d] Failed to create destination file.\n",
                            slot_id);

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                printf("[Client %d] Transfer ID: %llu\n",
                       slot_id,
                       (unsigned long long)state.transfer_id);

                printf("[Client %d] Filename: %s\n",
                       slot_id,
                       metadata.filename);

                printf("[Client %d] Expected size: %llu bytes\n",
                       slot_id,
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
                            "[Client %d] Failed to acknowledge START.\n",
                            slot_id);

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
                            "[Client %d] Invalid DATA packet or sequence.\n",
                            slot_id);

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
                            "[Client %d] Failed to write DATA chunk.\n",
                            slot_id);

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                printf(
                    "[Client %d] Received chunk: seq=%u, size=%u, "
                    "total=%llu/%llu bytes\n",
                    slot_id,
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

                    fprintf(stderr,
                            "[Client %d] Failed to send DATA ACK.\n",
                            slot_id);

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
                            "[Client %d] Invalid TRANSFER_END packet.\n",
                            slot_id);

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                printf("\n[Client %d] Received TRANSFER_END.\n",
                       slot_id);

                /*
                 * Verify file size and SHA-256.
                 */
                if (file_receiver_finish(
                        &file_receiver,
                        packet.payload,
                        packet.header.payload_length
                    ) < 0) {

                    fprintf(stderr,
                            "[Client %d] File integrity verification failed.\n",
                            slot_id);

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                /*
                 * File is now complete and verified.
                 */
                printf("[Client %d] Transfer completed successfully.\n",
                       slot_id);

                /*
                 * Acknowledge END only after successful verification.
                 */
                if (send_ack(
                        client_fd,
                        packet.header.transfer_id,
                        packet.header.sequence_number
                    ) < 0) {

                    fprintf(stderr,
                            "[Client %d] Failed to acknowledge TRANSFER_END.\n",
                            slot_id);

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

                /*
                 * Transfer completed. Mark client as idle.
                 */
                if (connection_manager_set_activity(
                        slot_id,
                        CLIENT_IDLE
                    ) < 0) {

                    fprintf(stderr,
                            "[Client %d] Failed to update client activity.\n",
                            slot_id);

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                /*
                 * During server shutdown, do not wait for
                 * another transfer from this client.
                 */
                if (connection_manager_is_shutting_down()) {

                    printf("[Client %d] Closing connection "
                           "after completed transfer (shutdown).\n",
                           slot_id);

                    free_packet(&packet);
                    goto cleanup;
                }

                break;
            }


            /* -------------------------------- */
            /* DISCONNECT                       */
            /* -------------------------------- */

            case MSG_DISCONNECT: {

                printf("\n[Client %d] Client requested disconnection.\n",
                       slot_id);

                /*
                 * DISCONNECT is connection-level.
                 */
                if (packet.header.payload_length != 0 ||
                    packet.header.transfer_id != 0 ||
                    packet.header.sequence_number != 0) {

                    fprintf(stderr,
                            "[Client %d] Invalid DISCONNECT packet.\n",
                            slot_id);

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
                            "[Client %d] Failed to acknowledge disconnection.\n",
                            slot_id);

                    free_packet(&packet);
                    status = -1;
                    goto cleanup;
                }

                printf("[Client %d] Disconnect request acknowledged.\n",
                       slot_id);

                free_packet(&packet);
                goto cleanup;
            }


            /* -------------------------------- */
            /* UNKNOWN MESSAGE                  */
            /* -------------------------------- */

            default: {

                fprintf(stderr,
                        "[Client %d] Unexpected message type: 0x%04X\n",
                        slot_id,
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

    printf("[Client %d] Receiver cleanup completed.\n", slot_id);

    return status;
}

