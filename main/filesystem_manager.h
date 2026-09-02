#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define FS_MOUNT_POINT "/sdcard"
#define FS_MAX_FILES  128
#define FS_MAX_PATH   64
#define FS_MAX_NAME   32

typedef struct {
    char filename[FS_MAX_NAME];
    char path[FS_MAX_PATH];
    uint32_t size;
    uint32_t timestamp; // derived from filename or mtime
} wav_file_info_t;

esp_err_t fs_manager_init(void);
esp_err_t fs_manager_deinit(void);
bool      fs_manager_is_mounted(void);
esp_err_t fs_manager_list_wav(wav_file_info_t *out, size_t *count, size_t max_count);
esp_err_t fs_manager_get_free_kb(uint32_t *out_kb);
esp_err_t fs_manager_delete(const char *path);
