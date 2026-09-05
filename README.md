# Voice Recorder — Waveshare ESP32-S3 AMOLED 1.8" / 2.06"

ESP-IDF v5.5.5 firmware for Waveshare ESP32-S3 Touch AMOLED 1.8" (368×448) and 2.06" (410×502). Implements
spec `Voice-Recorder-Spec-opencode.md` as amended 2026-09-02: recording (WAV 16k mono 16-bit) with 20 Hz scrolling waveform, file browser (newest first) with delete, playback via touch, and PWR-button display toggle for battery save.

## Hardware (Amended)
- MCU: ESP32-S3R8 (dual-core LX7, 8 MB PSRAM, 16 MB Flash)
- Display: 1.8" AMOLED 368×448 QSPI (CO5300 / SH8601, QSPI 12/11/4-7), touch CST820 `0x15` / FT5x06 `0x38` via I2C `15/14` `400kHz` (5× retry + pullup, continues without touch if not found)
- Audio: ES8311 codec (I2S `16/9/45/8/10/46` `16 kHz mono`, `vol 92`, `gain 30 dB`) + mic + speaker amp
- Storage: microSD via SDMMC 1-bit `2/1/3` FAT32 `LFN_HEAP` (for `YYYYMMDD_HHMMSS.wav`)
- Buttons: **Button A** = `BOOT GPIO0` active-low (record start/stop); **Button B** = `PWR` via `AXP2101 0x34` on `I2C 14/15` — short-press toggles **display ON/OFF** for battery save (long-press >6s still hard power-off). `BUTTON_B_GPIO=-1` by default because `GPIO21` is `TOUCH_INT` (conflict → phantom presses, see spec §1.4). Playback is via on-screen `PLAY` / double-tap when `B` is `-1`; remap `CONFIG_BUTTON_B_GPIO` to a free pad (e.g. `38`) to restore `B`→play.
- PMU: `AXP2101` `0x34` for `PWR` and battery (polled `120 ms` in `power_manager`)

See [PINOUT.md](PINOUT.md) for full pin map and [Voice-Recorder-Spec-opencode.md §10](Voice-Recorder-Spec-opencode.md#10-changelog--implementation-amendments-added) for amendment rationale.

## Project Structure
```
.
├── CMakeLists.txt
├── sdkconfig.defaults  # FATFS_LFN_HEAP, BUTTON_A=0, BUTTON_B=-1, I2C pullup, octal PSRAM
├── partitions.csv      # 4 MB factory, 960K storage
├── main/
│   ├── app_main.c          # State machine (IDLE→RECORDING→BROWSER→PLAYBACK) + delete + PWR
│   ├── ui_manager.*        # LVGL 9.5 screens: recording chart 40pts@20Hz, browser PLAY+delete dialog, playback
│   ├── audio_recorder.*    # I2S mic → SD WAV, RMS+peak level 0..1000, 30 dB gain, s_mic_open tracking
│   ├── audio_player.*      # WAV → I2S speaker, vol 92, s_spk_open tracking, 30ms PA settle
│   ├── audio_commons.*     # Shared I2S 16k mono init (must be first), ES8311 handles
│   ├── filesystem_manager.*# SD mount 1-bit 20MHz, list/sort .wav newest-first, delete unlink
│   ├── button_manager.*    # BOOT GPIO0 debounce (stack 4096), B disabled when 21
│   ├── power_manager.*     # AXP2101 0x34 poll 120ms, INTSTS2 bit3 → display toggle via backlight
│   ├── wav.*               # WAV header create/parse/update (handles extra fmt)
│   ├── state_machine.*     # Enum + helper (PWR toggle orthogonal, DELETE modal)
│   ├── Kconfig.projbuild
│   └── idf_component.yml   # waveshare/esp32_s3_touch_amoled_1_8 2.0.3, esp_codec_dev 1.5.11, lvgl 9.5
└── docs (this README, PINOUT, BUILD, TEST_PLAN, Spec §10)
```

## State Machine (Amended)
```
IDLE ──A──► RECORDING (PWR toggle) ──A──► FILE_BROWSER (PWR toggle) ──PLAY (B if GPIO>=0 / on-screen PLAY / double-tap)──► PLAYBACK ──ends/A/B──► FILE_BROWSER
                 ▲ Touch select ────────────────────────────────┘     ↖ long-press → Delete? → Delete/Cancel (modal, stays in BROWSER)
```
* `PWR` short (AXP2101) is orthogonal — toggles `Display ON/OFF` via `power_manager` without changing state (see spec §4).*

## Build — TL;DR
Requires ESP-IDF v5.5.5 (`C:\esp\v5.5.5\esp-idf`) and tools at `C:\Espressif`.
Use `C:\Users\jonfl\Dropbox\Tools\esp.cmd` to open an IDF shell, or:

```powershell
. 'C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1'
# Pick the board in menuconfig (Voice Recorder Config → Target Waveshare board),
# then fullclean when switching so the other BSP is not linked:
#   idf.py menuconfig
#   idf.py fullclean
#   idf.py build
idf.py build
idf.py -p COMx flash monitor
```

One binary per board. See [BUILD.md](BUILD.md).

Binary: `build/voice-recorder.bin` (83% free on 4 MB factory partition). Flash with:
```powershell
python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/voice-recorder.bin
```

Full steps → [BUILD.md](BUILD.md)

## Usage (Amended)
1. Insert FAT32 microSD (`LFN_HEAP` required for `YYYYMMDD_HHMMSS.wav`).
2. Power board (`PWR` long-press or Type-C). Display shows `Files`.
3. **Button A** (`BOOT GPIO0`): start/stop recording. Screen shows `Recording… mm:ss` + **scrolling green waveform 40 pts @20 Hz** (RMS+peak `0..1000`) + instant bar, bottom `Press A to stop`. Gains already set for audible capture.
4. After stop, browser lists `YYYYMMDD_HHMMSS.wav` newest-first (`340×300` list, `40 px` rows); drag to scroll, **tap to select (blue)**, **double-tap same file → play**, **long-press (~400 ms) → Delete? dialog** with `Cancel` (grey) / `Delete` (red) → `unlink` → list refreshes. On-screen `PLAY selected` button (`340×42` green) also plays when visible.
5. **Playback:** `PLAY` button or double-tap (or `B` if `BUTTON_B_GPIO>=0` remapped) → `Playing: <file> mm:ss / mm:ss` (scrolling filename) → returns to browser on end; `A/B` can stop early. Volume `92/100` (V2 needs 90).
6. **PWR (Button B) short-press:** toggles **display ON/OFF** for battery save (`power_manager` polls `AXP2101 0x34` `INTSTS2 bit3` every `120 ms`, debounced `400 ms`); state stays, backlight `0` vs `100` (AMOLED black ≈ off). Long-press `>6s` still hard power-off. Works even when touch is not found (5× retry, then `continue without touch`).
7. SD removal/re-insert handled (retry every `2s`); corrupted `WAV`/`8.3` aliases skipped; `task_wdt`/`stack 4096`/`i2s` double-close fixes applied.

## Configuration (Amended)
`idf.py menuconfig` → `Voice Recorder Config`:
- `BUTTON_A_GPIO` `0` (BOOT), `BUTTON_B_GPIO` `-1` (PWR via AXP2101 — set to free pad e.g. `38` and wire to GND to restore `B`→play)
- `RECORDER_SAMPLE_RATE`/`BITS`/`CHANNELS` fixed `16k/16/mono` per spec; `FATFS_LFN_HEAP=y` already default for long filenames.
- `BSP_*` (`I2C 1` `15/14` `pullup+glitch`, `I2S 1` `16k`, `SD 1-bit 20MHz`, `LCD QSPI`, `PSRAM octal 40M`) in `Board Support Package` menu.
- `PWR` polling is automatic; no Kconfig — requires `AXP2101 0x34` on `14/15` (same bus, probed at boot).

## Assets
LVGL uses built-in `montserrat_14`; no external font assets required. QSPI display is black/dark theme per spec.

## Test Plan
→ [TEST_PLAN.md](TEST_PLAN.md)

## License
Apache-2.0 (matches Waveshare BSP).
