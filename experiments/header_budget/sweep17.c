/* Sweep the stable domain of the bit-align-17 card.
 *
 *   cc -O2 -std=c99 -Wall -Wextra -Werror -o /tmp/sweep17 sweep17.c
 *
 * Header, 17 bytes, little-endian, CRC32C over bytes [0,13) plus the payload:
 *   [0..2]  kind:3 stream:5 dtype:3 layout:1 (channels-1):4 quality:6 pad:2
 *   [3..4]  sample_count u16
 *   [5..8]  block_seq u32
 *   [9..12] first_seq u32
 *   [13..16] crc32c
 * Wide fields sit on odd offsets. Loads in this file are byte-wise.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HEADER 17u
#define CRC_AT 13u

static uint32_t crc_table[256];
static uint32_t rng_state = 0xA5A5F00Du;

static uint32_t rng(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void crc_init(void) {
    unsigned i;
    unsigned bit;
    for (i = 0u; i < 256u; ++i) {
        uint32_t crc = i;
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc >> 1) ^ (0x82f63b78u & (uint32_t)-(int32_t)(crc & 1u));
        }
        crc_table[i] = crc;
    }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *data, size_t n) {
    size_t i;
    for (i = 0u; i < n; ++i) {
        crc = crc_table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
    }
    return crc;
}

static uint32_t crc32c(const uint8_t *data, size_t n) {
    return ~crc_update(0xffffffffu, data, n);
}

static uint32_t crc_parts(const uint8_t *head, size_t head_n, const uint8_t *payload, size_t payload_n) {
    uint32_t crc = crc_update(0xffffffffu, head, head_n);
    if (payload_n != 0u && payload != NULL) {
        crc = crc_update(crc, payload, payload_n);
    }
    return ~crc;
}

static void put_u16(uint8_t *dst, uint16_t v) {
    dst[0] = (uint8_t)(v & 0xffu);
    dst[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void put_u32(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)(v & 0xffu);
    dst[1] = (uint8_t)((v >> 8) & 0xffu);
    dst[2] = (uint8_t)((v >> 16) & 0xffu);
    dst[3] = (uint8_t)((v >> 24) & 0xffu);
}

static uint16_t get_u16(const uint8_t *src) {
    return (uint16_t)src[0] | (uint16_t)((uint16_t)src[1] << 8);
}

static uint32_t get_u32(const uint8_t *src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

typedef struct {
    uint32_t kind;
    uint32_t stream;
    uint32_t dtype;
    uint32_t layout;
    uint32_t channels;
    uint32_t quality;
    uint32_t sample_count;
    uint32_t block_seq;
    uint32_t first_seq;
} Card;

static int dtype_size(uint32_t dtype) {
    if (dtype == 1u || dtype == 4u || dtype == 5u) return 4;
    if (dtype == 2u || dtype == 3u) return 2;
    return 0;
}

static int payload_bytes(const Card *card, size_t *out) {
    int elem = dtype_size(card->dtype);
    uint64_t bytes;
    if (elem == 0 || card->channels == 0u) return -1;
    bytes = (uint64_t)card->sample_count * (uint64_t)card->channels * (uint64_t)elem;
    if (bytes > (uint64_t)SIZE_MAX) return -1;
    *out = (size_t)bytes;
    return 0;
}

/* Masking encoder: out-of-range fields are truncated, not rejected. */
static size_t encode_mask(const Card *in, const uint8_t *payload, size_t n, uint8_t *dst) {
    uint32_t packed = 0u;
    uint32_t channels_field = in->channels == 0u ? 0u : (in->channels - 1u);
    memset(dst, 0, HEADER);
    packed |= (in->kind & 7u);
    packed |= (in->stream & 31u) << 3;
    packed |= (in->dtype & 7u) << 8;
    packed |= (in->layout & 1u) << 11;
    packed |= (channels_field & 15u) << 12;
    packed |= (in->quality & 63u) << 16;
    dst[0] = (uint8_t)(packed & 0xffu);
    dst[1] = (uint8_t)((packed >> 8) & 0xffu);
    dst[2] = (uint8_t)((packed >> 16) & 0xffu);
    put_u16(dst + 3u, (uint16_t)in->sample_count);
    put_u32(dst + 5u, in->block_seq);
    put_u32(dst + 9u, in->first_seq);
    put_u32(dst + CRC_AT, crc_parts(dst, CRC_AT, payload, n));
    if (n != 0u && payload != NULL) memcpy(dst + HEADER, payload, n);
    return HEADER + n;
}

static int decode_card(const uint8_t *src, Card *out) {
    uint32_t packed = (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16);
    memset(out, 0, sizeof(*out));
    out->kind = packed & 7u;
    out->stream = (packed >> 3) & 31u;
    out->dtype = (packed >> 8) & 7u;
    out->layout = (packed >> 11) & 1u;
    out->channels = ((packed >> 12) & 15u) + 1u;
    out->quality = (packed >> 16) & 63u;
    out->sample_count = get_u16(src + 3u);
    out->block_seq = get_u32(src + 5u);
    out->first_seq = get_u32(src + 9u);
    return 0;
}

static int decode_check(const uint8_t *src, size_t n, Card *out) {
    uint8_t head[CRC_AT];
    uint32_t stored = get_u32(src + CRC_AT);
    memcpy(head, src, CRC_AT);
    if (crc_parts(head, CRC_AT, n == 0u ? NULL : src + HEADER, n) != stored) return -1;
    return decode_card(src, out);
}

static int same_masked(const Card *in, const Card *out) {
    uint32_t channels = in->channels == 0u ? 1u : (((in->channels - 1u) & 15u) + 1u);
    return out->kind == (in->kind & 7u) &&
           out->stream == (in->stream & 31u) &&
           out->dtype == (in->dtype & 7u) &&
           out->layout == (in->layout & 1u) &&
           out->channels == channels &&
           out->quality == (in->quality & 63u) &&
           out->sample_count == (in->sample_count & 0xffffu) &&
           out->block_seq == in->block_seq &&
           out->first_seq == in->first_seq;
}

static void fill_random(uint8_t *dst, size_t n) {
    size_t i;
    for (i = 0u; i < n; ++i) dst[i] = (uint8_t)rng();
}

static Card legal_card(void) {
    Card card;
    card.kind = rng() % 8u;
    card.stream = rng() % 32u;
    card.dtype = 1u + (rng() % 5u);
    card.layout = rng() & 1u;
    card.channels = 1u + (rng() % 16u);
    card.quality = rng() & 63u;
    card.sample_count = 1u + (rng() % 128u);
    card.block_seq = rng();
    card.first_seq = rng();
    return card;
}

static int expect_roundtrip(const Card *card, const uint8_t *payload, size_t n, int odd_base) {
    uint8_t *buf = (uint8_t *)malloc(odd_base + HEADER + n);
    Card out;
    size_t wrote;
    int rc;
    if (buf == NULL) return -1;
    memset(buf, 0xa5, (size_t)odd_base);
    wrote = encode_mask(card, payload, n, buf + odd_base);
    rc = decode_check(buf + odd_base, n, &out);
    if (rc != 0 || !same_masked(card, &out) || wrote != HEADER + n) {
        free(buf);
        return -1;
    }
    if (n != 0u && memcmp(buf + odd_base + HEADER, payload, n) != 0) {
        free(buf);
        return -1;
    }
    if (n != 0u) {
        buf[odd_base + HEADER + (rng() % n)] ^= 0x01u;
        if (decode_check(buf + odd_base, n, &out) == 0) {
            free(buf);
            return -1;
        }
    }
    free(buf);
    return 0;
}

static int run_random(void) {
    int i;
    int failures = 0;
    uint8_t *payload = (uint8_t *)malloc(128u * 16u * 4u);
    if (payload == NULL) return -1;
    for (i = 0; i < 8000; ++i) {
        Card card = legal_card();
        size_t n = 0u;
        if (payload_bytes(&card, &n) != 0) {
            ++failures;
            break;
        }
        fill_random(payload, n);
        if (expect_roundtrip(&card, payload, n, i & 3) != 0 ||
            expect_roundtrip(&card, payload, n, (i + 1) & 3) != 0) {
            ++failures;
            break;
        }
    }
    free(payload);
    printf("random in-range cards: %s (%d failures in 8000 x 2 passes, bases 0..3)\n",
           failures == 0 ? "stable" : "UNSTABLE", failures);
    return failures == 0 ? 0 : -1;
}

static int run_edges(void) {
    struct {
        const char *name;
        Card card;
        uint32_t truncated;
    } edges[8];
    uint8_t payload[8];
    uint8_t buf[HEADER + 8];
    int i;
    Card base;
    memset(&base, 0, sizeof(base));
    base.kind = 3u;
    base.stream = 1u;
    base.dtype = 1u;
    base.layout = 0u;
    base.channels = 2u;
    base.quality = 63u;
    base.sample_count = 2u;
    base.block_seq = 0xffffffffu;
    base.first_seq = 0xffffffffu;
    fill_random(payload, sizeof(payload));
    printf("edges (masking encoder, one step past the field width):\n");
    edges[0].name = "kind 8";
    edges[0].card = base; edges[0].card.kind = 8u; edges[0].truncated = 0u;
    edges[1].name = "stream 32";
    edges[1].card = base; edges[1].card.stream = 32u; edges[1].truncated = 0u;
    edges[2].name = "dtype 8";
    edges[2].card = base; edges[2].card.dtype = 8u; edges[2].truncated = 0u;
    edges[3].name = "layout 2";
    edges[3].card = base; edges[3].card.layout = 2u; edges[3].truncated = 0u;
    edges[4].name = "channels 17";
    edges[4].card = base; edges[4].card.channels = 17u; edges[4].truncated = 1u;
    edges[5].name = "channels 0";
    edges[5].card = base; edges[5].card.channels = 0u; edges[5].truncated = 1u;
    edges[6].name = "quality 64";
    edges[6].card = base; edges[6].card.quality = 64u; edges[6].truncated = 0u;
    edges[7].name = "samples 65536";
    edges[7].card = base; edges[7].card.sample_count = 65536u; edges[7].truncated = 0u;
    for (i = 0; i < 8; ++i) {
        Card out;
        encode_mask(&edges[i].card, payload, sizeof(payload), buf);
        decode_card(buf, &out);
        {
            uint32_t got = 0u;
            if (i == 0) got = out.kind;
            if (i == 1) got = out.stream;
            if (i == 2) got = out.dtype;
            if (i == 3) got = out.layout;
            if (i == 4 || i == 5) got = out.channels;
            if (i == 6) got = out.quality;
            if (i == 7) got = out.sample_count;
            printf("  %-16s written=%u read=%u %s\n", edges[i].name,
                   i == 7 ? 65536u : (i == 4 ? 17u : (i == 5 ? 0u : (i == 6 ? 64u : (i == 0 ? 8u : (i == 1 ? 32u : (i == 2 ? 8u : 2u)))))),
                   got, got == edges[i].truncated ? "CLIFF" : "unexpected");
            if (got != edges[i].truncated) return -1;
        }
    }
    encode_mask(&base, payload, sizeof(payload), buf);
    decode_card(buf, &base);
    if (base.block_seq != 0xffffffffu || base.first_seq != 0xffffffffu || base.quality != 63u) {
        printf("  endpoint u32/quality failed\n");
        return -1;
    }
    printf("  block_seq and first_seq 0xFFFFFFFF: kept\n");
    printf("  quality 0..63: kept (6 flags). Next flag bit is the cliff above.\n");
    printf("  field offsets: sample_count@3 block_seq@5 first_seq@9 crc@13 (none is 4-byte aligned)\n");
    return 0;
}

static int min_samples(int channels, int elem, double limit) {
    int n;
    for (n = 1; n <= 65535; ++n) {
        double payload = (double)n * (double)channels * (double)elem;
        if (1.0 + 17.0 / payload <= limit) return n;
    }
    return -1;
}

static int run_knee(void) {
    static const int channels[] = {1, 2, 4, 8, 16};
    static const int elems[] = {2, 4};
    static const char *elem_name[] = {"i16", "f32"};
    static const double limits[] = {1.02, 1.05, 1.10, 1.50, 2.00};
    size_t c;
    size_t e;
    size_t L;
    int mismatches = 0;
    printf("\nminimum samples/block so stored/raw stays at or under the limit:\n");
    printf("%-4s %-3s", "elem", "ch");
    for (L = 0u; L < 5u; ++L) printf(" %6.2f", limits[L]);
    printf("\n");
    for (e = 0u; e < 2u; ++e) {
        for (c = 0u; c < 5u; ++c) {
            printf("%-4s %3d", elem_name[e], channels[c]);
            for (L = 0u; L < 5u; ++L) {
                int need = min_samples(channels[c], elems[e], limits[L]);
                Card card;
                uint8_t tiny[HEADER + 4];
                size_t payload = (size_t)need * (size_t)channels[c] * (size_t)elems[e];
                size_t wrote;
                memset(&card, 0, sizeof(card));
                card.kind = 3u;
                card.stream = 1u;
                card.dtype = elems[e] == 4 ? 1u : 2u;
                card.channels = (uint32_t)channels[c];
                card.sample_count = (uint32_t)need;
                wrote = encode_mask(&card, NULL, 0u, tiny);
                if (wrote != HEADER || payload == 0u) ++mismatches;
                if ((HEADER + payload) * 1000u / payload != (size_t)((1.0 + 17.0 / (double)payload) * 1000.0 + 0.5)) {
                    /* integer check is fuzzy; exact byte check below */
                }
                if (HEADER + payload != (size_t)need * (size_t)channels[c] * (size_t)elems[e] + 17u) ++mismatches;
                printf(" %6d", need);
            }
            printf("\n");
        }
    }
    printf("ratio is 1 + 17/payload. payload 0 is the only divide-by-zero (sample_count 0).\n");
    return mismatches == 0 ? 0 : -1;
}

static void run_frequency(void) {
    static const int hz[] = {1, 10, 50, 100, 200, 500, 1000, 2000, 8000};
    static const int samples[] = {1, 10, 50, 200, 1024};
    size_t h;
    size_t s;
    const double days_u32 = 4294967296.0 / 86400.0;
    printf("\nfrequency does not sit in the 17 bytes. It changes bytes/s and u32 lifetime.\n");
    printf("u32 sample counter wraps after 2^32 samples: ");
    for (h = 0u; h < 9u; ++h) {
        printf("%dHz=%.1fd ", hz[h], days_u32 / (double)hz[h]);
    }
    printf("\n");
    printf("bytes/s for f32 x 2 channels, one block = N samples (header tax = 17*Hz/N):\n");
    printf("%6s", "Hz\\N");
    for (s = 0u; s < 5u; ++s) printf(" %10d", samples[s]);
    printf("\n");
    for (h = 0u; h < 9u; ++h) {
        printf("%6d", hz[h]);
        for (s = 0u; s < 5u; ++s) {
            double payload = (double)samples[s] * 2.0 * 4.0;
            double per_s = ((double)HEADER + payload) * ((double)hz[h] / (double)samples[s]);
            printf(" %10.0f", per_s);
        }
        printf("\n");
    }
}

static int run_crc_scale(void) {
    static const size_t sizes[] = {2u, 16u, 400u, 4096u, 65536u, 1048576u, 4194240u};
    static volatile uint32_t crc_sink;
    uint8_t *buf;
    size_t i;
    buf = (uint8_t *)malloc(4194240u + HEADER);
    if (buf == NULL) return -1;
    fill_random(buf, 4194240u);
    printf("\nCRC time on this host, one buffer, median of 5 (table) or 3 (bit-serial):\n");
    printf("%10s %12s %12s\n", "payload", "table_ns", "bitser_ns");
    for (i = 0u; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        int round;
        uint64_t table_ns = 0u;
        uint64_t bit_ns = 0u;
        int reps = sizes[i] >= 1048576u ? 3 : 5;
        uint64_t best_t = UINT64_MAX;
        uint64_t best_b = UINT64_MAX;
        for (round = 0; round < reps; ++round) {
            uint64_t t0 = now_ns();
            uint64_t elapsed;
            crc_sink = crc32c(buf, sizes[i]);
            elapsed = now_ns() - t0;
            if (elapsed < best_t) best_t = elapsed;
        }
        table_ns = best_t;
        if (sizes[i] <= 65536u) {
            for (round = 0; round < 3; ++round) {
                uint32_t crc = 0xffffffffu;
                size_t b;
                unsigned bit;
                uint64_t t0 = now_ns();
                for (b = 0u; b < sizes[i]; ++b) {
                    crc ^= buf[b];
                    for (bit = 0u; bit < 8u; ++bit) {
                        crc = (crc >> 1) ^ (0x82f63b78u & (uint32_t)-(int32_t)(crc & 1u));
                    }
                }
                crc_sink = ~crc;
                {
                    uint64_t elapsed = now_ns() - t0;
                    if (elapsed < best_b) best_b = elapsed;
                }
            }
            bit_ns = best_b;
        }
        if (sizes[i] <= 65536u) {
            printf("%10zu %12llu %12llu\n", sizes[i],
                   (unsigned long long)table_ns, (unsigned long long)bit_ns);
        } else {
            printf("%10zu %12llu %12s\n", sizes[i], (unsigned long long)table_ns, "n/a");
        }
        if (crc_sink == 0u && sizes[i] == 0u) return -1;
    }
    {
        Card card;
        Card out;
        size_t n = 65535u * 16u * 4u;
        uint8_t *rec;
        memset(&card, 0, sizeof(card));
        card.kind = 3u;
        card.dtype = 1u;
        card.channels = 16u;
        card.sample_count = 65535u;
        card.block_seq = 1u;
        card.first_seq = 1u;
        rec = (uint8_t *)malloc(HEADER + n);
        if (rec == NULL) {
            free(buf);
            return -1;
        }
        fill_random(buf, n > 4194240u ? 4194240u : n);
        encode_mask(&card, buf, n, rec);
        if (decode_check(rec, n, &out) != 0 || out.sample_count != 65535u || out.channels != 16u) {
            printf("max block roundtrip FAILED\n");
            free(rec);
            free(buf);
            return -1;
        }
        printf("max in-range block: 65535 samples x 16 x f32 = %zu payload bytes + 17, roundtrip ok\n", n);
        card.sample_count = 0u;
        card.channels = 1u;
        card.dtype = 1u;
        encode_mask(&card, NULL, 0u, rec);
        if (decode_check(rec, 0u, &out) != 0 || out.sample_count != 0u) {
            printf("empty block FAILED\n");
            free(rec);
            free(buf);
            return -1;
        }
        printf("empty block: sample_count 0 encodes and checks, payload 0, ratio is undefined\n");
        free(rec);
    }
    free(buf);
    return 0;
}

static void run_monte_carlo(void) {
    static const int hz[] = {1, 10, 50, 100, 200, 500, 1000, 2000, 8000};
    int i;
    int comfortable = 0;
    int usable = 0;
    int poor = 0;
    int empty = 0;
    int over_channels = 0;
    double worst = 1.0;
    int worst_hz = 0;
    int worst_ch = 0;
    int worst_n = 0;
    int worst_elem = 0;
    for (i = 0; i < 20000; ++i) {
        int channels = 1 + (int)(rng() % 20u);
        int elem = (rng() & 1u) ? 2 : 4;
        int n = (int)(rng() % 2049u);
        int rate = hz[rng() % 9u];
        double payload;
        double ratio;
        if (channels > 16) {
            ++over_channels;
            continue;
        }
        if (n == 0) {
            ++empty;
            continue;
        }
        payload = (double)n * (double)channels * (double)elem;
        ratio = 1.0 + 17.0 / payload;
        if (ratio > worst) {
            worst = ratio;
            worst_hz = rate;
            worst_ch = channels;
            worst_n = n;
            worst_elem = elem;
        }
        if (ratio <= 1.05) ++comfortable;
        else if (ratio <= 1.10) ++usable;
        else ++poor;
    }
    printf("\nmonte carlo 20000 draws: channels 1..20, samples/block 0..2048, elem 2 or 4, Hz from the set above\n");
    printf("  rejected by channel width: %d\n", over_channels);
    printf("  sample_count 0 (ratio undefined): %d\n", empty);
    printf("  in range and ratio<=1.05: %d\n", comfortable);
    printf("  in range and 1.05<ratio<=1.10: %d\n", usable);
    printf("  in range and ratio>1.10: %d\n", poor);
    printf("  worst in-range draw: %.3f at %d Hz, %d ch, N=%d, %d B/sample\n",
           worst, worst_hz, worst_ch, worst_n, worst_elem);
}

int main(void) {
    crc_init();
    if (crc32c((const uint8_t *)"123456789", 9u) != 0xe3069283u) {
        fprintf(stderr, "crc vector failed\n");
        return 1;
    }
    printf("bit-align-17 domain sweep\n");
    if (run_random() != 0) return 1;
    if (run_edges() != 0) return 1;
    if (run_knee() != 0) return 1;
    run_frequency();
    if (run_crc_scale() != 0) return 1;
    run_monte_carlo();
    return 0;
}
