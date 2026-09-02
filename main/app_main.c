#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "bsp/esp32_s3_touch_amoled_1_8.h"
#include "bsp/display.h"
#include "bsp/touch.h"

#include "filesystem_manager.h"
#include "audio_recorder.h"
#include "audio_player.h"
#include "button_manager.h"
#include "ui_manager.h"
#include "power_manager.h"
#include "state_machine.h"
#include "wav.h"

static const char *TAG = "app_main";

static app_state_t s_state = STATE_IDLE;
static wav_file_info_t s_files[FS_MAX_FILES];
static size_t s_file_count = 0;
static int s_selected = -1;

static void refresh_file_list(void)
{
    if (fs_manager_list_wav(s_files, &s_file_count, FS_MAX_FILES) != ESP_OK) {
        s_file_count = 0;
    }
    // Keep selection in range
    if (s_selected >= (int)s_file_count) s_selected = (int)s_file_count -1;
    if (s_file_count==0) s_selected=-1;
}

static void on_file_selected(int idx, const wav_file_info_t *info)
{
    (void)info;
    s_selected = idx;
    ESP_LOGI(TAG, "UI selected %d", idx);
}

static void set_state(app_state_t ns)
{
    ESP_LOGI(TAG, "State %s -> %s", state_name(s_state), state_name(ns));
    s_state = ns;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Voice Recorder firmware starting");
    ESP_LOGI(TAG, "Target: Waveshare ESP32-S3 AMOLED 1.8 368x448 QSPI, ES8311, SDMMC");

    // Init display + LVGL (Waveshare BSP handles QSPI 368x448 CO5300 + CST820/FT3168)
    // Suppress noisy I2C probe logs during detection (matches stock demo)
    esp_log_level_t i2c_log = esp_log_level_get("i2c.master");
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    lv_display_t *disp = bsp_display_start();
    esp_log_level_set("i2c.master", i2c_log);
    if (!disp) {
        ESP_LOGE(TAG, "Display init failed - check QSPI wiring and PSRAM octal mode");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    esp_err_t br = bsp_display_brightness_set(100);
    if (br != ESP_OK) {
        ESP_LOGE(TAG, "brightness_set(100) failed %s - display may stay dark", esp_err_to_name(br));
    } else {
        ESP_LOGI(TAG, "Display brightness 100%% OK - panel should be lit");
    }
    // Small settle delay for AMOLED power rail
    vTaskDelay(pdMS_TO_TICKS(120));

    // Power manager - PWR button (B) toggles display on/off for battery save
    // Uses AXP2101 PMU via same I2C bus (14/15); falls back gracefully if not found
    if (power_manager_init() != ESP_OK) {
        ESP_LOGW(TAG, "power_manager init failed - PWR toggle disabled");
    }

    // Filesystem
    esp_err_t r = fs_manager_init();
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed, UI will show error");
    }

    // Buttons (BOOT GPIO0 + secondary)
    button_manager_init();

    // Audio (preinit at 16k)
    if (recorder_init() != ESP_OK) {
        ESP_LOGE(TAG, "recorder init failed");
    }
    if (player_init() != ESP_OK) {
        ESP_LOGE(TAG, "player init failed");
    }

    // UI
    ui_init(on_file_selected);
    refresh_file_list();
    ui_show_file_browser(s_files, s_file_count, s_selected);
    set_state(STATE_FILE_BROWSER);

    int64_t last_ui_update = 0;
    int64_t last_rec_update = 0;

    while (1) {
        bool a_press = button_a_was_pressed();
        bool b_press = button_b_was_pressed() || ui_play_requested();
        int64_t now_ms = esp_timer_get_time()/1000;

        switch (s_state) {
        case STATE_IDLE:
        case STATE_FILE_BROWSER: {
            // Handle delete confirmation from long-press dialog
            int del_idx;
            if (ui_delete_requested(&del_idx)) {
                if (del_idx >= 0 && del_idx < (int)s_file_count) {
                    ESP_LOGI(TAG, "Delete file %d %s", del_idx, s_files[del_idx].path);
                    if (player_is_playing()) player_stop();
                    if (fs_manager_delete(s_files[del_idx].path) == ESP_OK) {
                        ESP_LOGI(TAG, "File deleted");
                    }
                    refresh_file_list();
                    ui_show_file_browser(s_files, s_file_count, s_selected);
                }
                break;
            }
            // While delete dialog is open, ignore other inputs except keep selection in sync
            if (ui_is_delete_dialog_open()) {
                int sel = ui_get_selected_index();
                if (sel != s_selected && sel >=0) s_selected = sel;
                break;
            }
            // Button A: start recording
            if (a_press) {
                if (recorder_start()==ESP_OK) {
                    ui_show_recording(0, 0);
                    set_state(STATE_RECORDING);
                    last_rec_update = now_ms;
                } else {
                    ESP_LOGE(TAG, "recorder_start failed");
                }
            } else if (b_press) {
                if (s_selected >=0 && s_selected < (int)s_file_count) {
                    const char *path = s_files[s_selected].path;
                    ESP_LOGI(TAG, "Play %s", path);
                    if (player_play(path)==ESP_OK) {
                        ui_show_playback(s_files[s_selected].filename, 0, s_files[s_selected].size / WAV_BYTE_RATE);
                        set_state(STATE_PLAYBACK);
                    }
                } else {
                    ESP_LOGW(TAG, "B pressed but no file selected");
                }
            }
            // Also update selection from UI tap
            int sel = ui_get_selected_index();
            if (sel != s_selected && sel >=0) s_selected = sel;

            // Periodic refresh of file list? not needed
            break;
        }
        case STATE_RECORDING: {
            // Update UI 10Hz or waveform 20-30Hz
            if (now_ms - last_rec_update > 50) { // 20Hz
                last_rec_update = now_ms;
                uint32_t sec = recorder_get_elapsed_sec();
                uint16_t rms = recorder_get_rms_level();
                ui_show_recording(sec, rms);
            }
            if (a_press) {
                ESP_LOGI(TAG, "Stop recording");
                recorder_stop();
                refresh_file_list();
                // Auto transition to file browser per spec
                // Select newest file (index 0 after sort)
                if (s_file_count>0) s_selected=0;
                ui_show_file_browser(s_files, s_file_count, s_selected);
                set_state(STATE_FILE_BROWSER);
            }
            break;
        }
        case STATE_PLAYBACK: {
            if (now_ms - last_ui_update > 200) {
                last_ui_update = now_ms;
                uint32_t el = player_get_elapsed_sec();
                uint32_t tot = player_get_total_sec();
                const char *fn = player_get_current_file();
                const char *base = fn ? strrchr(fn,'/') : NULL;
                if (base) base++; else base = fn;
                ui_show_playback(base, el, tot);
            }
            if (!player_is_playing()) {
                ESP_LOGI(TAG, "Playback ended, return to browser");
                refresh_file_list();
                ui_show_file_browser(s_files, s_file_count, s_selected);
                set_state(STATE_FILE_BROWSER);
            }
            // Allow stop? Spec says return when playback ends; we could also allow B to stop
            if (a_press || b_press) {
                // If want to interrupt, stop player
                if (player_is_playing()) player_stop();
                // will be handled next loop as not playing
            }
            break;
        }
        }

        // Handle SD removal gracefully: if not mounted, try remount periodically
        if (!fs_manager_is_mounted() && (now_ms % 2000 < 20)) {
            ESP_LOGW(TAG, "SD not mounted, retrying");
            fs_manager_init();
            refresh_file_list();
            if (s_state == STATE_FILE_BROWSER) ui_show_file_browser(s_files, s_file_count, s_selected);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
