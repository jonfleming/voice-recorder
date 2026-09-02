#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t recorder_init(void);
esp_err_t recorder_start(void);
esp_err_t recorder_stop(void);
bool      recorder_is_recording(void);
uint32_t  recorder_get_elapsed_sec(void);
uint32_t  recorder_get_data_bytes(void);
uint16_t  recorder_get_rms_level(void); // 0..1000 approx
const char* recorder_get_current_path(void);
