
#ifndef TRANSFER_H
#define TRANSFER_H

#include <stdint.h>

typedef enum {
    TRANSFER_IDLE,
    TRANSFER_ACTIVE
} TransferStatus;

typedef struct {
    TransferStatus status;
    uint64_t transfer_id;
    uint32_t next_sequence_number;
} TransferState;

/* Initialize a transfer state. */
void init_transfer_state(TransferState *state);

/* Generate a random, nonzero 64-bit transfer ID. */
int generate_transfer_id(uint64_t *transfer_id);

/* Start a new transfer with the supplied ID. */
int start_transfer(TransferState *state, uint64_t transfer_id);

/* Validate an incoming or outgoing transfer packet. */
int validate_transfer_packet(
    const TransferState *state,
    uint64_t transfer_id,
    uint32_t sequence_number
);

/* Advance the sequence number after successful processing. */
int advance_transfer_sequence(TransferState *state);

/* Complete or abort the active transfer. */
int complete_transfer(TransferState *state);
void abort_transfer(TransferState *state);

#endif

