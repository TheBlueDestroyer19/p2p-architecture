#include "transfer.h"

#include <sys/random.h>
#include <errno.h>
#include <stddef.h>

void init_transfer_state(TransferState *state) {
    if (state == NULL)
        return;

    state->status = TRANSFER_IDLE;
    state->transfer_id = 0;
    state->next_sequence_number = 0;
}

int generate_transfer_id(uint64_t *transfer_id) {
    if (transfer_id == NULL)
        return -1;

    uint64_t id = 0;
    size_t bytes_read = 0;

    while (bytes_read < sizeof(id)) {
        ssize_t result = getrandom(
            (uint8_t *)&id + bytes_read,
            sizeof(id) - bytes_read,
            0
        );

        if (result < 0) {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (result == 0)
            return -1;

        bytes_read += (size_t)result;
    }

    if (id == 0)
        return generate_transfer_id(transfer_id);

    *transfer_id = id;
    return 0;
}

int start_transfer(TransferState *state, uint64_t transfer_id) {
    if (state == NULL || transfer_id == 0)
        return -1;

    if (state->status != TRANSFER_IDLE)
        return -1;

    state->status = TRANSFER_ACTIVE;
    state->transfer_id = transfer_id;
    state->next_sequence_number = 0;

    return 0;
}

int validate_transfer_packet(
    const TransferState *state,
    uint64_t transfer_id,
    uint32_t sequence_number
) {
    if (state == NULL || state->status != TRANSFER_ACTIVE)
        return -1;

    if (state->transfer_id != transfer_id)
        return -1;

    if (state->next_sequence_number != sequence_number)
        return -1;

    return 0;
}

int advance_transfer_sequence(TransferState *state) {
    if (state == NULL || state->status != TRANSFER_ACTIVE)
        return -1;

    if (state->next_sequence_number == UINT32_MAX)
        return -1;

    state->next_sequence_number++;
    return 0;
}

int complete_transfer(TransferState *state) {
    if (state == NULL || state->status != TRANSFER_ACTIVE)
        return -1;

    init_transfer_state(state);
    return 0;
}

void abort_transfer(TransferState *state) {
    init_transfer_state(state);
}

