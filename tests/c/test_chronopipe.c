#include "chronopipe/chronopipe.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
        return 1; \
    } \
} while (0)

static size_t make_data_record(uint8_t *record, size_t capacity, uint32_t block_seq) {
    cp_cpb1_data_header_t header;
    float payload[2] = {1.25f, 2.5f};
    size_t written = 0u;
    memset(&header, 0, sizeof(header));
    header.session_id = 7u;
    header.stream_id = 1u;
    header.schema_id = 1u;
    header.block_seq = block_seq;
    header.first_sample_seq = block_seq * 2u;
    header.t0_us = 1000u;
    header.period_num_us = 10000u;
    header.period_den = 1u;
    header.sample_count = 2u;
    header.channel_count = 1u;
    header.dtype = CP_DTYPE_F32;
    header.payload_bytes = sizeof(payload);
    CHECK(cp_cpb1_data_encode(&header, payload, record, capacity, &written) == CP_OK);
    return written;
}

static int test_cpb1(void) {
    uint8_t header[CP_CPB1_FILE_HEADER_BYTES];
    uint8_t record[128];
    cp_cpb1_data_view_t view;
    uint16_t minor = 99u;
    size_t bytes;
    size_t metadata_bytes;
    CHECK(cp_crc32c("123456789", 9u) == 0xe3069283u);
    CHECK(cp_cpb1_file_header_encode(0u, header, sizeof(header)) == CP_OK);
    CHECK(cp_cpb1_file_header_decode(header, sizeof(header), &minor) == CP_OK);
    CHECK(minor == 0u);
    CHECK(cp_cpb1_record_encode(CP_CPB1_SESSION, 0u, "{}", 2u,
                                record, sizeof(record), &metadata_bytes) == CP_OK);
    CHECK(cp_cpb1_validate_record(record, metadata_bytes) == CP_OK);
    record[metadata_bytes - 1u] ^= 1u;
    CHECK(cp_cpb1_validate_record(record, metadata_bytes) == CP_ECRC);
    bytes = make_data_record(record, sizeof(record), 3u);
    CHECK(cp_cpb1_data_decode(record, bytes, &view) == CP_OK);
    CHECK(view.header.block_seq == 3u);
    CHECK(view.header.sample_count == 2u);
    record[bytes - 1u] ^= 1u;
    CHECK(cp_cpb1_data_decode(record, bytes, &view) == CP_ECRC);
    return 0;
}

static int test_ram_store(void) {
    uint8_t memory[2u * 128u];
    uint8_t record[128];
    cp_ram_store_t ram;
    cp_store_t store;
    cp_block_view_t view;
    uint32_t oldest;
    size_t bytes = make_data_record(record, sizeof(record), 1u);
    CHECK(cp_ram_store_init(&ram, memory, sizeof(memory), 128u, 2u,
                            CP_OVERFLOW_DROP_OLDEST) == CP_OK);
    store.ops = &cp_ram_store_ops;
    store.ctx = &ram;
    CHECK(store.ops->append(store.ctx, 10u, record, bytes) == CP_OK);
    CHECK(store.ops->append(store.ctx, 11u, record, bytes) == CP_OK);
    CHECK(store.ops->append(store.ctx, 12u, record, bytes) == CP_OK);
    CHECK(store.ops->oldest(store.ctx, &oldest) == CP_OK && oldest == 11u);
    CHECK(store.ops->read(store.ctx, 12u, &view) == CP_OK && view.bytes == bytes);
    CHECK(store.ops->ack_through(store.ctx, 11u) == CP_OK);
    CHECK(store.ops->oldest(store.ctx, &oldest) == CP_OK && oldest == 12u);
    return 0;
}

static int test_recorder(void) {
    uint8_t memory[2u * 128u];
    uint8_t record_buffer[128];
    cp_ram_store_t ram;
    cp_store_t store;
    cp_stream_desc_t stream;
    cp_recorder_config_t config;
    cp_recorder_t recorder;
    cp_block_view_t block;
    cp_cpb1_data_view_t decoded;
    uint32_t seq;
    memset(&stream, 0, sizeof(stream));
    stream.id = 1u; stream.schema_id = 1u; stream.name = "raw";
    stream.dtype = CP_DTYPE_F32; stream.channel_count = 1u;
    stream.period_num_us = 10000u; stream.period_den = 1u;
    CHECK(cp_ram_store_init(&ram, memory, sizeof(memory), 128u, 2u,
                            CP_OVERFLOW_DROP_OLDEST) == CP_OK);
    store.ops = &cp_ram_store_ops; store.ctx = &ram;
    config.session_id = 9u; config.stream = &stream;
    config.samples_per_block = 4u; config.first_block_seq = 100u;
    CHECK(cp_recorder_init(&recorder, &config, store,
                           record_buffer, sizeof(record_buffer)) == CP_OK);
    for (seq = 0u; seq < 6u; ++seq) {
        float value = (float)seq;
        cp_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        frame.stream_id = 1u; frame.seq = seq; frame.event_time_us = seq * 10000u;
        frame.period_num_us = 10000u; frame.period_den = 1u;
        frame.dtype = CP_DTYPE_F32; frame.channel_count = 1u; frame.sample_count = 1u;
        frame.data = &value; frame.data_bytes = sizeof(value);
        CHECK(cp_recorder_append(&recorder, &frame) == CP_OK);
    }
    CHECK(cp_recorder_flush(&recorder) == CP_OK);
    CHECK(ram.count == 2u);
    CHECK(store.ops->read(store.ctx, 100u, &block) == CP_OK);
    CHECK(cp_cpb1_data_decode(block.data, block.bytes, &decoded) == CP_OK);
    CHECK(decoded.header.sample_count == 4u && decoded.header.first_sample_seq == 0u);
    CHECK(store.ops->read(store.ctx, 101u, &block) == CP_OK);
    CHECK(cp_cpb1_data_decode(block.data, block.bytes, &decoded) == CP_OK);
    CHECK(decoded.header.sample_count == 2u && decoded.header.first_sample_seq == 4u);
    return 0;
}

typedef struct {
    uint8_t bytes[4u * 160u];
    int writes_until_failure;
} fake_flash_t;

static cp_status_t fake_read(void *ctx, uint32_t offset, void *dst, size_t bytes) {
    fake_flash_t *flash = (fake_flash_t *)ctx;
    if ((size_t)offset + bytes > sizeof(flash->bytes)) return CP_EIO;
    memcpy(dst, flash->bytes + offset, bytes);
    return CP_OK;
}

static cp_status_t fake_write(void *ctx, uint32_t offset, const void *src, size_t bytes) {
    fake_flash_t *flash = (fake_flash_t *)ctx;
    const uint8_t *input = (const uint8_t *)src;
    size_t i;
    if (flash->writes_until_failure == 0) return CP_EIO;
    if (flash->writes_until_failure > 0) --flash->writes_until_failure;
    if ((size_t)offset + bytes > sizeof(flash->bytes)) return CP_EIO;
    for (i = 0u; i < bytes; ++i) {
        if ((flash->bytes[offset + i] & input[i]) != input[i]) return CP_EIO;
        flash->bytes[offset + i] &= input[i];
    }
    return CP_OK;
}

static cp_status_t fake_erase(void *ctx, uint32_t offset, size_t bytes) {
    fake_flash_t *flash = (fake_flash_t *)ctx;
    if ((size_t)offset + bytes > sizeof(flash->bytes)) return CP_EIO;
    memset(flash->bytes + offset, 0xff, bytes);
    return CP_OK;
}

static int test_flash_recovery(void) {
    fake_flash_t flash;
    cp_flash_io_t io;
    cp_flash_store_t first;
    cp_flash_store_t recovered;
    uint8_t read_buffer[136];
    uint8_t second_read_buffer[136];
    uint8_t record[128];
    uint32_t oldest;
    size_t bytes = make_data_record(record, sizeof(record), 1u);
    memset(&flash, 0xff, sizeof(flash));
    flash.writes_until_failure = -1;
    io.read = fake_read; io.write = fake_write; io.erase = fake_erase; io.ctx = &flash;
    CHECK(cp_flash_store_init(&first, &io, 0u, 160u, 4u,
                              read_buffer, sizeof(read_buffer),
                              CP_OVERFLOW_DROP_OLDEST) == CP_OK);
    CHECK(cp_flash_store_ops.append(&first, 20u, record, bytes) == CP_OK);
    flash.writes_until_failure = 2;
    CHECK(cp_flash_store_ops.append(&first, 21u, record, bytes) == CP_EIO);
    flash.writes_until_failure = -1;
    CHECK(cp_flash_store_init(&recovered, &io, 0u, 160u, 4u,
                              second_read_buffer, sizeof(second_read_buffer),
                              CP_OVERFLOW_DROP_OLDEST) == CP_OK);
    CHECK(recovered.valid_count == 1u);
    CHECK(cp_flash_store_ops.oldest(&recovered, &oldest) == CP_OK && oldest == 20u);
    CHECK(cp_flash_store_ops.ack_through(&recovered, 20u) == CP_OK);
    CHECK(cp_flash_store_init(&first, &io, 0u, 160u, 4u,
                              read_buffer, sizeof(read_buffer),
                              CP_OVERFLOW_DROP_OLDEST) == CP_OK);
    CHECK(first.valid_count == 0u);
    return 0;
}

static int test_cobs(void) {
    const uint8_t input[] = {0u, 1u, 2u, 0u, 3u, 0u};
    uint8_t encoded[16];
    uint8_t decoded[16];
    size_t encoded_bytes;
    size_t decoded_bytes;
    CHECK(cp_cobs_encode(input, sizeof(input), encoded, sizeof(encoded), &encoded_bytes) == CP_OK);
    CHECK(cp_cobs_decode(encoded, encoded_bytes, decoded, sizeof(decoded), &decoded_bytes) == CP_OK);
    CHECK(decoded_bytes == sizeof(input));
    CHECK(memcmp(decoded, input, sizeof(input)) == 0);
    return 0;
}

typedef struct {
    uint32_t last_seq;
    unsigned sends;
} send_state_t;

static cp_status_t test_send(void *ctx, uint32_t block_seq, const uint8_t *data, size_t bytes) {
    send_state_t *state = (send_state_t *)ctx;
    if (cp_cpb1_validate_record(data, bytes) != CP_OK) return CP_EFORMAT;
    state->last_seq = block_seq;
    ++state->sends;
    return CP_OK;
}

static int test_batch_sender(void) {
    uint8_t memory[128];
    uint8_t record[128];
    cp_ram_store_t ram;
    cp_store_t store;
    cp_batch_transport_t transport;
    cp_batch_sender_t sender;
    send_state_t sent;
    size_t bytes = make_data_record(record, sizeof(record), 1u);
    memset(&sent, 0, sizeof(sent));
    CHECK(cp_ram_store_init(&ram, memory, sizeof(memory), 128u, 1u,
                            CP_OVERFLOW_STOP) == CP_OK);
    store.ops = &cp_ram_store_ops; store.ctx = &ram;
    CHECK(store.ops->append(store.ctx, 42u, record, bytes) == CP_OK);
    transport.send = test_send; transport.ctx = &sent;
    cp_batch_sender_init(&sender, store, transport, 2u);
    CHECK(cp_batch_sender_poll(&sender) == CP_OK);
    CHECK(sent.sends == 1u && sent.last_seq == 42u && sender.pending);
    CHECK(cp_batch_sender_ack(&sender, 42u) == CP_OK);
    CHECK(ram.count == 0u && !sender.pending);
    return 0;
}

typedef struct {
    float peak;
    unsigned peak_count;
    unsigned gaps;
    uint32_t last_peak_quality;
} sink_state_t;

static void frame_sink(void *user, const cp_frame_t *frame) {
    sink_state_t *sink = (sink_state_t *)user;
    if (frame->stream_id == 4u) {
        sink->peak = ((const float *)frame->data)[0];
        sink->last_peak_quality = frame->quality_flags;
        ++sink->peak_count;
    }
}

static void event_sink(void *user, const cp_event_t *event) {
    sink_state_t *sink = (sink_state_t *)user;
    if (event->type == CP_EVENT_SEQUENCE_GAP) ++sink->gaps;
}

static int test_pipeline(void) {
    uint8_t arena_memory[2048];
    cp_arena_t arena;
    cp_stage_t stages[3];
    cp_window_config_t window = {1u, 8u, 4u};
    cp_spectrum_config_t spectrum = {1u, 8u, CP_SPECTRUM_RECTANGULAR};
    cp_peak_frequency_config_t peak = {1u, 8u, 8.0f, 0.5f, 2.0f};
    cp_stream_desc_t streams[4];
    cp_runtime_t runtime;
    sink_state_t sink;
    uint32_t seq;
    memset(streams, 0, sizeof(streams));
    memset(&sink, 0, sizeof(sink));
    streams[0].id = 1u; streams[0].schema_id = 1u; streams[0].name = "raw";
    streams[0].dtype = CP_DTYPE_F32; streams[0].channel_count = 1u;
    streams[0].period_num_us = 125000u; streams[0].period_den = 1u;
    streams[1] = streams[0]; streams[1].id = 2u; streams[1].schema_id = 2u; streams[1].name = "window";
    streams[2] = streams[0]; streams[2].id = 3u; streams[2].schema_id = 3u; streams[2].name = "spectrum";
    streams[3] = streams[0]; streams[3].id = 4u; streams[3].schema_id = 4u; streams[3].name = "peak_hz";
    cp_arena_init(&arena, arena_memory, sizeof(arena_memory));
    CHECK(cp_stage_window_init(&stages[0], 1u, 1u, 2u, &window, &arena) == CP_OK);
    CHECK(cp_stage_spectrum_init(&stages[1], 2u, 2u, 3u, &spectrum, &arena) == CP_OK);
    CHECK(cp_stage_peak_frequency_init(&stages[2], 3u, 3u, 4u, &peak, &arena) == CP_OK);
    CHECK(cp_runtime_init(&runtime, streams, 4u, stages, 3u, frame_sink, event_sink, &sink) == CP_OK);
    for (seq = 0u; seq < 8u; ++seq) {
        float value = sinf((float)(2.0 * 3.14159265358979323846 * seq / 8.0));
        cp_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        frame.stream_id = 1u; frame.seq = seq; frame.event_time_us = seq * 125000u;
        frame.emit_time_us = frame.event_time_us; frame.period_num_us = 125000u; frame.period_den = 1u;
        frame.dtype = CP_DTYPE_F32; frame.channel_count = 1u; frame.sample_count = 1u;
        frame.data = &value; frame.data_bytes = sizeof(value);
        CHECK(cp_runtime_push(&runtime, &frame) == CP_OK);
    }
    CHECK(sink.peak_count == 1u);
    CHECK(fabsf(sink.peak - 1.0f) < 0.001f);
    for (seq = 9u; seq <= 20u; ++seq) {
        float value = 0.0f;
        cp_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        frame.stream_id = 1u; frame.seq = seq; frame.event_time_us = seq * 125000u;
        frame.emit_time_us = frame.event_time_us; frame.period_num_us = 125000u; frame.period_den = 1u;
        frame.quality_flags = seq == 15u ? CP_Q_LATE : 0u;
        frame.dtype = CP_DTYPE_F32; frame.channel_count = 1u; frame.sample_count = 1u;
        frame.data = &value; frame.data_bytes = sizeof(value);
        CHECK(cp_runtime_push(&runtime, &frame) == CP_OK);
    }
    CHECK(sink.gaps == 1u);
    CHECK(sink.peak_count == 3u);
    CHECK((sink.last_peak_quality & CP_Q_LATE) != 0u);
    return 0;
}

int main(void) {
    CHECK(test_cpb1() == 0);
    CHECK(test_ram_store() == 0);
    CHECK(test_recorder() == 0);
    CHECK(test_flash_recovery() == 0);
    CHECK(test_cobs() == 0);
    CHECK(test_batch_sender() == 0);
    CHECK(test_pipeline() == 0);
    puts("chronopipe_tests: all checks passed");
    return 0;
}
