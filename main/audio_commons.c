#include "audio_commons.h"
#include "board.h"
#include "esp_log.h"
#include "esp_codec_dev_defaults.h"
#include "driver/i2s_std.h"

static const char *TAG = "audio_common";
static bool s_inited = false;
static esp_codec_dev_handle_t s_speaker = NULL;
static esp_codec_dev_handle_t s_mic = NULL;

static esp_err_t ensure_i2s_16k(void)
{
    // Must be the first bsp_audio_init() call — the BSP early-returns after that.
    // 2.06: shared I2S is stereo (ES7210 dual-mic + ES8311 DAC).
    // 1.8: ES8311 full-duplex mono.
    i2s_slot_mode_t slot = (BOARD_AUDIO_CHANNELS == 2) ? I2S_SLOT_MODE_STEREO
                                                       : I2S_SLOT_MODE_MONO;
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, slot),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws   = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din  = BSP_I2S_DSIN,
            .invert_flags = {.mclk_inv=false,.bclk_inv=false,.ws_inv=false},
        },
    };
    esp_err_t r = bsp_audio_init(&cfg);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "bsp_audio_init failed: %s", esp_err_to_name(r));
        return r;
    }
    ESP_LOGI(TAG, "I2S initialized at 16kHz %s (BCLK=%d DOUT=%d DIN=%d)",
             BOARD_AUDIO_CHANNELS == 2 ? "stereo" : "mono",
             (int)BSP_I2S_SCLK, (int)BSP_I2S_DOUT, (int)BSP_I2S_DSIN);
    return ESP_OK;
}

esp_err_t audio_common_init(void)
{
    if (s_inited) return ESP_OK;
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(ensure_i2s_16k());
    // Create speaker and mic codec devs (they will reuse I2S)
    s_speaker = bsp_audio_codec_speaker_init();
    if (!s_speaker) {
        ESP_LOGE(TAG, "speaker init failed");
        return ESP_FAIL;
    }
    s_mic = bsp_audio_codec_microphone_init();
    if (!s_mic) {
        ESP_LOGE(TAG, "mic init failed");
        return ESP_FAIL;
    }
    s_inited = true;
    ESP_LOGI(TAG, "audio_common ready speaker=%p mic=%p", s_speaker, s_mic);
    return ESP_OK;
}

esp_codec_dev_handle_t audio_common_speaker_handle(void) { return s_speaker; }
esp_codec_dev_handle_t audio_common_mic_handle(void) { return s_mic; }

esp_err_t audio_common_set_volume(int volume)
{
    if (!s_speaker) return ESP_ERR_INVALID_STATE;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    int ret = esp_codec_dev_set_out_vol(s_speaker, volume);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "set volume failed %d", ret);
        return ESP_FAIL;
    }
    return ESP_OK;
}
