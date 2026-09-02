# Pinout — Waveshare ESP32-S3 Touch AMOLED 1.8" (verified against BSP v2.0.3)

Source: `waveshare/esp32_s3_touch_amoled_1_8` BSP (`include/bsp/esp32_s3_touch_amoled_1_8.h` + `esp32_s3_touch_amoled_1_8.c`,
QSPI example `13_display_colorbar`).

| Function | Signal | GPIO | Notes |
|---|---|---|---|
| **Display QSPI** | CS | 12 | BSP_LCD_CS |
| | PCLK (CLK) | 11 | BSP_LCD_PCLK, SPI2 host QSPI |
| | DATA0 | 4 | BSP_LCD_DATA0 |
| | DATA1 | 5 | BSP_LCD_DATA1 |
| | DATA2 | 6 | BSP_LCD_DATA2 |
| | DATA3 | 7 | BSP_LCD_DATA3 |
| | RST | NC | via CO5300 |
| | Backlight | NC | via 0x51 DCS |
| **Touch I2C** | SDA | 15 | BSP_I2C_SDA, I2C_NUM_1 400kHz |
| | SCL | 14 | BSP_I2C_SCL |
| | INT | 21 | BSP_LCD_TOUCH_INT (active low) |
| | RST | NC | shared with LCD |
| | Addr V2 | 0x15 | CST820 (probe) else FT3168 |
| **Audio I2S** | MCLK | 16 | BSP_I2S_MCLK |
| | BCLK | 9 | BSP_I2S_SCLK |
| | LRCK/WS | 45 | BSP_I2S_LCLK |
| | DOUT (DAC → spk) | 8 | BSP_I2S_DOUT |
| | DSIN (mic → ADC) | 10 | BSP_I2S_DSIN |
| | PA enable | 46 | BSP_POWER_AMP_IO |
| | Codec addr | 0x18 | ES8311 |
| **SDMMC** | CLK | 2 | BSP_SD_CLK, 1-bit SDMMC Host |
| | CMD | 1 | BSP_SD_CMD |
| | D0 | 3 | BSP_SD_D0 |
| **I2C bus** | port | 1 | BSP_I2C_NUM |
| **Buttons** | Button A (record) | 0 | BOOT, pull-up, active-low. `CONFIG_BUTTON_A_GPIO` |
| | Button B (play) | 21* | default touch INT pin; if conflict set to -1 or spare. `CONFIG_BUTTON_B_GPIO`. BSP reports `BSP_CAPS_BUTTONS 0` — wired as discrete GPIOs. |
| **PMU** | AXP2101 | I2C 0x34 | power/battery; PWR button via PMU (long-press power off). |
| **IMU** | QMI8658 | I2C | not used by recorder |
| **RTC** | PCF85063 | I2C | optional timestamp source |
| **USB** | Type-C | 19/20 | D-/D+ native USB |
| **PSRAM/Flash** | Octal PSRAM + 16 MB Flash | — | `CONFIG_SPIRAM_MODE_OCT` |

### Notes
- Display: 368×448 RGB565, QSPI (CO5300 driver, `esp_lcd_co5300`). X-gap 0x10 on V2 board (CST820 detected).
- SD: SDMMC 1-bit width, `BSP_SD_MOUNT_POINT "/sdcard"`, FAT32.
- I2S: 16 kHz mono 16-bit (spec), BSP default 22.05 kHz overridden in `audio_commons.c`.
- Exposed pads: 7× GPIO + I2C/UART/USB footprints (1.27 mm) for expansion.
- Power: MX1.25 3.7 V Li-ion header, charge via AXP2101.
