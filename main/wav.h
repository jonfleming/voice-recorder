#pragma once
#include <stdint.h>
#include "esp_err.h"

#define WAV_SAMPLE_RATE     16000
#define WAV_BITS_PER_SAMPLE 16
#define WAV_CHANNELS        1
#define WAV_BYTE_RATE       (WAV_SAMPLE_RATE * WAV_CHANNELS * WAV_BITS_PER_SAMPLE / 8)
#define WAV_BLOCK_ALIGN     (WAV_CHANNELS * WAV_BITS_PER_SAMPLE / 8)

typedef struct __attribute__((packed)) {
    char     chunk_id[4];      // "RIFF"
    uint32_t chunk_size;       // 36 + data_size
    char     format[4];        // "WAVE"
    char     subchunk1_id[4];  // "fmt "
    uint32_t subchunk1_size;   // 16 for PCM
    uint16_t audio_format;     // 1 = PCM
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char     subchunk2_id[4];  // "data"
    uint32_t subchunk2_size;   // data_size
} wav_header_t;

esp_err_t wav_header_init(wav_header_t *hdr, uint32_t data_size);
esp_err_t wav_header_write(FILE *f, uint32_t data_size);
esp_err_t wav_header_update(FILE *f, uint32_t data_size);
esp_err_t wav_header_parse(FILE *f, wav_header_t *out, uint32_t *data_start);
