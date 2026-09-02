# Flashable Binary

Build produced:
- `build/bootloader/bootloader.bin` (0x0)
- `build/partition_table/partition-table.bin` (0x8000)
- `build/voice-recorder.bin` (0x10000) — factory app 4 MB partition

Flash (Windows):
```powershell
. 'C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1'
idf.py -p COMx flash
# or
python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/voice-recorder.bin
```

Artifacts are in `build/` after `idf.py build`; share that folder as release archive (matches Waveshare `releases/downloads/` pattern).
