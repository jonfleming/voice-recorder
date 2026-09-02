#include "power_manager.h"
#include "bsp/esp32_s3_touch_amoled_1_8.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power_mgr";
static bool s_display_on = true;
static bool s_pm_inited = false;
static i2c_master_dev_handle_t s_pmu_dev = NULL;
static TaskHandle_t s_task = NULL;

#define AXP2101_ADDR 0x34
#define AXP2101_INTEN2 0x41
#define AXP2101_INTSTS1 0x48
#define AXP2101_INTSTS2 0x49
#define AXP2101_INTSTS3 0x4A
// PKEY short press is bit 3 in INTSTS2 ( _BV(11) >>8 )
#define PKEY_SHORT_MASK  (1 << 3) // INTSTS2 bit3
#define PKEY_SHORT_EN    (1 << 3) // INTEN2 bit3

static esp_err_t pmu_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_pmu_dev, buf, 2, 100);
}

static esp_err_t pmu_read_reg(uint8_t reg, uint8_t *out)
{
    return i2c_master_transmit_receive(s_pmu_dev, &reg, 1, out, 1, 100);
}

static esp_err_t pmu_read_sts(uint8_t *s1, uint8_t *s2, uint8_t *s3)
{
    if (pmu_read_reg(AXP2101_INTSTS1, s1) != ESP_OK) return ESP_FAIL;
    if (pmu_read_reg(AXP2101_INTSTS2, s2) != ESP_OK) return ESP_FAIL;
    if (pmu_read_reg(AXP2101_INTSTS3, s3) != ESP_OK) return ESP_FAIL;
    return ESP_OK;
}

static esp_err_t pmu_clear_irq(void)
{
    // Write 0xFF to clear all status bits (write 1 to clear)
    uint8_t ff = 0xFF;
    if (pmu_write_reg(AXP2101_INTSTS1, ff) != ESP_OK) return ESP_FAIL;
    if (pmu_write_reg(AXP2101_INTSTS2, ff) != ESP_OK) return ESP_FAIL;
    if (pmu_write_reg(AXP2101_INTSTS3, ff) != ESP_OK) return ESP_FAIL;
    return ESP_OK;
}

bool power_manager_is_display_on(void) { return s_display_on; }

esp_err_t power_manager_set_display(bool on)
{
    if (on == s_display_on) return ESP_OK;
    if (on) {
        ESP_LOGI(TAG, "Display ON (PWR toggle)");
        esp_err_t r = bsp_display_backlight_on();
        if (r != ESP_OK) ESP_LOGW(TAG, "backlight_on %s", esp_err_to_name(r));
        // Also ensure panel is on (if we have handle, brightness covers it)
        s_display_on = true;
    } else {
        ESP_LOGI(TAG, "Display OFF (PWR toggle) - saving battery");
        esp_err_t r = bsp_display_backlight_off();
        if (r != ESP_OK) ESP_LOGW(TAG, "backlight_off %s", esp_err_to_name(r));
        s_display_on = false;
    }
    return ESP_OK;
}

esp_err_t power_manager_toggle_display(void)
{
    return power_manager_set_display(!s_display_on);
}

static void pwr_monitor_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "PWR monitor task started (poll AXP2101 0x34)");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(120));
        if (!s_pmu_dev) {
            continue;
        }
        uint8_t s1, s2, s3;
        if (pmu_read_sts(&s1, &s2, &s3) != ESP_OK) {
            // occasional I2C NACK when touch also polling - ignore
            continue;
        }
        if (s2 & PKEY_SHORT_MASK) {
            ESP_LOGI(TAG, "PWR short press detected (INTSTS2=0x%02x) -> toggle display", s2);
            pmu_clear_irq();
            power_manager_toggle_display();
            // debounce - wait a bit before next detection
            vTaskDelay(pdMS_TO_TICKS(400));
            pmu_clear_irq();
        } else if (s1 || s2 || s3) {
            // Clear any other IRQs to keep status clean
            // Only clear if we saw something to avoid unnecessary writes
            if (s2 & 0xFC || s1 || s3) {
                // keep short press cleared above, clear rest too
                pmu_clear_irq();
            }
        }
    }
}

esp_err_t power_manager_init(void)
{
    if (s_pm_inited) return ESP_OK;
    // Use BSP I2C bus (already inited by display)
    esp_err_t r = bsp_i2c_init();
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "bsp_i2c_init failed %s", esp_err_to_name(r));
        return r;
    }
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) {
        ESP_LOGE(TAG, "I2C bus handle null");
        return ESP_FAIL;
    }
    // Probe AXP2101
    if (i2c_master_probe(bus, AXP2101_ADDR, 100) != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 0x34 not found - PWR button via PMU unavailable, display toggle disabled");
        s_display_on = true;
        s_pm_inited = true;
        return ESP_OK;
    }
    ESP_LOGI(TAG, "AXP2101 found at 0x34, configuring PWR key IRQ");
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = 100000,
    };
    r = i2c_master_bus_add_device(bus, &dev_cfg, &s_pmu_dev);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "add PMU device failed %s", esp_err_to_name(r));
        return r;
    }
    // Enable PWR key short press IRQ (INTEN2 bit3) and clear status
    uint8_t inten2 = 0;
    if (pmu_read_reg(AXP2101_INTEN2, &inten2) == ESP_OK) {
        inten2 |= PKEY_SHORT_EN;
        pmu_write_reg(AXP2101_INTEN2, inten2);
        ESP_LOGI(TAG, "AXP2101 INTEN2=0x%02x (PKEY short enabled)", inten2);
    }
    pmu_clear_irq();
    s_display_on = true;
    s_pm_inited = true;
    // Start monitor task
    BaseType_t ok = xTaskCreate(pwr_monitor_task, "pwr_mon", 3072, NULL, 4, &s_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "pwr task create failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "PWR button (B) will toggle display on/off");
    return ESP_OK;
}
