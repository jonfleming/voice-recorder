#pragma once
#include "esp_err.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"

// Shared helpers: ensure I2S at 16k and provide codec handles
esp_err_t audio_common_init(void);
esp_codec_dev_handle_t audio_common_speaker_handle(void);
esp_codec_dev_handle_t audio_common_mic_handle(void);
esp_err_t audio_common_set_volume(int volume); // 0-100
