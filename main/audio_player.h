#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t player_init(void);
esp_err_t player_play(const char *path);
esp_err_t player_stop(void);
bool      player_is_playing(void);
uint32_t  player_get_elapsed_sec(void);
uint32_t  player_get_total_sec(void);
const char* player_get_current_file(void);
