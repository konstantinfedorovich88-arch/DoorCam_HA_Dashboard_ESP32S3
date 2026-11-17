# DoorCam HA Dashboard (ESP32-S3 + LVGL)

Firmware project for the **Waveshare ESP32-S3-Touch-LCD-7** (800x480 RGB LCD, GT911 capacitive touch), Home Assistant panel and DoorCam video-doorbell call UI.

## Status

This is an **initial application scaffold**, not yet a flashed/board-validated firmware. The LVGL UI, Home Assistant entity mapping and DoorCam call-state demo are developed separately from the board display/touch hardware port. GPIO mappings, audio and video must be hardware-verified before enabling them. In particular, the Waveshare ESP32-S3-Touch-LCD-7 specification lists 16 MB flash, 8 MB PSRAM, RGB LCD and GT911, **not an onboard speaker/microphone**. Do not assume that audio hardware is present on the dashboard board.

## Initial architecture

- `main/dashboard_ui.c`: LVGL screens for Home, Rooms, Climate, Security and DoorCam; demonstration of an incoming-call overlay.
- `main/dashboard_call.c`: pure call-state machine (idle / ringing / active), no network or audio side effects.
- `main/dashboard_board.c`: **board integration placeholder**; to be replaced with Waveshare's RGB LCD / GT911 example and LVGL port, not a pretend hardware driver.
- `main/dashboard_ha.c`: explicit Home Assistant integration placeholder, no token embedded in firmware.
- `main/app_main.c`: boot and startup diagnostics. UI initialization awaits board port integration.

## Board documentation

Official hardware and pinout: https://docs.waveshare.com/ESP32-S3-Touch-LCD-7

ESP-IDF development: https://docs.waveshare.com/ESP32-S3-Touch-LCD-7/ESP-IDF

For this exact board, RGB uses multiple GPIO and the GT911/CH422G share I2C GPIO8/9; do not copy pin definitions from the `-7B` or `-7C` boards.

## Build (ESP-IDF 6.1)

```powershell
idf.py set-target esp32s3
idf.py build
```

**Note:** A successful build does not verify display or audio. At this stage the board port reports `ESP_ERR_NOT_SUPPORTED` and application logs this rather than claiming the screen is ready. The LVGL UI code is available for integration with the exact Waveshare ESP-IDF LCD example.

## Proposed next milestones

1. Port Waveshare RGB panel + GT911 touch + CH422G backlight from the official example and run the LVGL home screen on the physical board.
2. Wi-Fi provisioning and secure Home Assistant WebSocket state subscription, with configurable entity IDs.
3. DoorCam call signaling through Home Assistant / MQTT (ring, accept, reject, hangup, session ID, timeout).
4. Verify the dashboard **external audio hardware path**, then implement full-duplex audio with an echo mitigation strategy; add low-res video preview independent from H.264 main stream.

No Wi-Fi credentials or HA access tokens should be committed.
