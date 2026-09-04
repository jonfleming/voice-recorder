#include "ui_manager.h"
#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui";

static lv_obj_t *scr = NULL;
static lv_obj_t *title_label = NULL;
static lv_obj_t *time_label = NULL;
static lv_obj_t *hint_label = NULL;
static lv_obj_t *wave_bar = NULL;
static lv_obj_t *wave_chart = NULL;
static lv_chart_series_t *wave_series = NULL;
static lv_obj_t *file_list = NULL;
static lv_obj_t *status_label = NULL;
#define WAVE_POINTS 40

static wav_file_info_t s_files[FS_MAX_FILES];
static size_t s_file_count = 0;
static int s_selected = -1;

// Resolution-adaptive sizing (410x502 vs 368x448)
#ifndef BSP_LCD_H_RES
#define BSP_LCD_H_RES 368
#endif
#ifndef BSP_LCD_V_RES
#define BSP_LCD_V_RES 448
#endif
#define UI_LIST_W   (BSP_LCD_H_RES - 28)   // 340 on 1.8", 382 on 2.06"
#define UI_LIST_H   (BSP_LCD_V_RES - 148)  // 300 on 1.8", 354 on 2.06"
#define UI_CHART_W  (BSP_LCD_H_RES - 48)   // 320 on 1.8", 362 on 2.06"
#define UI_BAR_W    (BSP_LCD_H_RES - 48)
#define UI_BTN_W    (BSP_LCD_H_RES - 28)
#define UI_LABEL_W  (BSP_LCD_H_RES - 48)
static file_selected_cb_t s_cb = NULL;
static volatile bool s_play_req = false;
static lv_obj_t *play_btn = NULL;
static int s_last_tap_idx = -1;
static int64_t s_last_tap_ms = 0;
static lv_obj_t *delete_overlay = NULL;
static lv_obj_t *delete_dialog = NULL;
static int s_delete_pending_idx = -1;
static volatile bool s_delete_confirmed = false;
static volatile int s_delete_confirmed_idx = -1;

static void clear_screen(void)
{
    if (scr) {
        // If delete dialog was open, clean its pointers (lv_obj_clean will delete it)
        delete_overlay = NULL;
        delete_dialog = NULL;
        s_delete_pending_idx = -1;
        // Do not clear s_delete_confirmed here - let app consume it
        lv_obj_clean(scr);
        title_label=time_label=hint_label=wave_bar=wave_chart=file_list=status_label=play_btn=NULL;
        wave_series = NULL;
        s_play_req = false;
    } else {
        scr = lv_scr_act();
    }
}

static void format_time(char *out, size_t len, uint32_t sec)
{
    snprintf(out, len, "%02u:%02u", (unsigned)(sec/60), (unsigned)(sec%60));
}

static void play_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_selected >= 0 && s_selected < (int)s_file_count) {
        s_play_req = true;
        ESP_LOGI(TAG, "On-screen Play requested for %d", s_selected);
    }
}

static void delete_dialog_close(void)
{
    if (delete_overlay) {
        lv_obj_del(delete_overlay);
        delete_overlay = NULL;
        delete_dialog = NULL;
    }
    s_delete_pending_idx = -1;
}

static void delete_confirm_cb(lv_event_t *e)
{
    (void)e;
    if (s_delete_pending_idx >= 0 && s_delete_pending_idx < (int)s_file_count) {
        s_delete_confirmed_idx = s_delete_pending_idx;
        s_delete_confirmed = true;
        ESP_LOGI(TAG, "Delete confirmed for %d %s", s_delete_confirmed_idx, s_files[s_delete_confirmed_idx].filename);
    }
    delete_dialog_close();
}

static void delete_cancel_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Delete cancelled");
    delete_dialog_close();
}

static void show_delete_dialog(int idx)
{
    if (idx < 0 || idx >= (int)s_file_count) return;
    if (delete_overlay) delete_dialog_close();
    if (!bsp_display_lock(200)) {
        ESP_LOGW(TAG, "show_delete_dialog: LVGL busy");
        return;
    }
    s_delete_pending_idx = idx;
    // Semi-transparent overlay — full screen per board resolution
    delete_overlay = lv_obj_create(scr);
    lv_obj_set_size(delete_overlay, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_align(delete_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(delete_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(delete_overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(delete_overlay, 0, 0);
    lv_obj_set_style_radius(delete_overlay, 0, 0);
    lv_obj_clear_flag(delete_overlay, LV_OBJ_FLAG_SCROLLABLE);
    // Dialog box
    delete_dialog = lv_obj_create(delete_overlay);
    lv_obj_set_size(delete_dialog, 300, 160);
    lv_obj_align(delete_dialog, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(delete_dialog, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_color(delete_dialog, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(delete_dialog, 1, 0);
    lv_obj_set_style_radius(delete_dialog, 12, 0);
    lv_obj_set_style_pad_all(delete_dialog, 12, 0);
    lv_obj_clear_flag(delete_dialog, LV_OBJ_FLAG_SCROLLABLE);
    // Make overlay clickable to not pass through, but allow closing via Cancel
    lv_obj_add_flag(delete_overlay, LV_OBJ_FLAG_CLICKABLE);
    // Message
    lv_obj_t *lbl = lv_label_create(delete_dialog);
    char msg[64];
    snprintf(msg, sizeof(msg), "Delete\n%s ?", s_files[idx].filename);
    lv_label_set_text(lbl, msg);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, 276);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 8);
    // Buttons container
    lv_obj_t *btn_row = lv_obj_create(delete_dialog);
    lv_obj_set_size(btn_row, 276, 50);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    // Cancel button
    lv_obj_t *cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, 120, 40);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    lv_obj_add_event_cb(cancel_btn, delete_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);
    lv_obj_set_style_text_color(cancel_lbl, lv_color_white(), 0);
    // Delete button (red)
    lv_obj_t *del_btn = lv_btn_create(btn_row);
    lv_obj_set_size(del_btn, 120, 40);
    lv_obj_set_style_bg_color(del_btn, lv_color_hex(0xcc2222), 0);
    lv_obj_set_style_radius(del_btn, 8, 0);
    lv_obj_add_event_cb(del_btn, delete_confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *del_lbl = lv_label_create(del_btn);
    lv_label_set_text(del_lbl, "Delete");
    lv_obj_center(del_lbl);
    lv_obj_set_style_text_color(del_lbl, lv_color_white(), 0);
    bsp_display_unlock();
}

static void long_press_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(target);
    ESP_LOGI(TAG, "Long-press on %d %s -> delete dialog", idx, s_files[idx].filename);
    // Also select it
    s_selected = idx;
    if (s_cb) s_cb(idx, &s_files[idx]);
    show_delete_dialog(idx);
}

static void list_event_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(target);
    int64_t now = esp_timer_get_time() / 1000;
    bool double_tap = (idx == s_last_tap_idx && (now - s_last_tap_ms) < 600);
    s_last_tap_idx = idx;
    s_last_tap_ms = now;
    s_selected = idx;
    ESP_LOGI(TAG, "Selected %d %s %s", idx, s_files[idx].filename, double_tap ? "(double-tap -> PLAY)" : "");
    uint32_t cnt = lv_obj_get_child_count(file_list);
    for (uint32_t i=0;i<cnt;i++) {
        lv_obj_t *child = lv_obj_get_child(file_list, i);
        if ((int)i == idx) lv_obj_set_style_bg_color(child, lv_color_hex(0x0066ff), 0);
        else lv_obj_set_style_bg_color(child, lv_color_hex(0x222222), 0);
    }
    if (play_btn) {
        if (s_selected >= 0) lv_obj_clear_flag(play_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_cb) s_cb(idx, &s_files[idx]);
    if (double_tap) {
        s_play_req = true;
    }
}

// Helper to create styled label
static lv_obj_t *make_label(lv_obj_t *parent, const char *txt, int y, int font_size)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, txt);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    return lbl;
}

esp_err_t ui_init(file_selected_cb_t cb)
{
    s_cb = cb;
    if (!bsp_display_lock(200)) {
        ESP_LOGE(TAG, "ui_init: failed to lock LVGL");
        return ESP_ERR_TIMEOUT;
    }
    scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    bsp_display_unlock();
    ESP_LOGI(TAG, "UI init done");
    return ESP_OK;
}

esp_err_t ui_show_idle(void)
{
    if (!bsp_display_lock(100)) {
        ESP_LOGW(TAG, "ui_show_idle: LVGL busy, skip");
        return ESP_ERR_TIMEOUT;
    }
    clear_screen();
    title_label = make_label(scr, "Voice Recorder", 10, 24);
    hint_label = lv_label_create(scr);
    lv_label_set_text(hint_label, "Press A to record\nFiles below:");
    lv_obj_align(hint_label, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER, 0);
    bsp_display_unlock();
    return ESP_OK;
}

esp_err_t ui_show_recording(uint32_t elapsed_sec, uint16_t rms)
{
    char tbuf[16];
    format_time(tbuf, sizeof(tbuf), elapsed_sec);
    if (!bsp_display_lock(100)) {
        ESP_LOGW(TAG, "ui_show_recording: LVGL busy, skip");
        return ESP_ERR_TIMEOUT;
    }
    bool need_create = (!title_label || !time_label || !lv_obj_is_valid(time_label) || !lv_obj_is_valid(title_label));
    if (need_create) {
        clear_screen();
        title_label = make_label(scr, "Recording...", 12, 24);
        lv_obj_set_style_text_color(title_label, lv_color_hex(0xff4444), 0);
        time_label = lv_label_create(scr);
        lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -62);
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
        // Scrolling waveform chart - 20-30 FPS RMS history (spec 5.1)
        wave_chart = lv_chart_create(scr);
        lv_obj_set_size(wave_chart, UI_CHART_W, 88);
        lv_obj_align(wave_chart, LV_ALIGN_CENTER, 0, 12);
        lv_obj_set_style_bg_color(wave_chart, lv_color_hex(0x111111), 0);
        lv_obj_set_style_border_color(wave_chart, lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(wave_chart, 1, 0);
        lv_obj_set_style_radius(wave_chart, 6, 0);
        lv_obj_set_style_pad_all(wave_chart, 4, 0);
        lv_chart_set_type(wave_chart, LV_CHART_TYPE_LINE);
        lv_chart_set_range(wave_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
        lv_chart_set_point_count(wave_chart, WAVE_POINTS);
        lv_chart_set_update_mode(wave_chart, LV_CHART_UPDATE_MODE_SHIFT);
        lv_chart_set_div_line_count(wave_chart, 4, 6);
        wave_series = lv_chart_add_series(wave_chart, lv_color_hex(0x00ff88), LV_CHART_AXIS_PRIMARY_Y);
        for (int i = 0; i < WAVE_POINTS; i++) lv_chart_set_next_value(wave_chart, wave_series, 0);
        // Current level bar below chart
        wave_bar = lv_bar_create(scr);
        lv_obj_set_size(wave_bar, UI_BAR_W, 14);
        lv_obj_align(wave_bar, LV_ALIGN_CENTER, 0, 78);
        lv_obj_set_style_bg_color(wave_bar, lv_color_hex(0x222222), 0);
        lv_obj_set_style_bg_color(wave_bar, lv_color_hex(0x00ff88), LV_PART_INDICATOR);
        lv_bar_set_range(wave_bar, 0, 1000);
        lv_bar_set_value(wave_bar, 0, LV_ANIM_OFF);
        hint_label = lv_label_create(scr);
        lv_label_set_text(hint_label, "Press A to stop  •  waveform RMS 20Hz");
        lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_14, 0);
    }
    if (time_label && lv_obj_is_valid(time_label)) {
        lv_label_set_text(time_label, tbuf);
    }
    if (wave_chart && wave_series && lv_obj_is_valid(wave_chart)) {
        lv_chart_set_next_value(wave_chart, wave_series, rms);
        lv_chart_refresh(wave_chart);
    }
    if (wave_bar && lv_obj_is_valid(wave_bar)) {
        lv_bar_set_value(wave_bar, rms, LV_ANIM_OFF);
    }
    bsp_display_unlock();
    return ESP_OK;
}

esp_err_t ui_show_file_browser(const wav_file_info_t *files, size_t count, int selected)
{
    if (!bsp_display_lock(200)) {
        ESP_LOGW(TAG, "ui_show_file_browser: LVGL busy, skip");
        return ESP_ERR_TIMEOUT;
    }
    clear_screen();
    // Title
    title_label = make_label(scr, "Files", 8, 20);
    status_label = lv_label_create(scr);
    if (count==0) {
        lv_label_set_text(status_label, "No recordings yet\nPress A to record");
        lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_color(status_label, lv_color_hex(0x888888),0);
        lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER,0);
        bsp_display_unlock();
        return ESP_OK;
    }
    char sub[32];
    snprintf(sub,sizeof(sub), "%u file(s) - tap to select", (unsigned)count);
    lv_label_set_text(status_label, sub);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xaaaaaa),0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14,0);

    // Create scrollable list (reduced height to leave room for PLAY button)
    file_list = lv_list_create(scr);
    lv_obj_set_size(file_list, UI_LIST_W, UI_LIST_H);
    lv_obj_align(file_list, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_color(file_list, lv_color_black(),0);
    lv_obj_set_style_pad_all(file_list, 4,0);

    // Cache
    s_file_count = count > FS_MAX_FILES ? FS_MAX_FILES : count;
    for (size_t i=0;i<s_file_count;i++) s_files[i]=files[i];
    s_selected = selected;

    for (size_t i=0;i<s_file_count;i++) {
        char row[48];
        // Show filename + size KB
        snprintf(row,sizeof(row), "%s  (%u KB)", files[i].filename, (unsigned)(files[i].size/1024));
        lv_obj_t *btn = lv_list_add_button(file_list, LV_SYMBOL_AUDIO, row);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, list_event_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(btn, long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
        // Row styling
        lv_obj_set_style_bg_color(btn, (int)i==selected ? lv_color_hex(0x0066ff) : lv_color_hex(0x222222), 0);
        lv_obj_set_style_text_color(btn, lv_color_white(),0);
        lv_obj_set_style_radius(btn, 6,0);
        // Make row ~40px as spec
        lv_obj_set_height(btn, 40);
    }
    lv_obj_set_scrollbar_mode(file_list, LV_SCROLLBAR_MODE_AUTO);

    // On-screen Play button fallback when physical Button B is disabled (touch INT conflict)
    play_btn = lv_btn_create(scr);
    lv_obj_set_size(play_btn, UI_BTN_W, 42);
    lv_obj_align(play_btn, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_bg_color(play_btn, lv_color_hex(0x00aa44), 0);
    lv_obj_set_style_radius(play_btn, 8, 0);
    lv_obj_add_event_cb(play_btn, play_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *play_lbl = lv_label_create(play_btn);
    lv_label_set_text(play_lbl, LV_SYMBOL_AUDIO "  PLAY selected");
    lv_obj_center(play_lbl);
    lv_obj_set_style_text_color(play_lbl, lv_color_white(), 0);
    if (selected < 0) lv_obj_add_flag(play_btn, LV_OBJ_FLAG_HIDDEN);

    hint_label = lv_label_create(scr);
    // Show hint that adapts to B availability - include long-press delete hint
#if CONFIG_BUTTON_B_GPIO < 0
    lv_label_set_text(hint_label, "A:Record  Tap Play  Long-press:Delete");
#else
    lv_label_set_text(hint_label, "A:Record  B:Play  Long-press:Delete");
#endif
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888),0);
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_14,0);

    bsp_display_unlock();
    return ESP_OK;
}

esp_err_t ui_show_playback(const char *filename, uint32_t elapsed_sec, uint32_t total_sec)
{
    char tbuf[32];
    char tot[16], el[16];
    format_time(el, sizeof(el), elapsed_sec);
    format_time(tot, sizeof(tot), total_sec);
    snprintf(tbuf,sizeof(tbuf), "%s / %s", el, tot);
    if (!bsp_display_lock(100)) {
        ESP_LOGW(TAG, "ui_show_playback: LVGL busy, skip");
        return ESP_ERR_TIMEOUT;
    }
    if (!title_label || !lv_obj_is_valid(title_label) || lv_obj_get_child_count(scr)==0) {
        clear_screen();
        title_label = make_label(scr, "Playing", 20, 24);
        lv_obj_set_style_text_color(title_label, lv_color_hex(0x44ff44),0);
        // Filename
        lv_obj_t *fn = lv_label_create(scr);
        lv_label_set_text(fn, filename ? filename : "");
        lv_obj_align(fn, LV_ALIGN_CENTER, 0, -30);
        lv_obj_set_style_text_color(fn, lv_color_white(),0);
        lv_label_set_long_mode(fn, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(fn, UI_LABEL_W);
        // store reference via time_label for reuse? We'll recreate each update
        time_label = lv_label_create(scr);
        lv_obj_align(time_label, LV_ALIGN_CENTER, 0, 20);
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_14,0);
        lv_obj_set_style_text_color(time_label, lv_color_white(),0);
        hint_label = lv_label_create(scr);
        lv_label_set_text(hint_label, "Playing...");
        lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID,0,-20);
        lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888),0);
        title_label = fn; // reuse variable? keep time_label separate
    }
    if (time_label) lv_label_set_text(time_label, tbuf);
    bsp_display_unlock();
    return ESP_OK;
}

int ui_get_selected_index(void) { return s_selected; }
bool ui_play_requested(void) {
    if (s_play_req) { s_play_req = false; return true; }
    return false;
}
bool ui_delete_requested(int *idx) {
    if (s_delete_confirmed) {
        s_delete_confirmed = false;
        if (idx) *idx = s_delete_confirmed_idx;
        s_delete_confirmed_idx = -1;
        return true;
    }
    return false;
}
bool ui_is_delete_dialog_open(void) {
    return delete_overlay != NULL && lv_obj_is_valid(delete_overlay);
}
