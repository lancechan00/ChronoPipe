#include "chronopipe/store.h"

#include <string.h>

static cp_status_t ram_append(void *ctx, uint32_t block_seq, const uint8_t *data, size_t bytes) {
    cp_ram_store_t *store = (cp_ram_store_t *)ctx;
    uint16_t slot;
    if (store == NULL || data == NULL || bytes == 0u || bytes > store->slot_bytes) {
        return CP_EINVAL;
    }
    if (cp_evidence_validate_record(data, bytes) != CP_OK) {
        return CP_EFORMAT;
    }
    if (store->stopped) {
        return CP_ESTATE;
    }
    if (store->count == store->slot_count) {
        if (store->policy == CP_OVERFLOW_DROP_NEWEST) {
            ++store->dropped;
            return CP_EFULL;
        }
        if (store->policy == CP_OVERFLOW_STOP) {
            store->stopped = true;
            return CP_ESTATE;
        }
        store->head = (uint16_t)((store->head + 1u) % store->slot_count);
        --store->count;
        ++store->dropped;
    }
    slot = (uint16_t)((store->head + store->count) % store->slot_count);
    memcpy(store->memory + (size_t)slot * store->slot_bytes, data, bytes);
    store->seq[slot] = block_seq;
    store->length[slot] = (uint32_t)bytes;
    ++store->count;
    return CP_OK;
}

static cp_status_t ram_read(void *ctx, uint32_t block_seq, cp_block_view_t *out) {
    cp_ram_store_t *store = (cp_ram_store_t *)ctx;
    uint16_t i;
    uint16_t slot;
    if (store == NULL || out == NULL) {
        return CP_EINVAL;
    }
    for (i = 0u; i < store->count; ++i) {
        slot = (uint16_t)((store->head + i) % store->slot_count);
        if (store->seq[slot] == block_seq) {
            out->block_seq = block_seq;
            out->data = store->memory + (size_t)slot * store->slot_bytes;
            out->bytes = store->length[slot];
            return CP_OK;
        }
    }
    return CP_ENOTFOUND;
}

static cp_status_t ram_oldest(void *ctx, uint32_t *block_seq) {
    cp_ram_store_t *store = (cp_ram_store_t *)ctx;
    if (store == NULL || block_seq == NULL) {
        return CP_EINVAL;
    }
    if (store->count == 0u) {
        return CP_ENOTFOUND;
    }
    *block_seq = store->seq[store->head];
    return CP_OK;
}

static cp_status_t ram_newest(void *ctx, uint32_t *block_seq) {
    cp_ram_store_t *store = (cp_ram_store_t *)ctx;
    uint16_t slot;
    if (store == NULL || block_seq == NULL) {
        return CP_EINVAL;
    }
    if (store->count == 0u) {
        return CP_ENOTFOUND;
    }
    slot = (uint16_t)((store->head + store->count - 1u) % store->slot_count);
    *block_seq = store->seq[slot];
    return CP_OK;
}

static cp_status_t ram_ack_through(void *ctx, uint32_t block_seq) {
    cp_ram_store_t *store = (cp_ram_store_t *)ctx;
    if (store == NULL) {
        return CP_EINVAL;
    }
    while (store->count != 0u && store->seq[store->head] <= block_seq) {
        store->head = (uint16_t)((store->head + 1u) % store->slot_count);
        --store->count;
    }
    return CP_OK;
}

const cp_store_ops_t cp_ram_store_ops = {
    ram_append,
    ram_read,
    ram_oldest,
    ram_newest,
    ram_ack_through
};

cp_status_t cp_ram_store_init(cp_ram_store_t *store,
                              void *memory,
                              size_t memory_bytes,
                              size_t slot_bytes,
                              uint16_t slot_count,
                              cp_overflow_policy_t policy) {
    if (store == NULL || memory == NULL || slot_bytes < CP_CPB1_RECORD_PREFIX_BYTES ||
        slot_count == 0u || slot_count > CP_STORE_MAX_SLOTS ||
        slot_bytes > memory_bytes / slot_count) {
        return CP_EINVAL;
    }
    memset(store, 0, sizeof(*store));
    store->memory = (uint8_t *)memory;
    store->slot_bytes = slot_bytes;
    store->slot_count = slot_count;
    store->policy = policy;
    return CP_OK;
}
