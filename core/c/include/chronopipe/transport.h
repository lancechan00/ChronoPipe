#ifndef CHRONOPIPE_TRANSPORT_H
#define CHRONOPIPE_TRANSPORT_H

#include "chronopipe/store.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    cp_status_t (*send)(void *ctx, uint32_t block_seq, const uint8_t *data, size_t bytes);
    void *ctx;
} cp_batch_transport_t;

typedef struct {
    cp_store_t store;
    cp_batch_transport_t transport;
    uint32_t next_seq;
    uint32_t pending_seq;
    bool pending;
    uint32_t retries;
    uint32_t max_retries;
} cp_batch_sender_t;

void cp_batch_sender_init(cp_batch_sender_t *sender,
                          cp_store_t store,
                          cp_batch_transport_t transport,
                          uint32_t max_retries);
cp_status_t cp_batch_sender_poll(cp_batch_sender_t *sender);
cp_status_t cp_batch_sender_ack(cp_batch_sender_t *sender, uint32_t ack_through);

cp_status_t cp_cobs_encode(const uint8_t *input,
                           size_t input_bytes,
                           uint8_t *output,
                           size_t capacity,
                           size_t *written);
cp_status_t cp_cobs_decode(const uint8_t *input,
                           size_t input_bytes,
                           uint8_t *output,
                           size_t capacity,
                           size_t *written);

#ifdef __cplusplus
}
#endif

#endif
