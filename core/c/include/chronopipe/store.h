#ifndef CHRONOPIPE_STORE_H
#define CHRONOPIPE_STORE_H

#include "chronopipe/cpb1.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CP_STORE_MAX_SLOTS
#define CP_STORE_MAX_SLOTS 64u
#endif

typedef enum {
    CP_OVERFLOW_DROP_OLDEST = 0,
    CP_OVERFLOW_DROP_NEWEST = 1,
    CP_OVERFLOW_STOP = 2
} cp_overflow_policy_t;

typedef struct {
    const uint8_t *data;
    size_t bytes;
    uint32_t block_seq;
} cp_block_view_t;

typedef struct cp_store_ops {
    cp_status_t (*append)(void *ctx, uint32_t block_seq, const uint8_t *data, size_t bytes);
    cp_status_t (*read)(void *ctx, uint32_t block_seq, cp_block_view_t *out);
    cp_status_t (*oldest)(void *ctx, uint32_t *block_seq);
    cp_status_t (*newest)(void *ctx, uint32_t *block_seq);
    cp_status_t (*ack_through)(void *ctx, uint32_t block_seq);
} cp_store_ops_t;

typedef struct {
    const cp_store_ops_t *ops;
    void *ctx;
} cp_store_t;

typedef struct {
    uint8_t *memory;
    size_t slot_bytes;
    uint16_t slot_count;
    uint16_t head;
    uint16_t count;
    uint32_t seq[CP_STORE_MAX_SLOTS];
    uint32_t length[CP_STORE_MAX_SLOTS];
    cp_overflow_policy_t policy;
    uint32_t dropped;
    bool stopped;
} cp_ram_store_t;

extern const cp_store_ops_t cp_ram_store_ops;
cp_status_t cp_ram_store_init(cp_ram_store_t *store,
                              void *memory,
                              size_t memory_bytes,
                              size_t slot_bytes,
                              uint16_t slot_count,
                              cp_overflow_policy_t policy);

typedef struct {
    cp_status_t (*read)(void *ctx, uint32_t offset, void *dst, size_t bytes);
    cp_status_t (*write)(void *ctx, uint32_t offset, const void *src, size_t bytes);
    cp_status_t (*erase)(void *ctx, uint32_t offset, size_t bytes);
    void *ctx;
} cp_flash_io_t;

typedef struct {
    cp_flash_io_t io;
    uint32_t base_offset;
    uint32_t slot_bytes;
    uint16_t slot_count;
    uint16_t next_slot;
    uint16_t valid_count;
    uint32_t seq[CP_STORE_MAX_SLOTS];
    uint32_t length[CP_STORE_MAX_SLOTS];
    uint8_t valid[CP_STORE_MAX_SLOTS];
    uint8_t scratch[CP_CPB1_DATA_RECORD_HEADER_BYTES];
    uint8_t *read_buffer;
    size_t read_buffer_bytes;
    cp_overflow_policy_t policy;
    bool stopped;
} cp_flash_store_t;

extern const cp_store_ops_t cp_flash_store_ops;
cp_status_t cp_flash_store_init(cp_flash_store_t *store,
                                const cp_flash_io_t *io,
                                uint32_t base_offset,
                                uint32_t slot_bytes,
                                uint16_t slot_count,
                                void *read_buffer,
                                size_t read_buffer_bytes,
                                cp_overflow_policy_t policy);

#ifdef __cplusplus
}
#endif

#endif
