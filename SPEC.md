---
tags:
  - coding
creation date: 2026-08-18 11:39
modification date: Tuesday 18th August 2026 11:39:27
---
# **Firmware Specification — Waveshare ESP32‑S3 AMOLED 2.06" Touch Display**
### **Project: Audio Recorder + File Browser + Playback**
### **Target Hardware: Waveshare ESP32‑S3 AMOLED 2.06" Touch Display**
### **Author: Jon**
### **Date: 2026‑08‑18**
---
## **1. Hardware Overview**
### **1.1 MCU**
- ESP32‑S3 (dual‑core Xtensa LX7)
- Built‑in USB‑OTG
- Built‑in LCD interface (8080/ SPI depending on Waveshare board)
- Built‑in touch controller (GT911 or CSTxxx depending on model)
### **1.2 Display**
- 2.06" AMOLED
- Resolution: 410×502 (Waveshare spec)
- Interface: SPI or 8080 parallel (developer must confirm)
- Touch panel: Capacitive, I²C interface
### **1.3 Audio**
- Microphone: I2S digital mic (developer must confirm pinout)
- Speaker: I2S DAC + amplifier (developer must confirm pinout)
### **1.4 Buttons**
- Button A (GPIO input, pull‑up)
- Button B (GPIO input, pull‑up)
### **1.5 Storage**
- MicroSD card via SPI or SDMMC (developer must confirm)
- FAT32 filesystem
---
# **2. Functional Requirements**
## **2.1 Recording**
### **Trigger**
- **Button A short press**
  - If idle → start recording
  - If recording → stop recording
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
### **Recording Behavior**
- Create new file on SD card
- Stream audio chunks to file
- Display:
  - “Recording…”
  - Elapsed time (mm:ss)
  - **Nice‑to‑have:** waveform visualization (RMS or peak level)
  - **Nice‑to‑have:** remaining battery level
### **Waveform Requirements (optional)**
- Horizontal scrolling bar or vertical bars
- Update at 20–30 FPS
- Use RMS or peak amplitude from audio buffer
---
## **2.2 File Browser**
### **Trigger**
- When recording stops, automatically transition to File Browser screen
### **Features**
- Read directory `/` on SD card
- List all `.wav` files
- Sort by newest first (descending timestamp)
- Display:
  - Scrollable list
  - Filename
  - File size (optional)
- Touch interaction:
  - Vertical scrolling
  - Tap to select a file
  - Highlight selected file
### **Scrolling Requirements**
- Kinetic scrolling (optional)
- Scroll bar (optional)
- Touch gestures:
  - Drag up/down → scroll list
  - Tap → select item
---
## **2.3 Playback**
### **Trigger**
- **Button B short press**
  - If a file is selected → play selected file
  - If no file selected → ignore
### **Playback Format**
- WAV PCM 16‑bit mono 16 kHz
### **Playback Behavior**
- Open selected file
- Stream audio to I2S speaker
- Display:
  - “Playing: \<filename\>”
  - Elapsed time (mm:ss)
- When playback ends:
  - Return to File Browser screen
---
# **3. System Architecture**
## **3.1 Main Components**
1. **UI Manager**
2. **Audio Recorder**
3. **Audio Player**
4. **Filesystem Manager**
5. **Touch Input Manager**
6. **Button Input Manager**
7. **State Machine**
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
|    RECORDING     |
+------------------+
        |
        | Button A
        v
+------------------+
|   FILE BROWSER   |
+------------------+
   |           |
   | Touch     | Button B
   v           v
Select file   Play file
                |
                v
+------------------+
|     PLAYBACK     |
+------------------+
        |
        | Playback ends
        v
+------------------+
|   FILE BROWSER   |
+------------------+
```
---
# **5. UI Specification**
## **5.1 Recording Screen**
### **Layout**
- Top: “Recording…”
- Middle: elapsed time (large font)
- Bottom: waveform (optional)
- Background: black or dark theme
### **Update Rate**
- Elapsed time: 1 Hz
- Waveform: 20–30 Hz
---
## **5.2 File Browser Screen**
### **Layout**
- Title: “Files”
- Scrollable list:
  - Each row ~40 px height
  - Filename text
  - Highlight selected row
- Touch gestures:
  - Drag to scroll
  - Tap to select
---
## **5.3 Playback Screen**
### **Layout**
- Top: “Playing”
- Middle: filename
- Bottom: elapsed time
---
# **6. Module Specifications**
## **6.1 Audio Recorder Module**
### **Responsibilities**
- Initialize I2S microphone
- Create WAV header
- Stream audio to SD card
- Track elapsed time
- Provide RMS/peak values for waveform
### **Interfaces**
- `recorder.start()`
- `recorder.stop()`
- `recorder.getElapsedTime()`
- `recorder.getWaveformLevel()`
---
## **6.2 Audio Player Module**
### **Responsibilities**
- Open WAV file
- Parse header
- Stream audio to I2S speaker
- Track elapsed time
### **Interfaces**
- `player.play(filename)`
- `player.stop()`
- `player.getElapsedTime()`
---
## **6.3 Filesystem Manager**
### **Responsibilities**
- Initialize SD card
- List files
- Sort files
- Provide metadata
### **Interfaces**
- `fs.listFiles(extension=".wav")`
- `fs.open(filename)`
- `fs.write(filename, data)`
- `fs.close()`
---
## **6.4 Touch Input Manager**
### **Responsibilities**
- Read touch coordinates
- Detect gestures:
  - Tap
  - Drag
  - Scroll
### **Interfaces**
- `touch.getEvent()`
- `touch.getPosition()`
---
## **6.5 Button Input Manager**
### **Responsibilities**
- Debounce buttons
- Detect short press
### **Interfaces**
- `buttonA.wasPressed()`
- `buttonB.wasPressed()`
---
## **6.6 UI Manager**
### **Responsibilities**
- Render screens
- Maintain screen transitions
- Handle touch events
- Handle button events
### **Interfaces**
- `ui.showRecordingScreen()`
- `ui.showFileBrowser(files)`
- `ui.showPlaybackScreen(filename)`
---
# **7. Performance Requirements**
### **Recording**
- Must not drop audio samples
- SD card write speed must sustain 512 bytes every 2 ms
### **UI**
- Must maintain 30 FPS minimum
- Touch latency < 20 ms
### **Playback**
- Must stream audio without gaps
---
# **8. Non‑Functional Requirements**
### **Reliability**
- Handle SD card removal gracefully
- Handle corrupted files gracefully
### **Maintainability**
- Modular architecture
- Clear separation of UI and audio logic
### **Extensibility**
- Future features:
  - Bluetooth audio
  - Wi‑Fi upload
  - Waveform zoom
---
# **9. Deliverables for Developer**
1. Full firmware source code
2. Build instructions (ESP‑IDF preferred)
3. Pinout documentation
4. UI assets (fonts, icons)
5. Test plan
6. Flashable binary
---
- Use ESP‑IDF v5.5.5
- Waveshare Arduino Libraries
	- Arduino_DriveBus
	- Arduino_GFX
	- lvgl
	- lv_conf.h
	- Mylibrary
	- SensorLib
	- XPowersLib

---
# Resources
- Example code and Arduino Libraries: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06
- Hardware specficiations and resources: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.06
