#include "button_manager.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "btn_mgr";

#ifndef CONFIG_BUTTON_A_GPIO
#define CONFIG_BUTTON_A_GPIO 0
#endif
#ifndef CONFIG_BUTTON_B_GPIO
#define CONFIG_BUTTON_B_GPIO 21
#endif
#ifndef CONFIG_BUTTON_DEBOUNCE_MS
#define CONFIG_BUTTON_DEBOUNCE_MS 50
#endif

static gpio_num_t s_btn_a = CONFIG_BUTTON_A_GPIO;
static gpio_num_t s_btn_b = CONFIG_BUTTON_B_GPIO;

static volatile bool s_a_pressed = false;
static volatile bool s_b_pressed = false;

#define TOUCH_INT_GPIO 21  // BSP_LCD_TOUCH_INT - must not be reconfigured as Button B

// Debounced state
static void button_poll_task(void *arg)
{
    (void)arg;
    bool last_a = true, last_b = true;
    int64_t last_a_change = 0, last_b_change = 0;
    bool stable_a = true, stable_b = true;
    bool b_enabled = (s_btn_b >= 0 && s_btn_b < GPIO_NUM_MAX);

    while (1) {
        bool raw_a = (s_btn_a >= 0) ? gpio_get_level(s_btn_a) : true; // active low (pullup)
        bool raw_b = b_enabled ? gpio_get_level(s_btn_b) : true;

        int64_t now = esp_timer_get_time()/1000;

        // Debounce A
        if (raw_a != last_a) { last_a = raw_a; last_a_change = now; }
        if ((now - last_a_change) > CONFIG_BUTTON_DEBOUNCE_MS) {
            if (stable_a != raw_a) {
                stable_a = raw_a;
                if (stable_a == false) { // pressed (low)
                    s_a_pressed = true;
                    ESP_LOGI(TAG, "Button A pressed");
                }
            }
        }
        // Debounce B (only if enabled)
        if (b_enabled) {
            if (raw_b != last_b) { last_b = raw_b; last_b_change = now; }
            if ((now - last_b_change) > CONFIG_BUTTON_DEBOUNCE_MS) {
                if (stable_b != raw_b) {
                    stable_b = raw_b;
                    if (stable_b == false) {
                        s_b_pressed = true;
                        ESP_LOGI(TAG, "Button B pressed");
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t button_manager_init(void)
{
    // Skip if GPIO is NC (-1)
    if (s_btn_a >= 0) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << s_btn_a,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&cfg));
        ESP_LOGI(TAG, "Button A GPIO %d pullup", (int)s_btn_a);
    } else {
        ESP_LOGW(TAG, "Button A disabled");
    }
    if (s_btn_b >= 0) {
        if (s_btn_b == TOUCH_INT_GPIO) {
            ESP_LOGW(TAG, "Button B GPIO %d conflicts with touch INT - disabling B (use BOOT only, B via touch UI)", (int)s_btn_b);
            ESP_LOGW(TAG, "Playback must be triggered via touch UI or reconfigure BUTTON_B_GPIO to free pin");
            s_btn_b = GPIO_NUM_NC;
        } else {
            gpio_config_t cfg = {
                .pin_bit_mask = 1ULL << s_btn_b,
                .mode = GPIO_MODE_INPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            esp_err_t r = gpio_config(&cfg);
            if (r != ESP_OK) {
                ESP_LOGW(TAG, "Button B GPIO %d config failed %s, disabling B", (int)s_btn_b, esp_err_to_name(r));
                s_btn_b = GPIO_NUM_NC;
            } else {
                ESP_LOGI(TAG, "Button B GPIO %d pullup", (int)s_btn_b);
            }
        }
    }
    if (s_btn_b < 0) {
        ESP_LOGI(TAG, "Button B disabled - playback via touch UI or assign free GPIO via menuconfig");
    }
    xTaskCreate(button_poll_task, "btn_poll", 4096, NULL, 5, NULL);
    return ESP_OK;
}

bool button_a_was_pressed(void)
{
    if (s_a_pressed) { s_a_pressed = false; return true; }
    return false;
}
bool button_b_was_pressed(void)
{
    if (s_b_pressed) { s_b_pressed = false; return true; }
    return false;
}
bool button_a_is_held(void) { return s_btn_a>=0 ? gpio_get_level(s_btn_a)==0 : false; }
bool button_b_is_held(void) { return s_btn_b>=0 ? gpio_get_level(s_btn_b)==0 : false; }
