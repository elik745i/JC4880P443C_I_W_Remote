# JC4880P443C_I_W_Remote

Custom ESP32-P4 touchscreen firmware built on ESP-Brookesia and ESP-IDF 5.4 for the JC4880P443C_I_W hardware profile. This repo keeps the phone-style launcher UI, media features, camera support, SD card handling, and custom native apps such as the new Files browser.

## Highlights

- Native launcher-style UI based on ESP-Brookesia and LVGL
- Settings, calculator, media, camera, image display, and custom apps
- Files app for browsing both `/sdcard` and SPIFFS storage
- Safer SD-card boot behavior by only enabling video playback when supported MJPEG content is present
- ESP-IDF 5.4 workflow configured for the ESP32-P4 target

## Hardware

- ESP32-P4 Function EV Board based build target
- 7-inch 1024x600 MIPI-DSI display using EK79007
- MIPI-CSI camera
- USB-C for power, flashing, and serial output
- Optional SD card for media and file browsing

## Project Layout

- `main/`: boot flow and app installation
- `components/apps/`: native app implementations
- `common_components/` and `managed_components/`: board, framework, and dependency code
- `spiffs/`: bundled filesystem content

## Build

This project is intended for ESP-IDF 5.4.x.

```bash
idf.py set-target esp32p4
idf.py build
```

To flash and open the monitor:

```bash
idf.py -p PORT flash monitor
```

## SD Card Notes

- The video player only enables itself when `/sdcard/mjpeg` exists and contains `.mjpeg` files.
- Standard `.mp4` files are not played directly by the built-in video app.
- The Files app can be used to inspect SD card contents directly on the device.

Example conversion command:

```bash
ffmpeg -i input.mp4 -vcodec mjpeg -q:v 2 -vf "scale=1024:600" -acodec copy output.mjpeg
```

## App Conventions

New native apps should follow the structure documented in `components/apps/README.md` so they match the rest of the project.

## Repository

GitHub remote:

```text
https://github.com/elik745i/JC4880P443C_I_W_Remote
```
