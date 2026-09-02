#pragma once
#include "esp_err.h"
#include "filesystem_manager.h"
#include <stdbool.h>

// UI manager: LVGL screens
// Recording screen: "Recording..." + elapsed mm:ss + waveform bar
// File browser: scrollable list, tap to select
// Playback: "Playing: <filename>" + elapsed mm:ss
// Idle: same as file browser with hint

typedef void (*file_selected_cb_t)(int index, const wav_file_info_t *info);

esp_err_t ui_init(file_selected_cb_t cb);
esp_err_t ui_show_idle(void);
esp_err_t ui_show_recording(uint32_t elapsed_sec, uint16_t rms);
esp_err_t ui_show_file_browser(const wav_file_info_t *files, size_t count, int selected);
esp_err_t ui_show_playback(const char *filename, uint32_t elapsed_sec, uint32_t total_sec);
void ui_task_tick(void); // call periodically unlocked? uses bsp_display_lock
int  ui_get_selected_index(void);
bool ui_play_requested(void); // true if user tapped on-screen Play (when Button B disabled)
bool ui_delete_requested(int *idx); // true if Delete confirmed after long-press, idx set
bool ui_is_delete_dialog_open(void);
