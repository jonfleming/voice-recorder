#include "audio_recorder.h"
#include "audio_commons.h"
#include "wav.h"
#include "board.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "recorder";

#define REC_RING_FRAMES  1024
#define REC_TASK_STACK   4096
#define REC_TASK_PRIO    6
#define REC_READ_CHUNK   512  // bytes (256 samples)

static bool s_recording = false;
static FILE *s_file = NULL;
static char s_path[64];
static uint32_t s_data_bytes = 0;
static uint16_t s_rms = 0;
static bool s_mic_open = false;
static int64_t s_start_us = 0;
static TaskHandle_t s_task = NULL;
static esp_codec_dev_handle_t s_mic = NULL;

static void generate_filename(char *out, size_t len)
{
    // Prefer time from RTC if available; fallback to esp_timer
    time_t now = time(NULL);
    struct tm tm;
    if (now < 100000) {
        // No NTP/RTC yet; use esp_timer monotonic as pseudo timestamp
        // Format as 19700101_HHMMSS using uptime
        int64_t sec = esp_timer_get_time()/1000000;
        int hr = (sec/3600)%24;
        int mn = (sec/60)%60;
        int sc = sec %60;
        snprintf(out, len, BSP_SD_MOUNT_POINT "/%04d%02d%02d_%02d%02d%02d.wav",
                 1970,1,1, hr,mn,sc);
        // Avoid collision: append counter if exists
        int cnt=0;
        char test[64];
        strcpy(test, out);
        while (cnt<100) {
            struct stat st;
            if (stat(test, &st)!=0) { strcpy(out,test); break;}
            snprintf(test,sizeof(test), BSP_SD_MOUNT_POINT "/19700101_%02d%02d%02d_%02d.wav", hr,mn,sc,cnt++);
            strcpy(out,test);
        }
        return;
    }
    localtime_r(&now, &tm);
    snprintf(out, len, BSP_SD_MOUNT_POINT "/%04d%02d%02d_%02d%02d%02d.wav",
             tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    // Avoid overwrite: if exists, append _1 etc? Simple add millis
    struct stat st;
    if (stat(out, &st)==0) {
        char base[64];
        strncpy(base, out, sizeof(base));
        base[strlen(base)-4]=0; // strip .wav
        snprintf(out, len, "%s_%02d.wav", base, (int)(esp_timer_get_time()/100000)%100);
    }
}

static uint16_t compute_level(int16_t *samples, size_t n)
{
    if (n==0) return 0;
    int32_t peak = 0;
    int64_t sum = 0;
    for (size_t i=0;i<n;i++) {
        int32_t v = samples[i];
        if (v < 0) v = -v;
        if (v > peak) peak = v;
        sum += (int64_t)v * v;
    }
    double rms = sqrt((double)sum / n);
    // Combine RMS and peak for more visible waveform: 70% peak + 30% RMS, with 4x visual gain
    double level = peak * 0.7 + rms * 0.3;
    // Amplify: voice peak often 1000-5000, map 4000→1000 for visible deflection
    level = level * 1.4;
    if (level > 32768) level = 32768;
    uint16_t out = (uint16_t)(level * 1000.0 / 4000.0); // 4000 peak → full scale
    if (out > 1000) out = 1000;
    if (out < 5 && peak > 80) out = 5;
    return out;
}

static void recorder_task(void *arg)
{
    (void)arg;
    // Read enough PCM for REC_READ_CHUNK bytes of mono WAV output.
    // 2.06 ES7210 delivers interleaved L/R; we average to mono for the file.
    int16_t raw[REC_READ_CHUNK / 2 * BOARD_AUDIO_CHANNELS];
    int16_t mono[REC_READ_CHUNK / 2];
    ESP_LOGI(TAG, "recorder task started");
    while (s_recording) {
        if (!s_mic || !s_file) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        int ret = esp_codec_dev_read(s_mic, raw, sizeof(raw));
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "mic read failed %d", ret);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        const int16_t *out = raw;
        size_t n_mono = REC_READ_CHUNK / 2;
        if (BOARD_AUDIO_CHANNELS == 2) {
            for (size_t i = 0; i < n_mono; i++) {
                int32_t s = ((int32_t)raw[i * 2] + (int32_t)raw[i * 2 + 1]) >> 1;
                mono[i] = (int16_t)s;
            }
            out = mono;
        }
        s_rms = compute_level((int16_t *)out, n_mono);
        size_t written = fwrite(out, 1, REC_READ_CHUNK, s_file);
        if (written != REC_READ_CHUNK) {
            ESP_LOGE(TAG, "SD write failed");
            // continue but log
        } else {
            s_data_bytes += written;
        }
        // Optional: flush periodically (every 4k)
        if ((s_data_bytes % 4096)==0) fflush(s_file);
    }
    ESP_LOGI(TAG, "recorder task exit");
    vTaskDelete(NULL);
}

esp_err_t recorder_init(void)
{
    esp_err_t r = audio_common_init();
    if (r != ESP_OK) return r;
    s_mic = audio_common_mic_handle();
    if (!s_mic) {
        ESP_LOGE(TAG, "mic handle null");
        return ESP_FAIL;
    }
    // Do NOT leave codec open here; open is done in recorder_start to avoid double-open
    ESP_LOGI(TAG, "recorder initialized (codec closed, will open on start)");
    return ESP_OK;
}

esp_err_t recorder_start(void)
{
    if (s_recording) return ESP_ERR_INVALID_STATE;
    generate_filename(s_path, sizeof(s_path));
    ESP_LOGI(TAG, "Start recording -> %s", s_path);
    s_file = fopen(s_path, "wb");
    if (!s_file) {
        ESP_LOGE(TAG, "fopen failed %s", s_path);
        return ESP_FAIL;
    }
    // Write placeholder header (0 data)
    if (wav_header_write(s_file, 0) != ESP_OK) {
        fclose(s_file); s_file=NULL; return ESP_FAIL;
    }
    s_data_bytes = 0;
    s_rms = 0;
    s_start_us = esp_timer_get_time();
    if (!s_mic) s_mic = audio_common_mic_handle();
    if (s_mic_open) {
        esp_codec_dev_close(s_mic);
        s_mic_open = false;
    }
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample=16,
        .channel=BOARD_AUDIO_CHANNELS,
        .channel_mask=0,
        .sample_rate=16000,
        .mclk_multiple=256
    };
    int ret = esp_codec_dev_open(s_mic, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "mic open failed %d", ret);
        fclose(s_file);
        s_file = NULL;
        return ESP_FAIL;
    }
    s_mic_open = true;
    // ES7210 (2.06) and ES8311 (1.8): 30 dB. The Waveshare 2.06 example uses
    // 24 dB, which is too quiet for this recorder (same lesson as the 1.8).
    esp_codec_dev_set_in_gain(s_mic, BOARD_MIC_GAIN_DB);
    esp_codec_dev_set_in_mute(s_mic, false);
    ESP_LOGI(TAG, "mic opened sr=16000 ch=%d gain=%.0fdB", BOARD_AUDIO_CHANNELS, (double)BOARD_MIC_GAIN_DB);
    s_recording = true;
    BaseType_t created = xTaskCreate(recorder_task, "rec_task", REC_TASK_STACK, NULL, REC_TASK_PRIO, &s_task);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "rec_task create failed");
        esp_codec_dev_close(s_mic);
        fclose(s_file);
        s_file = NULL;
        s_recording = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t recorder_stop(void)
{
    if (!s_recording) return ESP_ERR_INVALID_STATE;
    s_recording = false;
    // Wait for task to exit
    vTaskDelay(pdMS_TO_TICKS(200));
    if (s_file) {
        // Update WAV header with actual size
        wav_header_update(s_file, s_data_bytes);
        fclose(s_file);
        s_file = NULL;
        ESP_LOGI(TAG, "Recording saved %s bytes=%u secs=%u", s_path, (unsigned)s_data_bytes, (unsigned)recorder_get_elapsed_sec());
        // If file too small (<1k), consider deleting? Keep anyway
        if (s_data_bytes < 1024) {
            ESP_LOGW(TAG, "Very short recording");
        }
    }
    if (s_mic && s_mic_open) {
        esp_codec_dev_close(s_mic);
        s_mic_open = false;
    }
    s_task = NULL;
    return ESP_OK;
}

bool recorder_is_recording(void) { return s_recording; }

uint32_t recorder_get_elapsed_sec(void)
{
    if (!s_recording) return s_data_bytes / WAV_BYTE_RATE;
    int64_t now = esp_timer_get_time();
    return (uint32_t)((now - s_start_us)/1000000);
}

uint32_t recorder_get_data_bytes(void) { return s_data_bytes; }
uint16_t recorder_get_rms_level(void) { return s_rms; }
const char* recorder_get_current_path(void) { return s_path; }
