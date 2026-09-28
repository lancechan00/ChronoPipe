#include "chronopipe/cpb2.h"

#include "chronopipe/cpb1.h"

#include <string.h>

static void put_u16(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xffu);
    dst[1] = (uint8_t)((value >> 8) & 0xffu);
}

static void put_u32(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xffu);
    dst[1] = (uint8_t)((value >> 8) & 0xffu);
    dst[2] = (uint8_t)((value >> 16) & 0xffu);
    dst[3] = (uint8_t)((value >> 24) & 0xffu);
}

static void put_u64(uint8_t *dst, uint64_t value) {
    put_u32(dst, (uint32_t)(value & 0xffffffffu));
    put_u32(dst + 4u, (uint32_t)(value >> 32));
}

static uint16_t get_u16(const uint8_t *src) {
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static uint32_t get_u32(const uint8_t *src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static uint64_t get_u64(const uint8_t *src) {
    return (uint64_t)get_u32(src) | ((uint64_t)get_u32(src + 4u) << 32);
}

static uint32_t crc_update(uint32_t crc, const uint8_t *data, size_t bytes) {
    size_t i;
    unsigned bit;
    for (i = 0u; i < bytes; ++i) {
        crc ^= data[i];
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc >> 1) ^ (0x82f63b78u & (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return crc;
}

static uint32_t crc_parts(const uint8_t *head, size_t head_bytes, const uint8_t *payload, size_t payload_bytes) {
    uint32_t crc = crc_update(0xffffffffu, head, head_bytes);
    if (payload_bytes != 0u && payload != NULL) {
        crc = crc_update(crc, payload, payload_bytes);
    }
    return ~crc;
}

static bool shape_bytes(uint32_t sample_count, uint32_t channel_count, cp_dtype_t dtype, size_t *out) {
    size_t values;
    size_t width = cp_dtype_size(dtype);
    if (out == NULL || width == 0u || sample_count == 0u || channel_count == 0u) {
        return false;
    }
    if (!cp_checked_mul_size((size_t)sample_count, (size_t)channel_count, &values)) {
        return false;
    }
    return cp_checked_mul_size(values, width, out);
}

bool cp_cpb2_shape_fits_compact(uint32_t stream_id,
                                uint32_t channel_count,
                                uint32_t sample_count,
                                uint32_t quality_flags,
                                uint32_t layout,
                                cp_dtype_t dtype,
                                uint32_t period_num_us) {
    return stream_id < CP_CPB2_COMPACT_MAX_STREAMS &&
           channel_count >= 1u && channel_count <= CP_CPB2_COMPACT_MAX_CHANNELS &&
           sample_count >= 1u && sample_count <= CP_CPB2_COMPACT_MAX_SAMPLES &&
           quality_flags <= CP_CPB2_COMPACT_MAX_QUALITY &&
           layout <= 1u &&
           cp_dtype_size(dtype) != 0u &&
           period_num_us != 0u;
}

cp_status_t cp_cpb2_compact_record_bytes(uint32_t sample_count,
                                         uint32_t channel_count,
                                         cp_dtype_t dtype,
                                         size_t *out) {
    size_t payload;
    if (out == NULL) {
        return CP_EINVAL;
    }
    if (sample_count == 0u || channel_count == 0u || cp_dtype_size(dtype) == 0u) {
        return CP_EINVAL;
    }
    if (sample_count > CP_CPB2_COMPACT_MAX_SAMPLES || channel_count > CP_CPB2_COMPACT_MAX_CHANNELS) {
        return CP_EBOUNDS;
    }
    if (!shape_bytes(sample_count, channel_count, dtype, &payload) ||
        payload > SIZE_MAX - CP_CPB2_COMPACT_HEADER_BYTES) {
        return CP_EBOUNDS;
    }
    *out = CP_CPB2_COMPACT_HEADER_BYTES + payload;
    return CP_OK;
}

cp_status_t cp_cpb2_file_header_encode(uint16_t minor, uint8_t *out, size_t capacity) {
    if (out == NULL || capacity < CP_CPB2_FILE_HEADER_BYTES) {
        return CP_EBOUNDS;
    }
    memset(out, 0, CP_CPB2_FILE_HEADER_BYTES);
    memcpy(out, "CPB2", 4u);
    put_u16(out + 4u, 2u);
    put_u16(out + 6u, minor);
    put_u32(out + 8u, 0u);
    put_u32(out + 12u, cp_crc32c(out, 12u));
    return CP_OK;
}

cp_status_t cp_cpb2_file_header_decode(const uint8_t *data, size_t bytes, uint16_t *minor) {
    if (data == NULL || minor == NULL || bytes < CP_CPB2_FILE_HEADER_BYTES) {
        return CP_EINVAL;
    }
    if (memcmp(data, "CPB2", 4u) != 0 || get_u16(data + 4u) != 2u || get_u32(data + 8u) != 0u) {
        return CP_EFORMAT;
    }
    if (get_u32(data + 12u) != cp_crc32c(data, 12u)) {
        return CP_ECRC;
    }
    *minor = get_u16(data + 6u);
    return CP_OK;
}

static bool length_delimited_kind_ok(uint16_t kind) {
    return kind != 0u && kind != CP_CPB2_DATA_WIDE && (kind & 7u) != 3u;
}

cp_status_t cp_cpb2_record_encode(uint16_t kind,
                                  uint16_t flags,
                                  const void *payload,
                                  size_t payload_bytes,
                                  uint8_t *out,
                                  size_t capacity,
                                  size_t *written) {
    size_t total;
    if (out == NULL || written == NULL || (payload == NULL && payload_bytes != 0u) ||
        !length_delimited_kind_ok(kind)) {
        return CP_EINVAL;
    }
    if (payload_bytes > SIZE_MAX - CP_CPB2_RECORD_PREFIX_BYTES - 4u) {
        return CP_EBOUNDS;
    }
    total = CP_CPB2_RECORD_PREFIX_BYTES + payload_bytes + 4u;
    if (total > capacity || total > UINT32_MAX) {
        return CP_EBOUNDS;
    }
    put_u16(out, kind);
    put_u16(out + 2u, flags);
    put_u32(out + 4u, (uint32_t)total);
    if (payload_bytes != 0u) {
        memcpy(out + CP_CPB2_RECORD_PREFIX_BYTES, payload, payload_bytes);
    }
    put_u32(out + CP_CPB2_RECORD_PREFIX_BYTES + payload_bytes, cp_crc32c(payload, payload_bytes));
    *written = total;
    return CP_OK;
}

cp_status_t cp_cpb2_compact_encode(const cp_cpb2_compact_header_t *header,
                                   const void *payload,
                                   uint8_t *out,
                                   size_t capacity,
                                   size_t *written) {
    size_t payload_bytes;
    size_t total;
    uint32_t packed;
    cp_status_t status;
    if (header == NULL || out == NULL || written == NULL) {
        return CP_EINVAL;
    }
    if (header->sample_count == 0u || header->channel_count == 0u || header->layout > 1u ||
        cp_dtype_size((cp_dtype_t)header->dtype) == 0u) {
        return CP_EINVAL;
    }
    if (header->stream_id >= CP_CPB2_COMPACT_MAX_STREAMS ||
        header->channel_count > CP_CPB2_COMPACT_MAX_CHANNELS ||
        header->sample_count > CP_CPB2_COMPACT_MAX_SAMPLES ||
        header->quality_flags > CP_CPB2_COMPACT_MAX_QUALITY ||
        header->dtype > 7u) {
        return CP_EBOUNDS;
    }
    status = cp_cpb2_compact_record_bytes(header->sample_count,
                                          header->channel_count,
                                          (cp_dtype_t)header->dtype,
                                          &total);
    if (status != CP_OK) {
        return status;
    }
    payload_bytes = total - CP_CPB2_COMPACT_HEADER_BYTES;
    if (payload == NULL && payload_bytes != 0u) {
        return CP_EINVAL;
    }
    if (total > capacity) {
        return CP_EBOUNDS;
    }
    packed = CP_CPB2_DATA_COMPACT;
    packed |= (header->stream_id & 31u) << 3;
    packed |= (header->dtype & 7u) << 8;
    packed |= (header->layout & 1u) << 11;
    packed |= ((header->channel_count - 1u) & 15u) << 12;
    packed |= (header->quality_flags & 63u) << 16;
    memset(out, 0, CP_CPB2_COMPACT_HEADER_BYTES);
    out[0] = (uint8_t)(packed & 0xffu);
    out[1] = (uint8_t)((packed >> 8) & 0xffu);
    out[2] = (uint8_t)((packed >> 16) & 0xffu);
    put_u16(out + 3u, (uint16_t)header->sample_count);
    put_u32(out + 5u, header->block_seq);
    put_u32(out + 9u, header->first_sample_seq);
    if (payload_bytes != 0u && payload != out + CP_CPB2_COMPACT_HEADER_BYTES) {
        memcpy(out + CP_CPB2_COMPACT_HEADER_BYTES, payload, payload_bytes);
    }
    put_u32(out + 13u, crc_parts(out, 13u, out + CP_CPB2_COMPACT_HEADER_BYTES, payload_bytes));
    *written = total;
    return CP_OK;
}

cp_status_t cp_cpb2_compact_decode(const uint8_t *data, size_t bytes, cp_cpb2_compact_view_t *out) {
    uint32_t packed;
    size_t expected;
    uint32_t sample_count;
    uint32_t channel_count;
    cp_dtype_t dtype;
    if (data == NULL || out == NULL || bytes < CP_CPB2_COMPACT_HEADER_BYTES) {
        return CP_EINVAL;
    }
    packed = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16);
    if ((packed & 7u) != CP_CPB2_DATA_COMPACT || ((packed >> 22) & 3u) != 0u) {
        return CP_EFORMAT;
    }
    dtype = (cp_dtype_t)((packed >> 8) & 7u);
    channel_count = ((packed >> 12) & 15u) + 1u;
    sample_count = get_u16(data + 3u);
    if (sample_count == 0u || cp_dtype_size(dtype) == 0u) {
        return CP_EFORMAT;
    }
    if (!shape_bytes(sample_count, channel_count, dtype, &expected) ||
        CP_CPB2_COMPACT_HEADER_BYTES + expected != bytes) {
        return CP_EFORMAT;
    }
    if (get_u32(data + 13u) != crc_parts(data, 13u, data + CP_CPB2_COMPACT_HEADER_BYTES, expected)) {
        return CP_ECRC;
    }
    memset(out, 0, sizeof(*out));
    out->header.stream_id = (packed >> 3) & 31u;
    out->header.dtype = (uint32_t)dtype;
    out->header.layout = (packed >> 11) & 1u;
    out->header.channel_count = channel_count;
    out->header.quality_flags = (packed >> 16) & 63u;
    out->header.sample_count = sample_count;
    out->header.block_seq = get_u32(data + 5u);
    out->header.first_sample_seq = get_u32(data + 9u);
    out->payload = data + CP_CPB2_COMPACT_HEADER_BYTES;
    out->payload_bytes = expected;
    return CP_OK;
}

static cp_status_t wide_payload_size(const cp_cpb2_wide_header_t *header, size_t *out) {
    if (header->period_den == 0u || header->sample_count == 0u || header->channel_count == 0u ||
        header->layout > 1u || cp_dtype_size(header->dtype) == 0u) {
        return CP_EINVAL;
    }
    if (header->channel_count > UINT16_MAX) {
        return CP_EBOUNDS;
    }
    if (!shape_bytes(header->sample_count, header->channel_count, header->dtype, out)) {
        return CP_EBOUNDS;
    }
    return CP_OK;
}

cp_status_t cp_cpb2_wide_encode(const cp_cpb2_wide_header_t *header,
                                const void *payload,
                                uint8_t *out,
                                size_t capacity,
                                size_t *written) {
    size_t expected;
    size_t total;
    cp_status_t status;
    if (header == NULL || out == NULL || written == NULL ||
        (payload == NULL && header->payload_bytes != 0u)) {
        return CP_EINVAL;
    }
    status = wide_payload_size(header, &expected);
    if (status != CP_OK) {
        return status;
    }
    if (expected != header->payload_bytes) {
        return CP_EFORMAT;
    }
    if (expected > SIZE_MAX - CP_CPB2_WIDE_RECORD_HEADER_BYTES) {
        return CP_EBOUNDS;
    }
    total = CP_CPB2_WIDE_RECORD_HEADER_BYTES + expected;
    if (total > capacity || total > UINT32_MAX) {
        return CP_EBOUNDS;
    }
    memset(out, 0, CP_CPB2_WIDE_RECORD_HEADER_BYTES);
    put_u16(out, CP_CPB2_DATA_WIDE);
    put_u16(out + 2u, 0u);
    put_u32(out + 4u, (uint32_t)total);
    put_u32(out + 8u, header->stream_id);
    put_u32(out + 12u, header->schema_id);
    put_u32(out + 16u, header->block_seq);
    put_u32(out + 20u, header->first_sample_seq);
    put_u64(out + 24u, header->t0_us);
    put_u32(out + 32u, header->period_num_us);
    put_u32(out + 36u, header->period_den);
    put_u32(out + 40u, header->sample_count);
    put_u16(out + 44u, (uint16_t)header->channel_count);
    out[46] = (uint8_t)header->dtype;
    out[47] = header->layout;
    put_u16(out + 48u, header->quality_flags);
    put_u32(out + 50u, header->payload_bytes);
    put_u32(out + 54u, cp_crc32c(payload, expected));
    put_u32(out + 58u, header->input_stream_id);
    put_u32(out + 62u, header->input_first_seq);
    put_u32(out + 66u, header->input_last_seq);
    put_u32(out + 70u, cp_crc32c(out, CP_CPB2_WIDE_RECORD_HEADER_BYTES));
    if (expected != 0u && payload != out + CP_CPB2_WIDE_RECORD_HEADER_BYTES) {
        memcpy(out + CP_CPB2_WIDE_RECORD_HEADER_BYTES, payload, expected);
    }
    *written = total;
    return CP_OK;
}

cp_status_t cp_cpb2_wide_decode(const uint8_t *data, size_t bytes, cp_cpb2_wide_view_t *out) {
    uint8_t header_copy[CP_CPB2_WIDE_RECORD_HEADER_BYTES];
    size_t expected;
    uint32_t record_bytes;
    if (data == NULL || out == NULL || bytes < CP_CPB2_WIDE_RECORD_HEADER_BYTES) {
        return CP_EINVAL;
    }
    if (get_u16(data) != CP_CPB2_DATA_WIDE) {
        return CP_EFORMAT;
    }
    record_bytes = get_u32(data + 4u);
    if (record_bytes != bytes) {
        return CP_EFORMAT;
    }
    memcpy(header_copy, data, sizeof(header_copy));
    memset(header_copy + 70u, 0, 4u);
    if (get_u32(data + 70u) != cp_crc32c(header_copy, sizeof(header_copy))) {
        return CP_ECRC;
    }
    memset(out, 0, sizeof(*out));
    out->flags = get_u16(data + 2u);
    out->header.stream_id = get_u32(data + 8u);
    out->header.schema_id = get_u32(data + 12u);
    out->header.block_seq = get_u32(data + 16u);
    out->header.first_sample_seq = get_u32(data + 20u);
    out->header.t0_us = get_u64(data + 24u);
    out->header.period_num_us = get_u32(data + 32u);
    out->header.period_den = get_u32(data + 36u);
    out->header.sample_count = get_u32(data + 40u);
    out->header.channel_count = get_u16(data + 44u);
    out->header.dtype = (cp_dtype_t)data[46];
    out->header.layout = data[47];
    out->header.quality_flags = get_u16(data + 48u);
    out->header.payload_bytes = get_u32(data + 50u);
    out->header.payload_crc32c = get_u32(data + 54u);
    out->header.input_stream_id = get_u32(data + 58u);
    out->header.input_first_seq = get_u32(data + 62u);
    out->header.input_last_seq = get_u32(data + 66u);
    if (wide_payload_size(&out->header, &expected) != CP_OK ||
        expected != out->header.payload_bytes ||
        CP_CPB2_WIDE_RECORD_HEADER_BYTES + expected != bytes) {
        return CP_EFORMAT;
    }
    out->payload = data + CP_CPB2_WIDE_RECORD_HEADER_BYTES;
    return cp_crc32c(out->payload, expected) == out->header.payload_crc32c ? CP_OK : CP_ECRC;
}

cp_status_t cp_cpb2_event_encode(const cp_cpb2_event_t *event,
                                 uint8_t *out,
                                 size_t capacity,
                                 size_t *written) {
    uint8_t payload[CP_CPB2_EVENT_PAYLOAD_BYTES];
    if (event == NULL) {
        return CP_EINVAL;
    }
    memset(payload, 0, sizeof(payload));
    put_u64(payload, event->session_id);
    put_u16(payload + 8u, event->type);
    put_u16(payload + 10u, event->flags);
    put_u32(payload + 12u, event->stream_id);
    put_u32(payload + 16u, event->sequence);
    put_u64(payload + 20u, event->event_time_us);
    put_u32(payload + 28u, (uint32_t)event->detail);
    memcpy(payload + 32u, &event->value, sizeof(event->value));
    return cp_cpb2_record_encode(CP_CPB2_EVENT, event->flags, payload, sizeof(payload), out, capacity, written);
}

cp_status_t cp_cpb2_event_decode(const uint8_t *data, size_t bytes, cp_cpb2_event_t *out) {
    const uint8_t *payload;
    uint32_t record_bytes;
    if (data == NULL || out == NULL || bytes < CP_CPB2_RECORD_PREFIX_BYTES + CP_CPB2_EVENT_PAYLOAD_BYTES + 4u) {
        return CP_EINVAL;
    }
    record_bytes = get_u32(data + 4u);
    if (get_u16(data) != CP_CPB2_EVENT || record_bytes != bytes ||
        record_bytes != CP_CPB2_RECORD_PREFIX_BYTES + CP_CPB2_EVENT_PAYLOAD_BYTES + 4u) {
        return CP_EFORMAT;
    }
    payload = data + CP_CPB2_RECORD_PREFIX_BYTES;
    if (get_u32(data + bytes - 4u) != cp_crc32c(payload, CP_CPB2_EVENT_PAYLOAD_BYTES)) {
        return CP_ECRC;
    }
    memset(out, 0, sizeof(*out));
    out->session_id = get_u64(payload);
    out->type = get_u16(payload + 8u);
    out->flags = get_u16(payload + 10u);
    out->stream_id = get_u32(payload + 12u);
    out->sequence = get_u32(payload + 16u);
    out->event_time_us = get_u64(payload + 20u);
    out->detail = (int32_t)get_u32(payload + 28u);
    memcpy(&out->value, payload + 32u, sizeof(out->value));
    return CP_OK;
}

static bool looks_like_compact(const uint8_t *data, size_t bytes) {
    return bytes >= 2u && (data[0] & 7u) == CP_CPB2_DATA_COMPACT &&
           !(data[0] == CP_CPB2_DATA_COMPACT && data[1] == 0u);
}

static cp_status_t validate_length_delimited(const uint8_t *data, size_t bytes) {
    uint16_t kind;
    uint32_t record_bytes;
    if (bytes < CP_CPB2_RECORD_PREFIX_BYTES) {
        return CP_EINVAL;
    }
    kind = get_u16(data);
    record_bytes = get_u32(data + 4u);
    if (kind == 0u || record_bytes != bytes || record_bytes < CP_CPB2_RECORD_PREFIX_BYTES) {
        return CP_EFORMAT;
    }
    if (kind == CP_CPB1_DATA) {
        cp_cpb1_data_view_t view;
        return cp_cpb1_data_decode(data, bytes, &view);
    }
    if (kind == CP_CPB2_DATA_WIDE) {
        cp_cpb2_wide_view_t view;
        return cp_cpb2_wide_decode(data, bytes, &view);
    }
    if ((kind & 7u) == 3u) {
        return CP_EFORMAT;
    }
    if (bytes < CP_CPB2_RECORD_PREFIX_BYTES + 4u) {
        return CP_EFORMAT;
    }
    return get_u32(data + bytes - 4u) ==
        cp_crc32c(data + CP_CPB2_RECORD_PREFIX_BYTES,
                  bytes - CP_CPB2_RECORD_PREFIX_BYTES - 4u) ? CP_OK : CP_ECRC;
}

cp_status_t cp_evidence_validate_record(const uint8_t *data, size_t bytes) {
    cp_cpb2_compact_view_t compact;
    if (data == NULL || bytes == 0u) {
        return CP_EINVAL;
    }
    if (looks_like_compact(data, bytes)) {
        return cp_cpb2_compact_decode(data, bytes, &compact);
    }
    return validate_length_delimited(data, bytes);
}
