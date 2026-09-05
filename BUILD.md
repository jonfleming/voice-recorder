# Build Instructions — ESP-IDF v5.5.5

### Prerequisites
- Windows 10/11
- ESP-IDF v5.5.5 at `C:\esp\v5.5.5\esp-idf` (installed via Espressif installer)
- Tools at `C:\Espressif\tools` (xtensa-esp-elf 14.2, cmake, ninja, python venv)
- Board: Waveshare ESP32-S3 Touch AMOLED 1.8" (USB Type-C, CP210x not needed — native USB)

### One-time setup (if IDF not already installed)
```powershell
# Installer already placed IDF at C:\esp\v5.5.5\esp-idf and tools at C:\Espressif\tools
# Verify:
C:\Espressif\tools\python\v5.5.5\venv\Scripts\python.exe --version
```

### Open IDF shell
Option A — double-click `C:\Users\jonfl\Dropbox\Tools\esp.cmd`  
Option B — PowerShell:
```powershell
. 'C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1'
idf.py --version   # → ESP-IDF v5.5.5
```

### Board selection (required — one binary per panel)

The 1.8" and 2.06" Waveshare BSPs export the **same `bsp_*` symbols**. Linking
both (or flipping only menuconfig) still runs 1.8 display/touch init: 368×448
window + GPIO21 touch INT on a 410×502 panel (green L leftover, dead touch).

CMake links exactly one BSP. **menuconfig is the usual way to pick it:**

```powershell
cd C:\Projects\ai-voice-assistant\voice-recorder
idf.py menuconfig
# → Voice Recorder Config → Target Waveshare board → 1.8" or 2.06"
idf.py fullclean    # required when switching boards (drops the other BSP)
idf.py build
```

A fresh tree (no `sdkconfig` yet) can also pass `-DWAVESHARE_AMOLED_2_06_BOARD=ON`,
which applies the 32 MB flash overlay while generating sdkconfig.

```powershell
# 2.06" from a clean tree
Remove-Item -ErrorAction SilentlyContinue sdkconfig
idf.py fullclean
idf.py "-DWAVESHARE_AMOLED_2_06_BOARD=ON" build
```

On a correct 2.06 boot the log should show:
`Target: Waveshare ESP32-S3 AMOLED 2.06" 410x502` and `LVGL display 410x502`.

### Build
```powershell
idf.py build                                          # 1.8"
# idf.py -DWAVESHARE_AMOLED_2_06_BOARD=ON build       # 2.06"
```
Output:
- `build/voice-recorder.bin` (≈ 700 KB)
- `build/bootloader/bootloader.bin`
- `build/partition_table/partition-table.bin`

### Flash
```powershell
idf.py -p COMx flash         # auto-detect port; or specify COM5 etc.
# 1.8 is 16 MB flash; 2.06 is 32 MB — idf.py flash uses the configured size.
# or manual esptool (adjust --flash_size to 16MB or 32MB):
python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/voice-recorder.bin
```
Enter download mode: hold BOOT, tap RST, release BOOT.

### Monitor
```powershell
idf.py -p COMx monitor
# exit: Ctrl+]
```

### Menuconfig (optional)
```powershell
idf.py menuconfig
# → Voice Recorder Config: Target board (then fullclean + build), buttons, debounce
# → Board Support Package: I2C/I2S/SD speeds
# → Component config → LVGL etc.
idf.py build
```

### Partitions
`partitions.csv`:
```
nvs,      data, nvs,     0x9000, 0x6000,
phy_init, data, phy,     0xf000, 0x1000,
factory,  app,  factory, 0x10000, 0x400000,
storage,  data, spiffs,  ,       0xF0000,
```
SD stores WAVs; SPIFFS unused but reserved.

### Dependencies (auto-fetched via `idf_component.yml`)
- Exactly one of `waveshare/esp32_s3_touch_amoled_1_8 ^2.0.3` or
  `waveshare/esp32_s3_touch_amoled_2_06 ^2.0.0` (selected by `VOICE_RECORDER_BOARD`)
- `espressif/esp_codec_dev ^1.5.0` (ES8311; 2.06 also uses ES7210 for the mics)
- `lvgl/lvgl ^9.2.2` + `esp_lvgl_port`

No manual `Arduino_DriveBus / Arduino_GFX / lv_conf.h` needed — BSP bundles LVGL port. Waveshare Arduino libs referenced in spec are for Arduino path; this is ESP-IDF native.

### Troubleshooting
- `SD mount failed` → reformat microSD FAT32, reinsert, reset.
- `I2C probe failed` → BSP auto-detects V2 (CST820 0x15) vs V1 (FT5x06); check solder jumpers.
- `Button B no response` → BOOT (GPIO0) is reliable; if using GPIO21 for B it shares touch INT — set `CONFIG_BUTTON_B_GPIO=-1` and use only A, or wire external button.
- `Display white` → QSPI gap 0x10 applied on V2; rebuild after touching I2C scan.
