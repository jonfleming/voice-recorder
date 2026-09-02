#include "wav.h"
#include <string.h>
#include <stdio.h>

esp_err_t wav_header_init(wav_header_t *hdr, uint32_t data_size)
{
    if (!hdr) return ESP_ERR_INVALID_ARG;
    memcpy(hdr->chunk_id, "RIFF", 4);
    hdr->chunk_size = 36 + data_size;
    memcpy(hdr->format, "WAVE", 4);
    memcpy(hdr->subchunk1_id, "fmt ", 4);
    hdr->subchunk1_size = 16;
    hdr->audio_format = 1;
    hdr->num_channels = WAV_CHANNELS;
    hdr->sample_rate = WAV_SAMPLE_RATE;
    hdr->byte_rate = WAV_BYTE_RATE;
    hdr->block_align = WAV_BLOCK_ALIGN;
    hdr->bits_per_sample = WAV_BITS_PER_SAMPLE;
    memcpy(hdr->subchunk2_id, "data", 4);
    hdr->subchunk2_size = data_size;
    return ESP_OK;
}

esp_err_t wav_header_write(FILE *f, uint32_t data_size)
{
    wav_header_t hdr;
    wav_header_init(&hdr, data_size);
    if (fwrite(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t wav_header_update(FILE *f, uint32_t data_size)
{
    if (!f) return ESP_ERR_INVALID_ARG;
    if (fseek(f, 0, SEEK_SET) != 0) return ESP_FAIL;
    return wav_header_write(f, data_size);
}

esp_err_t wav_header_parse(FILE *f, wav_header_t *out, uint32_t *data_start)
{
    if (!f || !out) return ESP_ERR_INVALID_ARG;
    if (fseek(f, 0, SEEK_SET) != 0) return ESP_FAIL;
    if (fread(out, 1, sizeof(wav_header_t), f) != sizeof(wav_header_t)) return ESP_FAIL;
    if (memcmp(out->chunk_id, "RIFF", 4) != 0 || memcmp(out->format, "WAVE", 4) != 0) return ESP_ERR_NOT_FOUND;
    if (out->audio_format != 1) return ESP_ERR_NOT_SUPPORTED;
    // Handle extra fmt bytes if subchunk1_size >16
    if (out->subchunk1_size > 16) {
        if (fseek(f, out->subchunk1_size - 16, SEEK_CUR) != 0) return ESP_FAIL;
        // After skipping, we already consumed subchunk2 header? Actually if fmt larger, need to re-sync
        // Simplistic: assume 16
    }
    // If file has extra chunks, find "data"
    if (memcmp(out->subchunk2_id, "data", 4) != 0) {
        // Seek for "data"
        uint8_t buf[8];
        // We already read 44 bytes, out->subchunk2_id is not "data", so search
        // Rewind and scan
        fseek(f, 12, SEEK_SET); // after RIFF/WAVE
        long pos = 12;
        while (pos < 1024) {
            char id[4];
            uint32_t sz;
            if (fread(id,1,4,f)!=4) return ESP_FAIL;
            if (fread(&sz,1,4,f)!=4) return ESP_FAIL;
            pos += 8;
            if (memcmp(id,"fmt ",4)==0) {
                fseek(f, sz, SEEK_CUR); pos += sz;
            } else if (memcmp(id,"data",4)==0) {
                memcpy(out->subchunk2_id, id, 4);
                out->subchunk2_size = sz;
                if (data_start) *data_start = (uint32_t)ftell(f);
                return ESP_OK;
            } else {
                fseek(f, sz, SEEK_CUR); pos += sz;
            }
        }
        return ESP_FAIL;
    }
    if (data_start) *data_start = sizeof(wav_header_t);
    return ESP_OK;
}
