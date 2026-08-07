#ifndef CHRONOPIPE_STAGES_H
#define CHRONOPIPE_STAGES_H

#include "chronopipe/arena.h"
#include "chronopipe/graph.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t output_channels;
    const uint16_t *indices;
    cp_dtype_t dtype;
} cp_select_config_t;

typedef struct {
    uint16_t channels;
    const float *gain;
    const float *bias;
} cp_affine_config_t;

typedef struct {
    uint16_t channels;
    cp_dtype_t output_dtype;
    const float *scale;
    const float *offset;
} cp_quantize_config_t;

typedef struct {
    uint16_t channels;
    uint16_t factor;
} cp_mean_decimate_config_t;

typedef struct {
    uint16_t factor;
    uint16_t phase;
    cp_dtype_t dtype;
    uint16_t channels;
} cp_pick_decimate_config_t;

typedef struct {
    uint16_t channels;
    uint16_t window;
} cp_moving_mean_config_t;

typedef struct {
    uint16_t channels;
    uint16_t factor;
    uint16_t tap_count;
    const float *coefficients;
} cp_fir_decimate_config_t;

typedef struct {
    uint16_t channels;
    uint16_t length;
    uint16_t hop;
} cp_window_config_t;

typedef enum {
    CP_SPECTRUM_RECTANGULAR = 0,
    CP_SPECTRUM_HANN = 1
} cp_spectrum_window_t;

typedef struct {
    uint16_t channels;
    uint16_t length;
    cp_spectrum_window_t window;
} cp_spectrum_config_t;

typedef struct {
    uint16_t channels;
    uint16_t fft_length;
    float sample_rate_hz;
    float min_hz;
    float max_hz;
} cp_peak_frequency_config_t;

cp_status_t cp_stage_select_init(cp_stage_t *stage,
                                 uint32_t id,
                                 uint32_t input_stream,
                                 uint32_t output_stream,
                                 const cp_select_config_t *config,
                                 cp_arena_t *arena);
cp_status_t cp_stage_affine_init(cp_stage_t *stage,
                                 uint32_t id,
                                 uint32_t input_stream,
                                 uint32_t output_stream,
                                 const cp_affine_config_t *config,
                                 cp_arena_t *arena);
cp_status_t cp_stage_quantize_init(cp_stage_t *stage,
                                   uint32_t id,
                                   uint32_t input_stream,
                                   uint32_t output_stream,
                                   const cp_quantize_config_t *config,
                                   cp_arena_t *arena);
cp_status_t cp_stage_mean_decimate_init(cp_stage_t *stage,
                                        uint32_t id,
                                        uint32_t input_stream,
                                        uint32_t output_stream,
                                        const cp_mean_decimate_config_t *config,
                                        cp_arena_t *arena);
cp_status_t cp_stage_pick_decimate_init(cp_stage_t *stage,
                                        uint32_t id,
                                        uint32_t input_stream,
                                        uint32_t output_stream,
                                        const cp_pick_decimate_config_t *config,
                                        cp_arena_t *arena);
cp_status_t cp_stage_moving_mean_init(cp_stage_t *stage,
                                      uint32_t id,
                                      uint32_t input_stream,
                                      uint32_t output_stream,
                                      const cp_moving_mean_config_t *config,
                                      cp_arena_t *arena);
cp_status_t cp_stage_fir_decimate_init(cp_stage_t *stage,
                                       uint32_t id,
                                       uint32_t input_stream,
                                       uint32_t output_stream,
                                       const cp_fir_decimate_config_t *config,
                                       cp_arena_t *arena);
cp_status_t cp_stage_window_init(cp_stage_t *stage,
                                 uint32_t id,
                                 uint32_t input_stream,
                                 uint32_t output_stream,
                                 const cp_window_config_t *config,
                                 cp_arena_t *arena);
cp_status_t cp_stage_spectrum_init(cp_stage_t *stage,
                                   uint32_t id,
                                   uint32_t input_stream,
                                   uint32_t output_stream,
                                   const cp_spectrum_config_t *config,
                                   cp_arena_t *arena);
cp_status_t cp_stage_peak_frequency_init(cp_stage_t *stage,
                                         uint32_t id,
                                         uint32_t input_stream,
                                         uint32_t output_stream,
                                         const cp_peak_frequency_config_t *config,
                                         cp_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif
