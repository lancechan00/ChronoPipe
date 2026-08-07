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

uint32_t cp_crc32c(const void *data, size_t bytes) {
    const uint8_t *cursor = (const uint8_t *)data;
    uint32_t crc = 0xffffffffu;
    size_t i;
    unsigned bit;
    if (data == NULL && bytes != 0u) {
        return 0u;
    }
    for (i = 0u; i < bytes; ++i) {
        crc ^= cursor[i];
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc >> 1) ^ (0x82f63b78u & (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return ~crc;
}

cp_status_t cp_cpb1_file_header_encode(uint16_t minor, uint8_t *out, size_t capacity) {
    if (out == NULL || capacity < CP_CPB1_FILE_HEADER_BYTES) {
        return CP_EBOUNDS;
    }
    memset(out, 0, CP_CPB1_FILE_HEADER_BYTES);
    memcpy(out, "CPB1", 4u);
    put_u16(out + 4u, 1u);
    put_u16(out + 6u, minor);
    put_u32(out + 8u, 0u);
    put_u32(out + 12u, cp_crc32c(out, 12u));
    return CP_OK;
}

cp_status_t cp_cpb1_file_header_decode(const uint8_t *data, size_t bytes, uint16_t *minor) {
    if (data == NULL || minor == NULL || bytes < CP_CPB1_FILE_HEADER_BYTES) {
        return CP_EINVAL;
    }
    if (memcmp(data, "CPB1", 4u) != 0 || get_u16(data + 4u) != 1u ||
        get_u32(data + 8u) != 0u) {
        return CP_EFORMAT;
    }
    if (get_u32(data + 12u) != cp_crc32c(data, 12u)) {
        return CP_ECRC;
    }
    *minor = get_u16(data + 6u);
    return CP_OK;
}

cp_status_t cp_cpb1_record_encode(uint16_t kind,
                                  uint16_t flags,
                                  const void *payload,
                                  size_t payload_bytes,
                                  uint8_t *out,
                                  size_t capacity,
                                  size_t *written) {
    size_t total;
    if (out == NULL || written == NULL || (payload == NULL && payload_bytes != 0u) ||
        kind == 0u || kind == CP_CPB1_DATA) {
        return CP_EINVAL;
    }
    if (payload_bytes > SIZE_MAX - CP_CPB1_RECORD_PREFIX_BYTES - 4u) return CP_EBOUNDS;
    total = CP_CPB1_RECORD_PREFIX_BYTES + payload_bytes + 4u;
    if (total > capacity || total > UINT32_MAX) {
        return CP_EBOUNDS;
    }
    put_u16(out, kind);
    put_u16(out + 2u, flags);
    put_u32(out + 4u, (uint32_t)total);
    if (payload_bytes != 0u) {
        memcpy(out + CP_CPB1_RECORD_PREFIX_BYTES, payload, payload_bytes);
    }
    put_u32(out + CP_CPB1_RECORD_PREFIX_BYTES + payload_bytes, cp_crc32c(payload, payload_bytes));
    *written = total;
    return CP_OK;
}

cp_status_t cp_cpb1_record_prefix_decode(const uint8_t *data,
                                         size_t bytes,
                                         cp_cpb1_record_prefix_t *out) {
    if (data == NULL || out == NULL || bytes < CP_CPB1_RECORD_PREFIX_BYTES) {
        return CP_EINVAL;
    }
    out->kind = get_u16(data);
    out->flags = get_u16(data + 2u);
    out->record_bytes = get_u32(data + 4u);
    if (out->kind == 0u ||
        out->record_bytes < CP_CPB1_RECORD_PREFIX_BYTES || out->record_bytes > bytes) {
        return CP_EFORMAT;
    }
    return CP_OK;
}

cp_status_t cp_cpb1_data_encode(const cp_cpb1_data_header_t *header,
                                const void *payload,
                                uint8_t *out,
                                size_t capacity,
                                size_t *written) {
    size_t element_bytes;
    size_t value_count;
    size_t expected;
    size_t total;
    if (header == NULL || out == NULL || written == NULL || header->period_den == 0u ||
        (payload == NULL && header->payload_bytes != 0u)) {
        return CP_EINVAL;
    }
    element_bytes = cp_dtype_size(header->dtype);
    if (element_bytes == 0u || !cp_checked_mul_size(header->sample_count, header->channel_count, &value_count) ||
        !cp_checked_mul_size(value_count, element_bytes, &expected) || expected != header->payload_bytes) {
        return CP_EFORMAT;
    }
    total = CP_CPB1_DATA_RECORD_HEADER_BYTES + expected;
    if (total > capacity || total > UINT32_MAX) {
        return CP_EBOUNDS;
    }
    memset(out, 0, CP_CPB1_DATA_RECORD_HEADER_BYTES);
    put_u16(out, CP_CPB1_DATA);
    put_u16(out + 2u, 0u);
    put_u32(out + 4u, (uint32_t)total);
    put_u64(out + 8u, header->session_id);
    put_u32(out + 16u, header->stream_id);
    put_u32(out + 20u, header->schema_id);
    put_u32(out + 24u, header->block_seq);
    put_u32(out + 28u, header->first_sample_seq);
    put_u64(out + 32u, header->t0_us);
    put_u32(out + 40u, header->period_num_us);
    put_u32(out + 44u, header->period_den);
    put_u16(out + 48u, header->sample_count);
    put_u16(out + 50u, header->channel_count);
    out[52] = (uint8_t)header->dtype;
    out[53] = header->layout;
    put_u16(out + 54u, header->quality_flags);
    put_u32(out + 56u, header->payload_bytes);
    put_u32(out + 60u, cp_crc32c(payload, expected));
    put_u32(out + 64u, header->input_stream_id);
    put_u32(out + 68u, header->input_first_seq);
    put_u32(out + 72u, header->input_last_seq);
    put_u32(out + 76u, cp_crc32c(out, CP_CPB1_DATA_RECORD_HEADER_BYTES));
    if (expected != 0u && payload != out + CP_CPB1_DATA_RECORD_HEADER_BYTES) {
        memcpy(out + CP_CPB1_DATA_RECORD_HEADER_BYTES, payload, expected);
    }
    *written = total;
    return CP_OK;
}

cp_status_t cp_cpb1_data_decode(const uint8_t *data,
                                size_t bytes,
                                cp_cpb1_data_view_t *out) {
    uint8_t header_copy[CP_CPB1_DATA_RECORD_HEADER_BYTES];
    size_t value_count;
    size_t expected;
    cp_status_t status;
    if (data == NULL || out == NULL || bytes < CP_CPB1_DATA_RECORD_HEADER_BYTES) {
        return CP_EINVAL;
    }
    status = cp_cpb1_record_prefix_decode(data, bytes, &out->prefix);
    if (status != CP_OK || out->prefix.kind != CP_CPB1_DATA || out->prefix.record_bytes != bytes) {
        return CP_EFORMAT;
    }
    memcpy(header_copy, data, sizeof(header_copy));
    memset(header_copy + 76u, 0, 4u);
    if (get_u32(data + 76u) != cp_crc32c(header_copy, sizeof(header_copy))) {
        return CP_ECRC;
    }
    out->header.session_id = get_u64(data + 8u);
    out->header.stream_id = get_u32(data + 16u);
    out->header.schema_id = get_u32(data + 20u);
    out->header.block_seq = get_u32(data + 24u);
    out->header.first_sample_seq = get_u32(data + 28u);
    out->header.t0_us = get_u64(data + 32u);
    out->header.period_num_us = get_u32(data + 40u);
    out->header.period_den = get_u32(data + 44u);
    out->header.sample_count = get_u16(data + 48u);
    out->header.channel_count = get_u16(data + 50u);
    out->header.dtype = (cp_dtype_t)data[52];
    out->header.layout = data[53];
    out->header.quality_flags = get_u16(data + 54u);
    out->header.payload_bytes = get_u32(data + 56u);
    out->header.payload_crc32c = get_u32(data + 60u);
    out->header.input_stream_id = get_u32(data + 64u);
    out->header.input_first_seq = get_u32(data + 68u);
    out->header.input_last_seq = get_u32(data + 72u);
    if (out->header.period_den == 0u || cp_dtype_size(out->header.dtype) == 0u ||
        !cp_checked_mul_size(out->header.sample_count, out->header.channel_count, &value_count) ||
        !cp_checked_mul_size(value_count, cp_dtype_size(out->header.dtype), &expected) ||
        expected != out->header.payload_bytes || CP_CPB1_DATA_RECORD_HEADER_BYTES + expected != bytes) {
        return CP_EFORMAT;
    }
    out->payload = data + CP_CPB1_DATA_RECORD_HEADER_BYTES;
    return cp_crc32c(out->payload, expected) == out->header.payload_crc32c ? CP_OK : CP_ECRC;
}

cp_status_t cp_cpb1_validate_record(const uint8_t *data, size_t bytes) {
    cp_cpb1_record_prefix_t prefix;
    cp_cpb1_data_view_t view;
    cp_status_t status = cp_cpb1_record_prefix_decode(data, bytes, &prefix);
    if (status != CP_OK || prefix.record_bytes != bytes) {
        return status == CP_OK ? CP_EFORMAT : status;
    }
    if (prefix.kind == CP_CPB1_DATA) return cp_cpb1_data_decode(data, bytes, &view);
    if (bytes < CP_CPB1_RECORD_PREFIX_BYTES + 4u) return CP_EFORMAT;
    return get_u32(data + bytes - 4u) ==
        cp_crc32c(data + CP_CPB1_RECORD_PREFIX_BYTES,
                  bytes - CP_CPB1_RECORD_PREFIX_BYTES - 4u) ? CP_OK : CP_ECRC;
}
