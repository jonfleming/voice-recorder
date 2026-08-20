/*
 * voicerec_config.h
 * =========================================================================
 * Single source of truth for pins, I2C addresses, audio parameters and
 * tuning knobs for the Waveshare ESP32-S3 Touch AMOLED 2.06" voice recorder.
 *
 * Pin numbers are derived from the Waveshare pinout table (see the project
 * root pinout document). Every driver component and the application #include
 * this header so the wiring lives in ONE place and can be changed for
 * revision differences without touching logic.
 *
 * The pinout is fixed by the Waveshare reference board:
 *   Display  : CO5300 AMOLED, QSPI, 410 x 502
 *   Touch    : FT3168, I2C
 *   PMU      : AXP2101, I2C
 *   RTC      : PCF85063, I2C
 *   IMU      : QMI8658C (present but not used in the functional spec)
 *   Codec out: ES8311, I2C + I2S  -> NS4150B amp -> speaker
 *   Codec in : ES7210, I2C + I2S  <- dual-mic
 *   TF card  : SPI 1-bit
 * =========================================================================
 */
#ifndef VOICEREC_CONFIG_H
#define VOICEREC_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== *
 *  Display (CO5300 QSPI AMOLED)                                      *
 * ================================================================== */
#define DISP_QSPI_DATA0_PIN       4      /* SIO0 */
#define DISP_QSPI_DATA1_PIN       5      /* SIO1 */
#define DISP_QSPI_DATA2_PIN       6      /* SIO2 */
#define DISP_QSPI_DATA3_PIN       7      /* SIO3 */
#define DISP_QSPI_SCLK_PIN       11      /* SCL  */
#define DISP_QSPI_CS_PIN         12      /* CS   */
#define DISP_RESET_PIN           8      /* RESET */
#define DISP_TE_PIN             13      /* Tearing-effect (optional sync) */

#define DISP_WIDTH              410
#define DISP_HEIGHT             502
#define DISP_H_OFFSET           0
#define DISP_V_OFFSET           0
#define DISP_MIRROR_X           0
#define DISP_MIRROR_Y           0
#define DISP_SWAP_xy            0      /* 0 = portrait 410x502 */

/* QSPI clock, MHz (CO5300 supports up to 62.5 MHz). */
#define DISP_QSPI_FREQ_MHZ      40
/* RGB565 double frame buffer (410x502 = 207 820 px x 2 B = 415 640 B). */
#define DISP_DOUBLE_BUFFER      1

/* ================================================================== *
 *  Touch (FT3168)                                                    *
 * ================================================================== */
#define TOUCH_INT_PIN          38      /* TP_INT (active low) */
#define TOUCH_RST_PIN          9      /* TP_RESET */
#define TOUCH_I2C_ADDR         0x38
#define TOUCH_MAX_TOUCHES      5
#define TOUCH_X_MAX            410
#define TOUCH_Y_MAX            502

/* ================================================================== *
 *  Shared I2C bus --------------------------------------------------  *
 *  One master bus (GPIO14/15) is shared by: FT3168, AXP2101, PCF85063,
 *  QMI8658C, ES8311, ES7210.                                          *
 * ================================================================== */
#define I2C_SDA_PIN             15
#define I2C_SCL_PIN             14
#define I2C_MASTER_FREQ_HZ     (400 * 1000)
#define I2C_TASK_STACK         2048

/* I2C slave addresses (7-bit). Adjust if board revision differs. */
#define PMU_I2C_ADDR           0x34      /* AXP2101       */
#define RTC_I2C_ADDR           0x51      /* PCF85063      */
#define IMU_I2C_ADDR           0x6B      /* QMI8658C      */
#define SPK_CODEC_I2C_ADDR     0x18      /* ES8311        */
#define MIC_CODEC_I2C_ADDR     0x0A      /* ES7210        */

/* ================================================================== *
 *  Audio / I2S -----------------------------------------------------  *
 *  Shared: MCLK, BCLK, LRCK. Separate data lanes for RX (mic) / TX (spk).
 * ========================================================================= */
#define AUDIO_MCLK_PIN         16
#define AUDIO_BCLK_PIN         41      /* I2S SCLK/bit clock */
#define AUDIO_LRCK_PIN         45      /* I2S WS/frame clock */
#define AUDIO_DIN_PIN          42      /* mic data OUT -> ESP32 (RX) */
#define AUDIO_DOUT_PIN         40      /* ESP32 TX -> speaker codec */
#define AMP_CTRL_PIN           46      /* NS4150B amplifier enable */

#define AUDIO_SAMPLE_RATE       16000
#define AUDIO_BITS              16
#define AUDIO_CHANNELS          1
#define AUDIO_I2S_SLOT_IDX      0      /* mono, left slot (0 = I2S_SLOT_M) */
/* I2S frame geometry (16 kHz @ 16 bits, 4x oversampled frame clock
 * -> LRCK = 16 kHz, BCLK = 16 kHz x 64). 64 bits/frame keeps the ES7210 /
 * ES8311 happy with mono slots. */
#define AUDIO_I2S_BCLK_DIV      64

/* I2S DMA buffer geometry (tuned for gapless capture/playback). */
#define AUDIO_DMA_DESCS         8
#define AUDIO_DMA_BUF_BYTES     (16 * 1024)

/* Recording / playback chunk size. 16 kHz * 16 bit / mono = 32 KB/s. A
 * 2048-sample chunk (~43 ms) comfortably sustains 512 B / 2 ms to SD. */
#define AUDIO_CHUNK_SAMPLES     2048
#define AUDIO_CHUNK_BYTES       (AUDIO_CHUNK_SAMPLES * AUDIO_BITS / 8 * AUDIO_CHANNELS)

/* ================================================================== *
 *  TF card (esp-sdspi, 1-bit SPI)                                   *
 * ================================================================== */
#define SD_MOSI_PIN             1
#define SD_MISO_PIN             3
#define SD_SCLK_PIN             2
#define SD_SPI_CS_PIN          17
#define SD_SPI_HZ             (40 * 1000 * 1000)

/* WAV files written to /records (kept separate from any system dirs). */
#define WAV_DIR               "/records"
#define WAV_FILE_EXT          ".wav"

/* ================================================================== *
 *  Buttons                                                         *
 *  This board exposes two user-pushable buttons:
 *    BTN_A = BOOT (GPIO0)  -> Recording start/stop
 *    BTN_B = PWR  (GPIO10) -> Play selected file
 *  Both read as pull-up inputs. NOTE: the PWR button is also wired to the
 *  AXP2101 power key (long-press power-off by the PMU); short-press is what
 *  we sample here. See README "Buttons" for usage caveats.
 * ================================================================== */
#define BTN_A_PIN             0       /* BOOT, active low, pull-up */
#define BTN_B_PIN            10       /* PWR,  active low, sampled as GPIO */
#define BTN_ACTIVE_LOW        1
#define BTN_DEBOUNCE_MS       30
#define BTN_HOLD_MS          400      /* press > hold => long press */

/* ================================================================== *
 *  UI tuning                                                        *
 * ================================================================== */
#define UI_REFRESH_HZ         30
#define UI_TIME_HZ            1
#define UI_WAVE_HZ            24
#define UI_ROWS_PER_SCREEN    8
#define UI_ROW_PX             52
#define UI_FONT_BODY          24
#define UI_FONT_TIME          48
#define UI_FONT_TITLE         26

/* FreeRTOS priorities / stacks (kept modest for RAM headroom). */
#define PRIO_AUDIO            5
#define PRIO_UI               4
#define PRIO_APP              3
#define PRIO_BUTTONS          3
#define PRIO_TOUCH            4

#ifdef __cplusplus
}
#endif

#endif /* VOICEREC_CONFIG_H */
