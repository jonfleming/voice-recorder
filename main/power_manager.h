#pragma once
#include "esp_err.h"
#include <stdbool.h>

esp_err_t power_manager_init(void);
bool power_manager_is_display_on(void);
esp_err_t power_manager_set_display(bool on);
esp_err_t power_manager_toggle_display(void);
