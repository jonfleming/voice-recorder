#pragma once
/*
 * board.h — board abstraction for Waveshare AMOLED variants
 *
 * Select via Kconfig `TARGET_BOARD_CHOICE` (menuconfig → Voice Recorder Config → Target board).
 * - 1.8"  368x448 CO5300
 * - 2.06" 410x502 CO5300/SH8601  (BSP uses SH8601 driver, compatible CO5300 init)
 *
 * Pin differences handled by the Waveshare BSPs themselves:
 *   1.8:  I2S 16/9/45/8/10/46  touch INT 21  RST NC   (CO5300)
 *   2.06: I2S 16/41/45/40/42/46 touch INT 38 RST 8/9  (SH8601)
 *
 * Application code should include this header instead of the bare BSP header.
 * It conditionally pulls the correct BSP and re-exports unified names.
 */
#include "sdkconfig.h"

#if CONFIG_TARGET_BOARD_2_06
  #include "bsp/esp32_s3_touch_amoled_2_06.h"
  // 2.06 BSP already defines BSP_LCD_H_RES=410 V_RES=502
  #ifndef BOARD_NAME
  #define BOARD_NAME "Waveshare ESP32-S3 AMOLED 2.06\" 410x502"
  #endif
  #ifndef BOARD_LCD_H_RES
  #define BOARD_LCD_H_RES BSP_LCD_H_RES
  #endif
  #ifndef BOARD_LCD_V_RES
  #define BOARD_LCD_V_RES BSP_LCD_V_RES
  #endif
#else
  // Default: 1.8" board (also when Kconfig not yet generated)
  #include "bsp/esp32_s3_touch_amoled_1_8.h"
  #ifndef BOARD_NAME
  #define BOARD_NAME "Waveshare ESP32-S3 AMOLED 1.8\" 368x448"
  #endif
  #ifndef BOARD_LCD_H_RES
  #define BOARD_LCD_H_RES BSP_LCD_H_RES
  #endif
  #ifndef BOARD_LCD_V_RES
  #define BOARD_LCD_V_RES BSP_LCD_V_RES
  #endif
#endif

// Unified log tag helper for which board was compiled
static inline const char* board_name(void) { return BOARD_NAME; }
