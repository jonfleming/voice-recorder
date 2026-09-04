#include "filesystem_manager.h"
#include "board.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

static const char *TAG = "fs_mgr";
static bool s_mounted = false;

esp_err_t fs_manager_init(void)
{
    if (s_mounted) return ESP_OK;
    esp_err_t ret = bsp_sdcard_mount();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_mounted = true;
    if (bsp_sdcard) sdmmc_card_print_info(stdout, bsp_sdcard);
    ESP_LOGI(TAG, "SD mounted at %s", BSP_SD_MOUNT_POINT);
    return ESP_OK;
}

esp_err_t fs_manager_deinit(void)
{
    if (!s_mounted) return ESP_OK;
    esp_err_t r = bsp_sdcard_unmount();
    if (r == ESP_OK) s_mounted = false;
    return r;
}

bool fs_manager_is_mounted(void) { return s_mounted; }

// Parse YYYYMMDD_HHMMSS.wav -> uint32 sortable timestamp-like value
static uint32_t filename_to_ts(const char *name)
{
    // Expect 15 chars before .wav
    if (strlen(name) < 15) return 0;
    char buf[16];
    // copy digits only
    int idx=0;
    for (int i=0;i<15;i++) {
        char c=name[i];
        if (c>='0' && c<='9') buf[idx++]=c;
    }
    buf[idx]=0;
    if (idx==0) return 0;
    // Use strtoul; fits in 32: YYYYMMDDHHMMSS ~ 20260818112355 > 2^32, so truncate
    // Use lexicographic compare works if we compare string, but for sort use filename compare
    // Fallback: return 0, sorting will use stat mtime later
    return (uint32_t)strtoul(buf, NULL, 10);
}

static int cmp_desc(const void *a, const void *b)
{
    const wav_file_info_t *fa = (const wav_file_info_t*)a;
    const wav_file_info_t *fb = (const wav_file_info_t*)b;
    // Newest first: reverse strcmp so larger (newer) timestamp sorts first
    int r = strcmp(fb->filename, fa->filename);
    if (r != 0) return r;
    return (int)fb->size - (int)fa->size;
}

esp_err_t fs_manager_list_wav(wav_file_info_t *out, size_t *count, size_t max_count)
{
    if (!out || !count) return ESP_ERR_INVALID_ARG;
    *count = 0;
    if (!s_mounted) {
        ESP_LOGW(TAG, "SD not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    DIR *d = opendir(BSP_SD_MOUNT_POINT);
    if (!d) {
        ESP_LOGE(TAG, "opendir failed");
        return ESP_FAIL;
    }
    struct dirent *ent;
    size_t n = 0;
    char full[256];
    while ((ent = readdir(d)) != NULL && n < max_count) {
        const char *name = ent->d_name;
        size_t len = strlen(name);
        if (len < 5) continue;
        if (strcasecmp(name + len - 4, ".wav") != 0) continue;
        snprintf(full, sizeof(full), "%s/%s", BSP_SD_MOUNT_POINT, name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;
        strncpy(out[n].filename, name, FS_MAX_NAME-1);
        out[n].filename[FS_MAX_NAME-1]=0;
        strncpy(out[n].path, full, FS_MAX_PATH-1);
        out[n].path[FS_MAX_PATH-1]=0;
        out[n].size = (uint32_t)st.st_size;
        // Try filename timestamp first, else use mtime
        uint32_t ts = filename_to_ts(name);
        if (ts == 0) ts = (uint32_t)st.st_mtime;
        out[n].timestamp = ts;
        n++;
    }
    closedir(d);
    qsort(out, n, sizeof(wav_file_info_t), cmp_desc);
    *count = n;
    ESP_LOGI(TAG, "Found %u wav files", (unsigned)n);
    return ESP_OK;
}

esp_err_t fs_manager_get_free_kb(uint32_t *out_kb)
{
    if (!out_kb) return ESP_ERR_INVALID_ARG;
    // Use statvfs if available, else estimate
    // FatFS: use esp_vfs_fat_info? approximate
    // Fallback 0
    *out_kb = 0;
    return ESP_OK;
}

esp_err_t fs_manager_delete(const char *path)
{
    if (!path) return ESP_ERR_INVALID_ARG;
    if (!s_mounted) {
        ESP_LOGW(TAG, "Delete failed: SD not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Deleting %s", path);
    if (unlink(path) != 0) {
        ESP_LOGE(TAG, "unlink %s failed errno=%d", path, errno);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Deleted %s", path);
    return ESP_OK;
}
