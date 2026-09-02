# Test Plan — Voice Recorder

### Environment
- Hardware: Waveshare ESP32-S3 AMOLED 1.8" + FAT32 microSD (≥ 100 MB free)
- Firmware: `build/voice-recorder.bin` (ESP-IDF v5.5.5)
- Tools: `idf.py monitor`, serial log, SD reader

### Functional Tests

| ID | Spec | Steps | Expected |
|---|---|---|---|
| T-REC-01 | 2.1 Button A short press idle→rec | Idle/browser, press A | `Recording… 00:00` + live RMS bar 20 Hz, log `Start recording -> /sdcard/YYYYMMDD_HHMMSS.wav` |
| T-REC-02 | Elapsed time 1 Hz, no drops | Record 60 s | Timer increments 00:00→01:00, file size ≈ 60×32000=1.92 MB, WAV parse ok, no glitches |
| T-REC-03 | Button A rec→stop→browser | While recording, press A | File closed, header `chunk_size=36+data`, browser lists new file at top (newest first) |
| T-REC-04 | Filename format | `ls /sdcard/*.wav` | `20260818_112355.wav`-style; second record in same second gets `_XX` suffix |
| T-FS-01 | Sort newest first | Record 3 files, reboot, browser | Order desc by filename timestamp |
| T-FS-02 | Corrupted/long-name skip | Put `bad.wav` (0 bytes) + `verylongname...wav` | Listed/skip gracefully, no crash |
| T-FS-03 | SD removal | Remove SD while in browser | Log `SD not mounted, retrying`, UI stays, re-insert → list refreshes within 2 s |
| T-BROW-01 | Touch scroll & tap | Drag list, tap row | Scroll moves, tapped row highlights blue, `Selected N <file>` log, `s_selected` updates |
| T-BROW-02 | Empty SD | Empty card, browser | Shows `No recordings yet` |
| T-PLAY-01 | B no selection → ignore | Browser, no selection, press B | No playback, log `B pressed but no file selected` |
| T-PLAY-02 | B with selection → play | Select file, press B | `Playing: <file> mm:ss / mm:ss` 5 Hz update, audio out, returns to browser on EOF |
| T-PLAY-03 | WAV format | `ffprobe` on recorded file | `pcm_s16le, 16000 Hz, mono, 16-bit` |
| T-PLAY-04 | Interrupt playback | While playing, press A/B | Playback stops, returns to browser |
| T-PERF-01 | Sustained write | Record 10 min | No drop, SD write ≥ 512 B /2 ms, log no `write failed` |
| T-PERF-02 | UI FPS | Drag list while recording (separate test) | ≥ 30 FPS, touch latency <20 ms (LVGL port) |

### Manual Procedure
1. Format SD FAT32, `idf.py flash monitor`.
2. Verify `bsp_sdcard_mount` + `SD mounted at /sdcard` + display shows Files.
3. T-REC-01→03, check file on PC.
4. `python -c "import wave; print(wave.open('YYYYMMDD_HHMMSS.wav').getparams())"` → `nchannels 1, sampwidth 2, framerate 16000`.
5. T-BROW-01/02, T-PLAY-01..04.
6. Pull SD mid-test (T-FS-03).

### Automation (host-side)
```powershell
# After recording, list SD via device
idf.py monitor | Select-String "Found.*wav files"
# Parse WAV
python tools/check_wav.py --file /sdcard/202*.wav --expect sr=16000 bits=16 ch=1
```

### Pass Criteria
- All T-REC/BROW/PLAY pass, no panic, no SD corruption after 10× cycles.
