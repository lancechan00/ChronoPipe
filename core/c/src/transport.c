#include "chronopipe/transport.h"

void cp_batch_sender_init(cp_batch_sender_t *sender,
                          cp_store_t store,
                          cp_batch_transport_t transport,
                          uint32_t max_retries) {
    if (sender == NULL) {
        return;
    }
    sender->store = store;
    sender->transport = transport;
    sender->next_seq = 0u;
    sender->pending_seq = 0u;
    sender->pending = false;
    sender->retries = 0u;
    sender->max_retries = max_retries;
}

cp_status_t cp_batch_sender_poll(cp_batch_sender_t *sender) {
    cp_block_view_t block;
    cp_status_t status;
    uint32_t sequence;
    if (sender == NULL || sender->store.ops == NULL || sender->transport.send == NULL) {
        return CP_EINVAL;
    }
    if (sender->pending) {
        if (sender->retries >= sender->max_retries) {
            return CP_ESTATE;
        }
        sequence = sender->pending_seq;
        ++sender->retries;
    } else {
        status = sender->store.ops->oldest(sender->store.ctx, &sequence);
        if (status != CP_OK) {
            return status;
        }
        sender->retries = 0u;
        sender->pending = true;
        sender->pending_seq = sequence;
    }
    status = sender->store.ops->read(sender->store.ctx, sequence, &block);
    if (status != CP_OK) {
        sender->pending = false;
        return status;
    }
    status = sender->transport.send(sender->transport.ctx, sequence, block.data, block.bytes);
    if (status != CP_OK) {
        return status;
    }
    sender->next_seq = sequence + 1u;
    return CP_OK;
}

cp_status_t cp_batch_sender_ack(cp_batch_sender_t *sender, uint32_t ack_through) {
    cp_status_t status;
    if (sender == NULL || sender->store.ops == NULL || !sender->pending ||
        ack_through < sender->pending_seq) {
        return CP_ESTATE;
    }
    status = sender->store.ops->ack_through(sender->store.ctx, ack_through);
    if (status == CP_OK) {
        sender->pending = false;
        sender->retries = 0u;
        sender->next_seq = ack_through + 1u;
    }
    return status;
}

cp_status_t cp_cobs_encode(const uint8_t *input,
                           size_t input_bytes,
                           uint8_t *output,
                           size_t capacity,
                           size_t *written) {
    size_t read_index = 0u;
    size_t write_index = 1u;
    size_t code_index = 0u;
    uint8_t code = 1u;
    if ((input == NULL && input_bytes != 0u) || output == NULL || written == NULL || capacity == 0u) {
        return CP_EINVAL;
    }
    while (read_index < input_bytes) {
        if (input[read_index] == 0u) {
            if (code_index >= capacity) {
                return CP_EBOUNDS;
            }
            output[code_index] = code;
            code = 1u;
            code_index = write_index++;
            ++read_index;
        } else {
            if (write_index >= capacity) {
                return CP_EBOUNDS;
            }
            output[write_index++] = input[read_index++];
            ++code;
            if (code == 0xffu) {
                output[code_index] = code;
                code = 1u;
                code_index = write_index++;
            }
        }
    }
    if (code_index >= capacity) {
        return CP_EBOUNDS;
    }
    output[code_index] = code;
    *written = write_index;
    return CP_OK;
}

cp_status_t cp_cobs_decode(const uint8_t *input,
                           size_t input_bytes,
                           uint8_t *output,
                           size_t capacity,
                           size_t *written) {
    size_t read_index = 0u;
    size_t write_index = 0u;
    uint8_t code;
    uint8_t i;
    if (input == NULL || input_bytes == 0u || output == NULL || written == NULL) {
        return CP_EINVAL;
    }
    while (read_index < input_bytes) {
        code = input[read_index++];
        if (code == 0u || (size_t)(code - 1u) > input_bytes - read_index) {
            return CP_EFORMAT;
        }
        for (i = 1u; i < code; ++i) {
            if (write_index >= capacity) {
                return CP_EBOUNDS;
            }
            output[write_index++] = input[read_index++];
        }
        if (code != 0xffu && read_index < input_bytes) {
            if (write_index >= capacity) {
                return CP_EBOUNDS;
            }
            output[write_index++] = 0u;
        }
    }
    *written = write_index;
    return CP_OK;
}
