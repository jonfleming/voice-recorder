Project Folder: C:\Projects\voice-recorder
ESP-IDF Environment: C:\Users\jonfl\Dropbox\Tools\esp.cmd
---
# **Firmware Specification — Waveshare ESP32‑S3 AMOLED 1.8" Touch Display**
### **Project: Audio Recorder + File Browser + Playback**
### **Target Hardware: Waveshare ESP32‑S3 AMOLED 1.8" Touch Display**
### **Author: Jon**
### **Date: 2026‑09-01**
### **Amendment Date: 2026‑09-02 — Implementation amendments (see §10, Changelog)**

> **Amendment note:** Board shipped as 1.8" 368×448 (CO5300 QSPI) — spec header says 1.08" — firmware targets 1.8" 368×448 as per Waveshare `ESP32-S3-Touch-AMOLED-1.8` BSP v2.0.3. All pin mappings verified against BSP. Original §1.4 `Button B = GPIO pull-up` conflicts with hardware `GPIO21 = TOUCH_INT` and `AXP2101 PWRON`; amended to `Button A = BOOT GPIO0`, `Button B = PWR via AXP2101` for display toggle, playback via touch (see §2.3, §6.5, §6.8). Added waveform, delete confirmation, and robustness fixes per build `0xb4a20` — no conflict with functional requirements, see §10.

---
## **1. Hardware Overview**
### **1.1 MCU**
- ESP32‑S3 (dual‑core Xtensa LX7)
- Built‑in USB‑OTG (native USB 19/20) + UART0 43/44 for console
- Built‑in LCD interface (QSPI for CO5300 on this board)
- 8 MB Octal PSRAM, 16 MB Flash, `CONFIG_SPIRAM_MODE_OCT`
- Built‑in touch controller — **FT5x06 @0x38 (original) or CST816S @0x15 (V2)**, auto-detected via `bsp_i2c_device_probe()` with 5× retry + bus recovery
### **1.2 Display**
- 1.8" AMOLED (spec header 1.08" is 1.8" on shipped BSP)
- Resolution: **368 x 448** (Waveshare spec)
- Interface: **QSPI** via `SPI2_HOST`: `CS 12, PCLK 11, DATA0 4, DATA1 5, DATA2 6, DATA3 7` (`BSP_LCD_*`), `CO5300` driver `2.1.0`, brightness via DCS `0x51` (`bsp_display_backlight_on/off`)
- Touch panel: Capacitive, I²C `SDA 15/SCL 14` `I2C_NUM 1` `400kHz` `glitch_ignore_cnt 7` `internal_pullup true`, `INT 21` (`BSP_LCD_TOUCH_INT`), `RST NC`
### **1.3 Audio**
- ES8311 codec, onboard analog microphone (MIC1 → `DIN 10`) and speaker amp (`DOUT 8`, `BCLK 9`, `MCLK 16`, `LRCK 45`, `PA 46` active-high), `addr 0x18`, `I2S_NUM 1` `16 kHz mono 16-bit` (`I2S_STD_PHILIP_SLOT_MONO`, `mclk_multiple 256`), `pa_voltage 5.0 / codec_dac_voltage 3.3`. Playback vol `92/100` (V2 needs `90`), capture gain `30 dB` (max 42 dB), unmuted after `open()`.
### **1.4 Buttons (Amended — see §10)**
- **Button A** — `BOOT` side button, `GPIO0` input pull-up, active-low, debounced `50 ms` (`button_manager`, `CONFIG_BUTTON_A_GPIO=0`, task stack `4096`). Short press: `Idle→Record` / `Record→Stop`.
- **Button B** — `PWR` side button, **not GPIO21** (`GPIO21 = TOUCH_INT` — `BUTTON_B_GPIO=-1` by default to avoid conflict). Routed to `AXP2101` `PWRON` (I2C `0x34` on same bus `14/15`). Short press (`PKEY_SHORT_IRQ` `INTSTS2 bit3`) toggles display `ON/OFF` to save battery (`power_manager` polls `0x48/49/4A` every `120 ms`, debounced `400 ms`, `s_display_on` flag). Long-press `>6s` still hard power-off via PMU. If a free GPIO is wired, `CONFIG_BUTTON_B_GPIO` can be set to that pin and it resumes playback trigger.
- *Implementation note:* Original spec `Button B = GPIO B short → play` is retained as **touch fallback** (on-screen `PLAY` + double-tap) when `B` is `-1`; if `B` is mapped to a free GPIO, `B` again triggers playback per spec.
### **1.5 Storage**
- MicroSD via **SDMMC 1-bit** (`CLK 2, CMD 1, D0 3`, `SDMMC_HOST_DEFAULT`, `BSP_SD_*`), `BSP_SD_MOUNT_POINT "/sdcard"`, `FAT32` with `CONFIG_FATFS_LFN_HEAP=y` (long filenames `YYYYMMDD_HHMMSS.wav` require LFN; warning silenced). `allocation_unit_size 16 KiB`.
- **PMU:** `AXP2101` `0x34` on I2C `14/15` (`100 kHz` for PMU), IRQ `INTEN2/INTSTS2` for `PKEY`.
### **1.6 Display Power (Added)**
- `AXP2101` + `BSP` brightness handles power. `PWR` short toggles `bsp_display_backlight_off/on` (AMOLED `0%` ≈ off, saves ~30-50 mA); panel `disp_on_off` remains as per BSP init. No sleep beyond backlight.

---
# **2. Functional Requirements**
## **2.1 Recording**
### **Trigger**
- **Button A short press**
  - If idle / browser → start recording (→ `RECORDING` state)
  - If recording → stop recording (→ `FILE BROWSER`, newest file selected at index 0)
  - Debounce `50 ms`, stack `4096`, `vApplicationStackOverflowHook` fixed
### **Recording Format**
- PCM WAV
- 16‑bit
- Mono
- 16 kHz sample rate
### **Filename Format**
```
YYYYMMDD_HHMMSS.wav
```
Example:
```
20260818_112355.wav
```
*Collision suffix `_XX` if same second; `19700101_HHMMSS` fallback from `esp_timer` when RTC/NTP not set. Requires `FATFS_LFN_HEAP`.*

### **Recording Behavior**
- Create new file on SD card, write placeholder `wav_header_write(0)`, stream `512 B` I2S chunks from `esp_codec_dev_read()` (16 kHz → ~16 ms per chunk) to `fwrite()` + `fflush()` every `4 KiB`, update header `wav_header_update()` on stop.
- I2S open tracked via `s_mic_open` to avoid `i2s_channel_disable: not enabled` on double-open; `set_in_gain 30 dB`, `set_in_mute(false)`.
- Display (see §5.1):
  - “Recording…”
  - Elapsed time (mm:ss) at `20 Hz` (`ui_show_recording()` every `50 ms` with `bsp_display_lock(100)` checked)
  - **Implemented:** waveform visualization (RMS+peak, see below)
  - Remaining battery level — *not implemented* (room for `AXP2101` `getBattVoltage()`)

### **Waveform Requirements — Implemented (§5.1)**
- Horizontal scrolling line chart `320×88` (`LV_CHART_TYPE_LINE`, `SHIFT` mode, `40` points, `Y 0..1000`) + instant bar `320×14` below it, dark `0x111111/0x222222` bg, green `0x00ff88` indicator.
- Update at **20 FPS** (every `50 ms` from `app_main` `last_rec_update`), `bsp_display_lock(100)` skipped if busy to avoid `task_wdt`.
- Use `RMS + peak` (`peak*0.7 + rms*0.3`, `1.4×` gain, `*1000/4000` → `4000` peak = full-scale, more sensitive than `32768`), `compute_level()` in `audio_recorder.c`.

---

## **2.2 File Browser**
### **Trigger**
- When recording stops, automatically transition to File Browser screen (with optional `PWR` display-off still in browser but hidden)
### **Features**
- Read directory `BSP_SD_MOUNT_POINT` (`/sdcard`) on SD card
- List all `.wav` files
- Sort by newest first (descending timestamp = reverse `strcmp(filename)` + size tie-break, `qsort`)
- Display:
  - Scrollable `lv_list` `340×300` (reduced from `340` to leave room for `PLAY`), black bg, `40 px` rows, `LV_SYMBOL_AUDIO` + `filename (KB)`, scrollbar `AUTO`
  - Filename + size KB
  - Highlight selected row `0x0066ff` vs `0x222222`
- Touch interaction:
  - Vertical scrolling (LVGL `SCROLLBAR_MODE_AUTO`, drag)
  - Tap to select a file (highlights, `s_selected` synced to `app_main`)
  - Highlight selected file
  - **Added:** On-screen `PLAY selected` button `340×42` (`0x00aa44`) at `y -28`, hidden when `selected<0`, shown after selection
  - **Added:** Double-tap same file within `600 ms` → `PLAY` (`ui_play_requested()` + `list_event_cb` double-tap detection via `esp_timer`)
  - **Added:** **Long-press** (`LV_EVENT_LONG_PRESSED` `~400 ms`) on a row → confirmation dialog `Delete / Cancel` (see §5.2)
- Touch robustness: `bsp_touch_new()` 5× retry with `bsp_i2c_deinit/init` + `150+50*attempt ms` + `glitch_ignore_cnt` + `internal_pullup`; if still not found, `bsp_display_start` continues without touch (`Display without touch - button/serial only`), UI remains button-accessible (A for record, PWR for display). I2C `i2c.master` logs suppressed during probe as in stock demo.

### **Scrolling Requirements**
- Kinetic scrolling (LVGL default)
- Scroll bar (AUTO)
- Touch gestures:
  - Drag up/down → scroll list
  - Tap → select item
  - Long-press → `Delete ?` dialog
  - Double-tap → play (fallback when `B` disabled)

---

## **2.3 Playback (Amended)**
### **Trigger — Amended**
- **Original spec:** `Button B short press` → if file selected → play; else ignore.
- **Amended (hardware reality + power-save):**
  - If `CONFIG_BUTTON_B_GPIO >=0` (free GPIO wired), `B` short press still triggers playback per original spec (`button_b_was_pressed()` in `FILE_BROWSER`).
  - **Default `BUTTON_B_GPIO=-1`:** `B` is `PWR` → **toggles display ON/OFF** (`power_manager` `AXP2101` `PKEY_SHORT_IRQ`), not playback. Playback is via **touch**: on-screen `PLAY` button or double-tap selected file (`ui_play_requested()`). `app_main` merges `b_press = button_b_was_pressed() || ui_play_requested()`.
  - User can re-enable `B` for playback by setting `CONFIG_BUTTON_B_GPIO` to any free exposed pad (e.g. `38`) and wiring a button to `GND`.

### **Playback Format**
- WAV PCM 16‑bit mono 16 kHz (parsed via `wav_header_parse()`, handles extra `fmt` chunks, checks `RIFF/WAVE`/`audio_format 1`)
### **Playback Behavior**
- Open selected file (`fopen` `rb`), `fseek` to `data_start`, `esp_codec_dev_open(speaker, hdr.sample_rate)` with `mclk_multiple 256`, `set_out_mute(false)`, `set_out_vol(92)`, 30 ms PA settle, then stream `1024 B` chunks via `esp_codec_dev_write()`; track `s_played_bytes` vs `s_total_bytes`.
- Close with `esp_codec_dev_close()` tracked via `s_spk_open` to avoid disable errors; `free(path)` for `strdup` copy.
- Display (§5.3):
  - “Playing: \<filename\>” (scrolling if long)
  - Elapsed `mm:ss / mm:ss` every `200 ms`
- When playback ends (`!player_is_playing()` or `s_played_bytes >= total`): `close` → refresh file list → return to File Browser
- **Interruption:** `A` or `B` (or `PLAY` while playing) stops playback via `player_stop()` then returns to browser.

---

## **2.4 File Deletion (Added)**
- **Trigger:** Long-press (`LONG_PRESSED`) on a file row in File Browser → modal confirmation dialog.
- **Dialog:** Semi-transparent overlay `368×448` `50% black` + centered `300×160` box `0x1a1a1a` with `Delete\n<filename> ?` (wrap) and two buttons: `Cancel` (grey `0x333333` `120×40`) and `Delete` (red `0xcc2222`). `bsp_display_lock(200)` for creation. `Cancel` just closes; `Delete` sets `s_delete_confirmed`.
- **Action:** `app_main` polls `ui_delete_requested(&del_idx)` in `FILE_BROWSER`; if confirmed, `player_stop()` if playing, `fs_manager_delete(path)` (`unlink()`), `refresh_file_list()`, `ui_show_file_browser()` (selection clamped). While dialog open, other `A/B` actions ignored.
- **Interfaces:** `fs_manager_delete(path)` (`filesystem_manager.h`), `ui_delete_requested()`, `ui_is_delete_dialog_open()`.

## **2.5 Display Power Save (Added)**
- **Trigger:** `PWR` (Button B) short press (`AXP2101` `INTSTS2 bit3`) at any time.
- **Behavior:** `power_manager_toggle_display()` flips `s_display_on`; `OFF` → `bsp_display_backlight_off()` (DCS `0x51` `0`), `ON` → `bsp_display_backlight_on()` (`255`). Log `Display OFF/ON (PWR toggle)`. State machine stays in current `FILE_BROWSER/RECORDING/PLAYBACK` (UI hidden when off, but still runs). Long-press `>6s` still hard power-off via `AXP2101` hardware.

---
# **3. System Architecture**
## **3.1 Main Components**
1. **UI Manager** (`ui_manager.*` — LVGL 9.5, `bsp_display_lock` checked)
2. **Audio Recorder** (`audio_recorder.*`)
3. **Audio Player** (`audio_player.*`)
4. **Filesystem Manager** (`filesystem_manager.*`)
5. **Touch Input Manager** (via `bsp_touch_new` + LVGL `lvgl_port_add_touch`)
6. **Button Input Manager** (`button_manager.*` for `A`; `B` via `power_manager` when `-1`)
7. **State Machine** (`state_machine.*`)
8. **Power Manager** (`power_manager.*` — *added*, AXP2101 `0x34` polling, display toggle)

---

# **4. State Machine**
```
+------------------+
|      IDLE        |
+------------------+
        |
        | Button A
        v
+------------------+
|    RECORDING     |  --PWR toggle display ON/OFF (no state change)-->
+------------------+
        |
        | Button A
        v
+------------------+
|   FILE BROWSER   | --PWR toggle display ON/OFF-->
+------------------+
   |           |\
   | Touch     | B (if GPIO>=0) / PLAY btn / double-tap
   v           v \
 Select file   Play file \
                |    \
                v     v (long-press → Delete? dialog → if Delete → stay in FILE_BROWSER)
+------------------+
|     PLAYBACK     | --PWR toggle display ON/OFF-->
+------------------+
        |
        | Playback ends / A/B stop
        v
+------------------+
|   FILE BROWSER   |
+------------------+
```
* `PWR` short is orthogonal — does not change `STATE_*`, only `power_manager_set_display()`. `Delete` is a modal within `FILE_BROWSER` (no new state).*

---

# **5. UI Specification**
## **5.1 Recording Screen (Amended)**
### **Layout**
- Top: “Recording…” (red `0xff4444`, `y 12`)
- Middle: elapsed time `mm:ss` large (`montserrat_14`, center `y -62`)
- Center: **scrolling waveform chart** `320×88` (`0x111111` bg, `0x333333` border, `WAVE_POINTS 40`, green line) + **instant bar** `320×14` (`0x222222` bg, green indicator) — replaces single bar
- Bottom: `Press A to stop  •  waveform RMS 20Hz` + hint, background black/dark
### **Update Rate**
- Elapsed time: 20 Hz (every 50 ms, skipped if LVGL busy)
- Waveform: 20 Hz (same `ui_show_recording(sec, rms)`), `LEVEL 0..1000` via `compute_level()` (`peak*0.7+rms*0.3` `1.4×` `*1000/4000`)

---

## **5.2 File Browser Screen (Amended)**
### **Layout**
- Title: “Files” (`y 8`)
- Subtitle: `"<N> file(s) - tap to select"` + dynamic hint `A:Record Tap Play Long-press:Delete` (or `B:Play` if `B>=0`)
- Scrollable list `340×300` at `y 50`, each row `40 px`, `LV_SYMBOL_AUDIO`
- Highlight selected row `0x0066ff` vs `0x222222`
- **Play button** `340×42` green `0x00aa44` at `y -28`, hidden when `selected<0`
- Hint `y -6`
### **Delete Confirmation Dialog (Added)**
- Overlay `368×448` `50% black`, centered `300×160` `0x1a1a1a` `radius 12`, border `0x333333`
- Wrapped label `Delete\n<filename> ?` (white, centered)
- Row `276×50` `FLEX_ROW SPACE_EVENLY`: `Cancel` `120×40` grey + `Delete` `120×40` red, both `radius 8`
### **Touch gestures:**
- Drag to scroll
- Tap to select (highlights, shows `PLAY`)
- **Double-tap** same file `<600 ms` → play
- **Long-press** `~400 ms` → Delete dialog → `Delete` → `unlink()` → refresh; `Cancel` → close

---

## **5.3 Playback Screen**
### **Layout**
- Top: “Playing” (green `0x44ff44`)
- Middle: filename (scrolling circular, `320` wide)
- Bottom: elapsed `mm:ss / mm:ss` (`montserrat_14`) + `Playing...` hint
- `PWR` still toggles display even during playback
---

# **6. Module Specifications**
## **6.1 Audio Recorder Module (Amended)**
### **Responsibilities**
- Initialize I2S microphone at 16 kHz (via `audio_commons` `bsp_audio_init(16k mono)`)
- Create WAV header `wav_header_write(0)` and finalize `wav_header_update(size)` on stop
- Stream `512 B` (`256` samples) chunks via `esp_codec_dev_read()` in `rec_task` (`4096` stack, `prio 6`) to SD
- Track elapsed via `esp_timer`, `s_data_bytes`
- Provide level via `compute_level()` (RMS+peak, `0..1000`) at 20 Hz
- Handle `s_mic_open` flag to avoid `i2s_channel_disable` errors
### **Interfaces — Amended**
- `recorder.init()` (no longer leaves open)
- `recorder.start()` (close if open → open `16/1/256` → `set_in_gain 30.0f` + `set_in_mute(false)` → create task)
- `recorder.stop()` (signal, `200 ms` wait, update header, `close` if `s_mic_open`)
- `recorder.getElapsedTime()` (via `WAV_BYTE_RATE`)
- `recorder.getWaveformLevel()` → `recorder_get_rms_level()` (now peak-weighted, `0..1000`)
- `recorder.getCurrentPath()`

---

## **6.2 Audio Player Module (Amended)**
### **Responsibilities**
- Open WAV file, `wav_header_parse()` (handles extra `fmt` >16), check `RIFF/WAVE`/`PCM 1`
- Stream via `esp_codec_dev_write()` `1024 B` chunks to I2S speaker at file `sample_rate` (warn if !=16000)
- Track `s_played_bytes` vs `s_total_bytes`, elapsed via `WAV_BYTE_RATE`
- Manage `s_spk_open` flag to avoid double-close errors, `free(path)` for `strdup`
### **Interfaces — Amended**
- `player.init()`
- `player.play(filename)` (create `play_task` `4096`, `s_playing=true`)
- `player.stop()` (signal, `200 ms`, `close` if `s_spk_open`)
- `player.getElapsedTime()` / `getTotalTime()`
- `player.isPlaying()`
- `player.getCurrentFile()`
- *Volume:* `audio_common_set_volume(92)` + `set_out_mute(false)` after open + `30 ms` PA settle (V2 needs `90`, original `70`; `92` works for both, see §8)

---

## **6.3 Filesystem Manager (Amended)**
### **Responsibilities**
- Initialize SD card `bsp_sdcard_mount()` (`SDMMC 1-bit`, `20 MHz`), print info, `s_mounted` flag, retry on removal every `2s`
- List files `opendir("/sdcard")`, filter `.wav` case-insensitive, `stat` + `S_ISREG`, store `filename/path/size/timestamp`, sort descending via `qsort` reverse `strcmp(filename)` (newest timestamp first)
- Provide metadata + free space stub
- **Added:** `delete` via `unlink()` (**requires `FATFS_LFN_HEAP`**)
### **Interfaces — Amended**
- `fs.listFiles(extension=".wav")` → `fs_manager_list_wav(out, count, max)`
- `fs.open/read/write/close` via stdio `fopen/fwrite`
- `fs.delete(path)` → `fs_manager_delete(path)` (new, `errno` logged)
- `fs.isMounted()`, `fs.getFreeKb()`

---

## **6.4 Touch Input Manager**
### **Responsibilities**
- Read touch coordinates via `bsp_touch_new()` + `lvgl_port_add_touch()`
- Detect gestures: Tap, Drag, Scroll, **Long-press** (`LONG_PRESSED`), **Double-tap** (600 ms window in `ui_manager`)
- Robust init: 5× retry with `bsp_i2c_deinit/init` + `150+50*attempt ms` + `glitch 7` + `pullup true`, suppressed `i2c.master` logs during probe, continues without touch if still not found (`NULL` → `Display without touch`)
### **Interfaces**
- `touch.getEvent()` (via LVGL `lv_event_get_target()` + `user_data` index)
- `touch.getPosition()` (via `esp_lcd_touch`)

---

## **6.5 Button Input Manager (Amended)**
### **Responsibilities**
- Debounce buttons `50 ms` (`esp_timer` `ms`), active-low pull-up, task `btn_poll` `4096` stack (was `2048` → overflow `panel_io_i2c_register_event_callbacks`), `vTaskDelay 10 ms`
- **Button A:** `GPIO0` (`BOOT`) only (`CONFIG_BUTTON_A_GPIO=0`)
- **Button B:** **disabled by default** (`CONFIG_BUTTON_B_GPIO=-1`) because `GPIO21` = `BSP_LCD_TOUCH_INT` (conflict → phantom `B pressed`). If `>=0` and `!=21`, configures as input pull-up; if `21` logs `conflicts - disabling` and sets `NC`. Polling skips when disabled (`b_enabled` flag). Playback via `B` only when mapped to a free exposed pad (e.g. `38`).
### **Interfaces — Amended**
- `buttonA.wasPressed()` (consumes edge)
- `buttonB.wasPressed()` (consumes, or `false` when disabled → playback via `ui_play_requested()`)
- `buttonA.isHeld()`, `buttonB.isHeld()`
- `button_manager_init()` logs `Button A GPIO 0 pullup`, `Button B disabled - playback via touch UI`

---

## **6.6 UI Manager (Amended)**
### **Responsibilities**
- Render screens `bsp_display_lock(100/200)` checked (skip if busy to avoid `task_wdt`), `clear_screen()` resets all pointers including `delete_overlay`
- Maintain transitions (`ui_showRecordingScreen` etc.) + waveform chart/bar
- Handle touch events: tap, double-tap, long-press → delete dialog
- Handle button events (A/B via `app_main` polling)
- **Added:** Delete confirmation dialog handling + `PLAY` button
### **Interfaces — Amended**
- `ui.init(cb)` (`file_selected_cb_t`)
- `ui.showRecordingScreen(sec, rms)` (now chart + bar, `WAVE_POINTS 40`)
- `ui.showFileBrowser(files, count, selected)` (now `300` height + `PLAY` + hint)
- `ui.showPlaybackScreen(filename, sec, total)`
- `ui.showIdle()` (unused, kept)
- `ui.getSelectedIndex()`, `ui.playRequested()`, **`ui.deleteRequested(idx)`**, **`ui.isDeleteDialogOpen()`**
- Internal: `show_delete_dialog(idx)`, `delete_confirm_cb`, `delete_cancel_cb`, `play_btn_cb`, `long_press_cb`, `list_event_cb` (now handles highlight + `s_play_req` on double-tap + shows `PLAY`)

---

## **6.7 Power Manager (Added)**
### **Responsibilities**
- Handle `PWR` (Button B) via `AXP2101` `0x34` on `I2C 14/15` (`100 kHz` for PMU): probe `i2c_master_probe`, add `i2c_master_dev_handle` for `0x34`, enable `INTEN2.PKEY_SHORT_EN` (`0x41 bit3` = `_BV(11)`), poll `INTSTS1/2/3` (`0x48/49/4A`) every `120 ms` in `pwr_mon` task `3072@4`, debounce `400 ms`.
- Toggle display `ON/OFF` to save battery: `power_manager_set_display(on)` → `bsp_display_backlight_on/off()` (`0x51` `255/0`), `s_display_on` flag. Orthogonal to `STATE_*`.
- Fallback: if `0x34` not found, logs `AXP2101 not found - toggle disabled` and continues.
### **Interfaces (New)**
- `power_manager.init()` (call after `bsp_display_start()`, uses `bsp_i2c_get_handle()`)
- `power_manager.isDisplayOn()`
- `power_manager.setDisplay(bool on)`
- `power_manager.toggleDisplay()`

---

## **6.8 State Machine (Amended)**
- Added orthogonal `PWR` display toggle (no state change) + modal `DELETE` within `FILE_BROWSER` (see §4 diagram).

---

# **7. Performance Requirements**
### **Recording**
- Must not drop audio samples — `rec_task` `prio 6` blocks on `esp_codec_dev_read()` (~16 ms per `512 B` @16k), `fflush` every `4 KiB`, `WAV_BYTE_RATE 32000`.
- SD write must sustain `512 B` every `16 ms` (was `2 ms` spec is for 32 kHz stereo; now `16 ms` for 16k mono, still well within `20 MHz` 1-bit).
### **UI**
- Must maintain 30 FPS minimum — LVGL chart 20 Hz, browser list `200 ms` lock timeout, `30 FPS` via `lvgl_port` `SW rotate`.
- Touch latency < 20 ms — `10 ms` poll + `b_enabled` check.
- Display toggle latency < 400 ms debounce.
### **Playback**
- Must stream without gaps — `1024 B` chunks, `30 ms` PA settle, `i2s_channel` already enabled via `bsp_audio_init(16k)`.
### **Power**
- Display off saves ~30-50 mA (AMOLED `0%`); `PWR` poll `120 ms` negligible.

---

# **8. Non‑Functional Requirements**
### **Reliability**
- Handle SD removal gracefully — `fs_manager_is_mounted()` polled every `2s` (`now_ms %2000`), `refresh_file_list()` + `ui_show_file_browser()` if in browser; `DELETE` checks `s_mounted`.
- Handle corrupted files gracefully — `wav_header_parse()` checks `RIFF/WAVE/PCM 1` and hunts `data` chunk, returns `ESP_ERR_NOT_FOUND/NOT_SUPPORTED`; browser skips `stat` failures, playback logs `wav parse failed` and returns to browser without reboot. `unlink` errors logged with `errno`.
- Handle touch not found — 5× retry with bus recovery, then `continue without touch` (see §6.4), UI remains via `A` + `PWR`.
- Handle `i2s_channel_disable` double-close — tracked via `s_mic_open/s_spk_open` flags.
- Handle `btn_poll` stack overflow — increased to `4096`.
### **Maintainability**
- Modular architecture — now `8` modules including `power_manager`
- Clear separation UI/audio/logic — `bsp_display_lock` checked before any `lv_*`, `audio_commons` shared `16 k` init must be first call (otherwise BSP defaults `22k`)
- `target_compile_options -Wno-error=format-truncation` for `"%s/%s"` `64→256` paths
### **Extensibility**
- Future features:
  - Bluetooth audio
  - Wi‑Fi upload
  - Waveform zoom (chart `WAVE_POINTS` configurable)
  - Battery level via `AXP2101` `getBattVoltage()` (already measured, not yet shown)

---

# **9. Deliverables for Developer**
1. Full firmware source code (`main/*.c/h`, `CMakeLists.txt`, `sdkconfig.defaults`, `partitions.csv`)
2. Build instructions `BUILD.md` (ESP‑IDF v5.5.5, `C:\Users\jonfl\Dropbox\Tools\esp.cmd` → `idf.py build/flash monitor`)
3. Pinout documentation `PINOUT.md` (verified `BSP_LCD_*` `BSP_I2S_*` `BSP_SD_*` `BSP_I2C_*` + `AXP2101 0x34`)
4. UI assets — built-in `montserrat_14` + `LV_SYMBOL_AUDIO` icons, dark theme, chart green `0x00ff88`
5. Test plan `TEST_PLAN.md` (+ manual steps for record/browser/play/delete/PWR toggle)
6. Flashable binary `build/voice-recorder.bin` `0xb4a20` (`82% free`), `build/bootloader.bin`, `build/partition-table.bin`, `@flash_args` (as per `BUILD.md`)

---
- Use ESP‑IDF v5.5.5
- Waveshare Arduino Libraries — *not used in ESP‑IDF build*; analogous `waveshare/esp32_s3_touch_amoled_1_8 2.0.3` + `espressif/esp_codec_dev 1.5.11` + `lvgl/lvgl 9.5.0` + `esp_lvgl_port 2.9.0` via `idf_component.yml` (Arduino `DriveBus/GFX/lv_conf` for reference only)
	- Arduino_DriveBus
	- Arduino_GFX
	- lvgl
	- lv_conf.h
	- Mylibrary
	- SensorLib
	- XPowersLib (used as reference for `AXP2101` `0x34` `PKEY` via direct I2C, not Arduino)
---
# **10. Changelog / Implementation Amendments (Added)**

| Date | Area | Original Spec | Amended Implementation | Reason |
|------|------|---------------|------------------------|--------|
| 2026-09-02 | Hardware §1.2/1.4 | `Button B = GPIO pull-up`, display 1.8" | `Button A=BOOT GPIO0`, `Button B=PWR via AXP2101 0x34` for **display toggle**, `BUTTON_B_GPIO=-1`, display `1.8" 368×448 CO5300 QSPI`, `I2C 15/14 400k pullup+glitch` | `GPIO21` is `TOUCH_INT` → phantom presses + `Touch not found`; PWR documented as customizable. Playback moved to touch. |
| 2026-09-02 | Storage §1.5 | `MicroSD via SPI or SDMMC` | `SDMMC 1-bit 2/1/3` `FATFS_LFN_HEAP=y` (was `NONE` → `file.wav` truncated to 8.3) | `YYYYMMDD_HHMMSS.wav` 19 chars needs LFN; BSP warning silenced |
| 2026-09-02 | Recording §2.1 | `waveform nice-to-have` | Implemented scrolling chart `40 pts` + bar at `20 Hz` `RMS+peak` `0..1000` | Spec optional → now required for level indication; previous flat-line due to `*1000/32768` → now `*1000/4000` `1.4×` |
| 2026-09-02 | File Browser §2.2 | `tap to select` only | Added `PLAY 340×42` button, **double-tap 600 ms → play**, **long-press 400 ms → Delete? dialog** `Cancel/Delete` | `B` disabled → need touch playback; user requested delete |
| 2026-09-02 | Playback §2.3 | `B short → play` | `B` (if remapped) **or** `PLAY` **or** double-tap → play; `A`/`B` can stop playback | Hardware reality |
| 2026-09-02 | Filesystem §6.3 | `list/sort` only | Added `fs_manager_delete(path)` (`unlink`) | Delete feature |
| 2026-09-02 | Touch §6.4 | Simple probe | 5× retry with `bsp_i2c_deinit/init` + `150+50*attempt ms`, `continue without touch` | Monitor plug-in caused `Touch not found` abort → now graceful |
| 2026-09-02 | Button §6.5 | `GPIO A/B` | `A GPIO0`, `B via AXP2101` handled by `power_manager`; `btn_poll` stack `2048→4096` fixing `panel_io_i2c_register_event_callbacks` overflow | Stack overflow reboot on `A` press + GPIO conflict |
| 2026-09-02 | UI §6.6 | Basic screens | `bsp_display_lock` checked (`100/200 ms` timeout, skip if busy) to fix `task_wdt` in `lv_label_set_text`; `clear_screen` resets `delete_overlay`; chart/bar added | Previous `lock(0)` without check → WDT + corruption |
| 2026-09-02 | Audio §6.1/6.2 | `16k mono` | `gain 15→30 dB`, `vol 70→92`, `set_in_mute(false)`, `set_out_mute(false)`, `s_mic_open/s_spk_open` tracking, `30 ms` PA settle, `free(path)` | Playback was ear-only, recorder double-open `i2s_channel_disable` error |
| 2026-09-02 | System §3/4 | 7 modules | 8 modules (`power_manager`) + orthogonal `PWR` toggle, `DELETE` modal within `FILE_BROWSER` | Power save request |
| 2026-09-02 | Build | `ESP-IDF v5.5.5` | Same, but `sdkconfig.defaults` now `FATFS_LFN_HEAP`, `BUTTON_B_GPIO=-1`, `BSP I2C pullup`, `target_compile_options -Wno-error=format` | Build `0xb4a20` `82% free` |

*README.md §Hardware `Button B = GPIO21` and §Usage `Button B: play` updated to match this spec (PWR toggles display, playback via touch) to avoid conflict.*

---
# Resources
- Example code and Arduino Libraries: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8
- Hardware specficiations and resources: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8
