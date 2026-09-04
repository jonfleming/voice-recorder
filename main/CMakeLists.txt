idf_component_register(
    SRCS "app_main.c"
         "wav.c"
         "filesystem_manager.c"
         "audio_commons.c"
         "audio_recorder.c"
         "audio_player.c"
         "button_manager.c"
         "ui_manager.c"
         "state_machine.c"
         "power_manager.c"
    INCLUDE_DIRS "."
     REQUIRES esp32_s3_touch_amoled_1_8 esp32_s3_touch_amoled_2_06 esp_codec_dev esp_driver_gpio esp_driver_i2s esp_lcd esp_lcd_touch esp_lvgl_port lvgl sdmmc fatfs spiffs wpa_supplicant esp_timer
    PRIV_REQUIRES esp_timer
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format-truncation -Wno-error=format-overflow)
