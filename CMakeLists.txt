cmake_minimum_required(VERSION 3.16)

# Pin the chip before project() so a missing/stale sdkconfig does not fall
# back to ESP-IDF's default (esp32) and drop the S3-only Waveshare BSPs.
if(NOT DEFINED IDF_TARGET)
  set(IDF_TARGET esp32s3)
endif()

# The 1.8 and 2.06 Waveshare BSPs export the same bsp_* symbols
# (bsp_display_start, bsp_i2c_init, ...). Linking both lets the linker pick
# the 1.8 objects: 368x448 window + GPIO21 touch INT on a 410x502 panel
# (green L leftover, dead touch). Exactly one BSP may be in the build.
#
# Board is chosen before project() (component manager runs there):
#   1. sdkconfig CONFIG_TARGET_BOARD_*  — menuconfig → Voice Recorder Config
#   2. -DWAVESHARE_AMOLED_2_06_BOARD=ON — used when sdkconfig does not exist yet
#      (also applies sdkconfig.defaults.2_06 for 32 MB flash etc.)
#
# After switching boards, fullclean so the old BSP drops out of the link.
option(WAVESHARE_AMOLED_2_06_BOARD "Build for Waveshare ESP32-S3 Touch AMOLED 2.06" OFF)

set(_VR_BOARD_2_06 ${WAVESHARE_AMOLED_2_06_BOARD})
if(EXISTS "${CMAKE_SOURCE_DIR}/sdkconfig")
  file(STRINGS "${CMAKE_SOURCE_DIR}/sdkconfig" _VR_SDK_2_06
       REGEX "^CONFIG_TARGET_BOARD_2_06=y")
  file(STRINGS "${CMAKE_SOURCE_DIR}/sdkconfig" _VR_SDK_1_8
       REGEX "^CONFIG_TARGET_BOARD_1_8=y")
  if(_VR_SDK_2_06)
    set(_VR_BOARD_2_06 ON)
  elseif(_VR_SDK_1_8)
    set(_VR_BOARD_2_06 OFF)
  endif()
endif()

if(_VR_BOARD_2_06)
  add_compile_definitions(WAVESHARE_AMOLED_2_06_BOARD=1)
  set(ENV{VOICE_RECORDER_BOARD} "waveshare_amoled_2_06")
  set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.2_06")
  message(STATUS "Voice Recorder: Waveshare AMOLED 2.06 (410x502, ES8311+ES7210)")
else()
  add_compile_definitions(WAVESHARE_AMOLED_2_06_BOARD=0)
  set(ENV{VOICE_RECORDER_BOARD} "waveshare_amoled_1_8")
  message(STATUS "Voice Recorder: Waveshare AMOLED 1.8 (368x448, ES8311)")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(voice-recorder)
