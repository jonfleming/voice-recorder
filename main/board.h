#pragma once
/*
 * board.h — include the active Waveshare BSP and re-export unified names.
 *
 * Board is selected by CMake BEFORE the component manager runs:
 *   idf.py build                                  → 1.8"  368x448
 *   idf.py -DWAVESHARE_AMOLED_2_06_BOARD=ON build → 2.06" 410x502
 *
 * Both official BSPs export the same bsp_* symbols, so only one may be
 * linked. Include bsp/esp-bsp.h (not a board-specific header) so the
 * compile uses whichever BSP CMake actually pulled in.
 *
 * Pin differences (owned by the BSP):
 *   1.8:  I2S 16/9/45/8/10/46  touch INT 21  RST NC    (CO5300)
 *   2.06: I2S 16/41/45/40/42/46 touch INT 38 RST 8/9   (SH8601/CO5300)
 *   2.06 mic is ES7210 stereo; 1.8 mic is ES8311 mono.
 */
#include "sdkconfig.h"
#include "bsp/esp-bsp.h"

#if defined(WAVESHARE_AMOLED_2_06_BOARD) && WAVESHARE_AMOLED_2_06_BOARD
  #define BOARD_NAME "Waveshare ESP32-S3 AMOLED 2.06\" 410x502"
  #define BOARD_AUDIO_CHANNELS 2
  #define BOARD_MIC_GAIN_DB    30.0f
  #define BOARD_SPK_VOLUME     100
  _Static_assert(BSP_LCD_H_RES == 410 && BSP_LCD_V_RES == 502,
                 "2.06 CMake flag is set but the 1.8 BSP headers were used");
#else
  #define BOARD_NAME "Waveshare ESP32-S3 AMOLED 1.8\" 368x448"
  #define BOARD_AUDIO_CHANNELS 1
  #define BOARD_MIC_GAIN_DB    30.0f
  #define BOARD_SPK_VOLUME     92
  _Static_assert(BSP_LCD_H_RES == 368 && BSP_LCD_V_RES == 448,
                 "1.8 build picked up the 2.06 BSP headers");
#endif

#ifndef BOARD_LCD_H_RES
#define BOARD_LCD_H_RES BSP_LCD_H_RES
#endif
#ifndef BOARD_LCD_V_RES
#define BOARD_LCD_V_RES BSP_LCD_V_RES
#endif

static inline const char* board_name(void) { return BOARD_NAME; }
