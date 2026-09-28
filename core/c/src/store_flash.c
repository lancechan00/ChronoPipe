#include "chronopipe/store.h"

#include <string.h>

#define SLOT_HEADER_BYTES 16u
#define SLOT_FOOTER_BYTES 8u
#define SLOT_COMMIT 0x43504d54u

static void put_u32(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xffu);
    dst[1] = (uint8_t)((value >> 8) & 0xffu);
    dst[2] = (uint8_t)((value >> 16) & 0xffu);
    dst[3] = (uint8_t)((value >> 24) & 0xffu);
}

static uint32_t get_u32(const uint8_t *src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static uint32_t slot_offset(const cp_flash_store_t *store, uint16_t slot) {
    return store->base_offset + (uint32_t)slot * store->slot_bytes;
}

static cp_status_t flash_read_record(cp_flash_store_t *store,
                                     uint16_t slot,
                                     cp_block_view_t *out) {
    cp_status_t status;
    size_t length = store->length[slot];
    if (length > store->read_buffer_bytes) {
        return CP_EBOUNDS;
    }
    status = store->io.read(store->io.ctx,
                            slot_offset(store, slot) + SLOT_HEADER_BYTES,
                            store->read_buffer,
                            length);
    if (status != CP_OK) {
        return status;
    }
    status = cp_evidence_validate_record(store->read_buffer, length);
    if (status != CP_OK) {
        return status;
    }
    out->block_seq = store->seq[slot];
    out->data = store->read_buffer;
    out->bytes = length;
    return CP_OK;
}

static cp_status_t flash_append(void *ctx,
                                uint32_t block_seq,
                                const uint8_t *data,
                                size_t bytes) {
    cp_flash_store_t *store = (cp_flash_store_t *)ctx;
    uint16_t slot = store != NULL ? store->next_slot : 0u;
    uint16_t i;
    uint8_t marker[4];
    cp_status_t status;
    if (store == NULL || data == NULL || bytes == 0u ||
        bytes > store->slot_bytes - SLOT_HEADER_BYTES - SLOT_FOOTER_BYTES) {
        return CP_EINVAL;
    }
    if (store->stopped) {
        return CP_ESTATE;
    }
    status = cp_evidence_validate_record(data, bytes);
    if (status != CP_OK) {
        return status;
    }
    for (i = 0u; i < store->slot_count; ++i) {
        if (store->valid[i] != 0u && store->seq[i] == block_seq) {
            return CP_ESTATE;
        }
    }
    if (store->valid_count == store->slot_count) {
        if (store->policy == CP_OVERFLOW_DROP_NEWEST) {
            return CP_EFULL;
        }
        if (store->policy == CP_OVERFLOW_STOP) {
            store->stopped = true;
            return CP_ESTATE;
        }
        slot = 0u;
        for (i = 1u; i < store->slot_count; ++i) {
            if (store->seq[i] < store->seq[slot]) {
                slot = i;
            }
        }
        store->valid[slot] = 0u;
        --store->valid_count;
    } else {
        for (i = 0u; i < store->slot_count; ++i) {
            slot = (uint16_t)((store->next_slot + i) % store->slot_count);
            if (store->valid[slot] == 0u) {
                break;
            }
        }
    }
    status = store->io.erase(store->io.ctx, slot_offset(store, slot), store->slot_bytes);
    if (status != CP_OK) {
        return status;
    }
    memset(store->scratch, 0, SLOT_HEADER_BYTES);
    memcpy(store->scratch, "CPS1", 4u);
    put_u32(store->scratch + 4u, block_seq);
    put_u32(store->scratch + 8u, (uint32_t)bytes);
    put_u32(store->scratch + 12u, cp_crc32c(store->scratch, 12u));
    status = store->io.write(store->io.ctx,
                             slot_offset(store, slot),
                             store->scratch,
                             SLOT_HEADER_BYTES);
    if (status == CP_OK) {
        status = store->io.write(store->io.ctx,
                                 slot_offset(store, slot) + SLOT_HEADER_BYTES,
                                 data,
                                 bytes);
    }
    if (status == CP_OK) {
        put_u32(marker, SLOT_COMMIT);
        status = store->io.write(store->io.ctx,
                                 slot_offset(store, slot) + store->slot_bytes - 4u,
                                 marker,
                                 sizeof(marker));
    }
    if (status != CP_OK) {
        return status;
    }
    store->seq[slot] = block_seq;
    store->length[slot] = (uint32_t)bytes;
    store->valid[slot] = 1u;
    ++store->valid_count;
    store->next_slot = (uint16_t)((slot + 1u) % store->slot_count);
    return CP_OK;
}

static cp_status_t flash_read(void *ctx, uint32_t block_seq, cp_block_view_t *out) {
    cp_flash_store_t *store = (cp_flash_store_t *)ctx;
    uint16_t i;
    if (store == NULL || out == NULL) {
        return CP_EINVAL;
    }
    for (i = 0u; i < store->slot_count; ++i) {
        if (store->valid[i] != 0u && store->seq[i] == block_seq) {
            return flash_read_record(store, i, out);
        }
    }
    return CP_ENOTFOUND;
}

static cp_status_t flash_edge(cp_flash_store_t *store, uint32_t *block_seq, bool newest) {
    uint16_t i;
    bool found = false;
    if (store == NULL || block_seq == NULL) {
        return CP_EINVAL;
    }
    for (i = 0u; i < store->slot_count; ++i) {
        if (store->valid[i] != 0u && (!found ||
            (newest ? store->seq[i] > *block_seq : store->seq[i] < *block_seq))) {
            *block_seq = store->seq[i];
            found = true;
        }
    }
    return found ? CP_OK : CP_ENOTFOUND;
}

static cp_status_t flash_oldest(void *ctx, uint32_t *block_seq) {
    return flash_edge((cp_flash_store_t *)ctx, block_seq, false);
}

static cp_status_t flash_newest(void *ctx, uint32_t *block_seq) {
    return flash_edge((cp_flash_store_t *)ctx, block_seq, true);
}

static cp_status_t flash_ack_through(void *ctx, uint32_t block_seq) {
    cp_flash_store_t *store = (cp_flash_store_t *)ctx;
    uint8_t ack[4] = {0u, 0u, 0u, 0u};
    uint16_t i;
    cp_status_t status;
    if (store == NULL) {
        return CP_EINVAL;
    }
    for (i = 0u; i < store->slot_count; ++i) {
        if (store->valid[i] != 0u && store->seq[i] <= block_seq) {
            status = store->io.write(store->io.ctx,
                                     slot_offset(store, i) + store->slot_bytes - 8u,
                                     ack,
                                     sizeof(ack));
            if (status != CP_OK) {
                return status;
            }
            store->valid[i] = 0u;
            --store->valid_count;
        }
    }
    return CP_OK;
}

const cp_store_ops_t cp_flash_store_ops = {
    flash_append,
    flash_read,
    flash_oldest,
    flash_newest,
    flash_ack_through
};

cp_status_t cp_flash_store_init(cp_flash_store_t *store,
                                const cp_flash_io_t *io,
                                uint32_t base_offset,
                                uint32_t slot_bytes,
                                uint16_t slot_count,
                                void *read_buffer,
                                size_t read_buffer_bytes,
                                cp_overflow_policy_t policy) {
    uint16_t i;
    uint32_t newest_seq = 0u;
    bool have_newest = false;
    cp_status_t status;
    cp_block_view_t view;
    if (store == NULL || io == NULL || io->read == NULL || io->write == NULL || io->erase == NULL ||
        read_buffer == NULL || slot_count == 0u || slot_count > CP_STORE_MAX_SLOTS ||
        slot_bytes <= SLOT_HEADER_BYTES + SLOT_FOOTER_BYTES ||
        read_buffer_bytes < slot_bytes - SLOT_HEADER_BYTES - SLOT_FOOTER_BYTES) {
        return CP_EINVAL;
    }
    memset(store, 0, sizeof(*store));
    store->io = *io;
    store->base_offset = base_offset;
    store->slot_bytes = slot_bytes;
    store->slot_count = slot_count;
    store->read_buffer = (uint8_t *)read_buffer;
    store->read_buffer_bytes = read_buffer_bytes;
    store->policy = policy;
    for (i = 0u; i < slot_count; ++i) {
        status = io->read(io->ctx, slot_offset(store, i), store->scratch, SLOT_HEADER_BYTES);
        if (status != CP_OK) {
            return status;
        }
        status = io->read(io->ctx,
                          slot_offset(store, i) + slot_bytes - SLOT_FOOTER_BYTES,
                          store->scratch + SLOT_HEADER_BYTES,
                          SLOT_FOOTER_BYTES);
        if (status != CP_OK) {
            return status;
        }
        if (memcmp(store->scratch, "CPS1", 4u) != 0 ||
            get_u32(store->scratch + 12u) != cp_crc32c(store->scratch, 12u) ||
            get_u32(store->scratch + SLOT_HEADER_BYTES + 4u) != SLOT_COMMIT ||
            get_u32(store->scratch + SLOT_HEADER_BYTES) == 0u) {
            continue;
        }
        store->seq[i] = get_u32(store->scratch + 4u);
        store->length[i] = get_u32(store->scratch + 8u);
        if (store->length[i] == 0u ||
            store->length[i] > slot_bytes - SLOT_HEADER_BYTES - SLOT_FOOTER_BYTES) {
            continue;
        }
        store->valid[i] = 1u;
        status = flash_read_record(store, i, &view);
        if (status != CP_OK) {
            store->valid[i] = 0u;
            continue;
        }
        ++store->valid_count;
        if (!have_newest || store->seq[i] > newest_seq) {
            newest_seq = store->seq[i];
            store->next_slot = (uint16_t)((i + 1u) % slot_count);
            have_newest = true;
        }
    }
    return CP_OK;
}
