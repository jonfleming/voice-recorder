#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// Button manager: debounces two buttons and detects short press.
// Maps to BOOT (GPIO0) and PWR/secondary (configurable, defaults GPIO21 touch INT unused as input?).
// User can change via Kconfig CONFIG_BUTTON_A_GPIO / CONFIG_BUTTON_B_GPIO.

esp_err_t button_manager_init(void);
bool button_a_was_pressed(void); // consumes event (edge)
bool button_b_was_pressed(void);
bool button_a_is_held(void);
bool button_b_is_held(void);
