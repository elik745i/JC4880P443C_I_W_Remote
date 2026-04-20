# JC4880P443C_I_W_Remote

Version 1.1.4 custom firmware for the JC4880P443C_I_W / ESP32-P4 Function EV Board profile.

This project keeps the Espressif phone-style launcher experience, then extends it with a broader native app set, emulator support, better SD-card behavior, persistent Wi-Fi settings, timezone control, online firmware discovery, a local factory reset flow, and an external ESP32-C6 coprocessor firmware path for BLE and ZigBee features.

## Hardware And Case Renders

Screen module views:

![Screen module render 1](3D/r1.jpg)
![Screen module render 2](3D/r2.jpg)

Battery and speaker case v1:

![Case render 1](3D/r3.jpg)
![Case render 2](3D/r4.jpg)

Printable STL files for the enclosure and related 3D assets are stored in `3D/`, including `JC4880P443C_I_W_V1.stl`, `JC4880P443C_I_W_V1_case.stl`, the updated `JC4880P443C_I_W_V1.2_case.stl`, and the matching `JC4880P443C_I_W_V1_case.3mf` project export.

## What Changed Versus The Vendor Base

Compared with the stock Espressif-based firmware stack used for this hardware profile, this build adds or changes the following:

- Files app for browsing both `/sdcard` and SPIFFS directly on the device.
- Internet Radio app with station discovery by popularity, country, language, and category.
- Native SEGA app with Master System, Game Gear, SG-1000, and Genesis / Mega Drive ROM support.
- Shared launcher icon set sized to fit the OTA partition budget.
- Persistent Wi-Fi credentials and reconnect behavior backed by NVS.
- Display timezone dropdown in GMT format with saved preference storage.
- Auto timezone detection from the internet after Wi-Fi connects.
- Firmware screen factory reset button with confirmation and settings wipe.
- Firmware releases now publish OTA-detectable `.bin` assets directly instead of ZIP-only packages.
- Safer SD-card boot behavior so video playback is only enabled when MJPEG content is actually present.
- SPIFFS cleanup that removes bundled demo media and frees flash for larger OTA-safe application images.
- Additional low-risk PSRAM placement for radio preview workers, background service stacks, and emulator lookup / ROM buffers to preserve internal SRAM for time-sensitive work.
- Firmware settings can browse GitHub releases directly and offer OTA updates from attached `.bin` assets.
- Dead launcher apps and unreachable video-player sources were removed to reduce maintenance surface and keep OTA builds within budget.
- MP3 probing and decode fallback behavior are more tolerant of malformed frames and stream sync loss.
- BLE and ZigBee features are now enabled through a matching ESP32-C6 coprocessor firmware release.
- The standalone ESP32-C6 release now includes the fixed ZigBee storage partition layout required for stable bring-up.

## Feature Summary

### Launcher And Native Apps

- Phone-style launcher UI based on ESP-Brookesia and LVGL.
- Settings, Calculator, Camera, Files, Music Player, Internet Radio, Image Display, and 2048.
- SEGA Emulator app integrated into the launcher instead of living as a separate upstream project.

### Media And Storage

- Files app can inspect both onboard SPIFFS and the SD card.
- Music and image sample payloads were removed from SPIFFS to save flash.
- The firmware updater can scan `/sdcard/firmware` for local `.bin` images or check GitHub releases for OTA-ready `.bin` assets.

### Wi-Fi, Time, And Settings

- Saved Wi-Fi credentials persist across reboots and reconnect automatically.
- Signal strength and scan results are exposed in Settings.
- System time is sourced from SNTP and converted with the configured local timezone.
- Manual timezone selection is available in GMT offsets.
- Auto timezone mode can update the offset from online geolocation when internet access is available.
- Factory Reset in Settings > Firmware clears the app preferences namespace and reapplies defaults immediately.

### Wireless Coprocessor

- BLE and ZigBee runtime support depends on the external ESP32-C6 coprocessor firmware published in the matching GitHub release.
- If the C6 is not flashed with the firmware from the same release, BLE and ZigBee features on the P4 side are not expected to work correctly.
- The C6 firmware is built from `coprocessor_c6/` and is released alongside the main P4 firmware.

### Emulator Support

- The SEGA app scans `/sdcard/sega_games` for `.sms`, `.gg`, `.sg`, `.md`, `.gen`, `.bin`, and `.smd` ROMs.
- SMS and Game Gear battery saves are written next to the ROM as `.sav` sidecars.
- Genesis / Mega Drive support is integrated through the adapted Gwenesis path.

## Hardware Target

- ESP32-P4 Function EV Board based target.
- 7-inch 1024x600 MIPI-DSI display using EK79007-compatible support.
- MIPI-CSI camera.
- USB-C for power, flashing, and serial monitoring.
- Optional SD card for media, firmware packages, and emulator ROMs.

## Storage And OTA Layout

- Flash size is configured for 16 MB.
- Partition table provides two enlarged OTA app slots of `0x7B0000` each.
- SPIFFS storage partition is reduced to `0x080000` to reclaim flash for OTA headroom while preserving the remaining onboard filesystem features.
- Version 1.1.4 validates at `0x743420`, leaving `0x6CBE0` bytes free in the smallest OTA app slot.

## SD Card Layout

- `/sdcard/music` for music content.
- `/sdcard/image` for image content.
- `/sdcard/sega_games` for SEGA ROMs.
- `/sdcard/firmware` for local `.bin` firmware packages.

## Build

This project targets ESP-IDF 5.5.x and `esp32p4`.

```bash
idf.py set-target esp32p4
idf.py build
```

To flash and open the serial monitor:

```bash
idf.py -p PORT flash monitor
```

## C6 Firmware Requirement

BLE and ZigBee features require the ESP32-C6 coprocessor to be flashed with the matching firmware from the same GitHub release as the P4 firmware.

Use the release assets for both devices together:

- Flash the P4 with the P4 firmware from the release.
- Flash the C6 with the C6 firmware from the same release.
- Do not mix older C6 firmware with a newer P4 release if you expect BLE or ZigBee to work.

## Flashing The ESP32-C6

![ESP32-C6 flashing reference](3D/C6_Flash.jpeg)

To flash the C6 from an external UART bridge, connect the bridge to the board header like this:

- `RX -> TX`
- `TX -> RX`
- `GND -> GND`
- `5V -> 5V`

To put the C6 into boot mode:

1. Pull `C6_IO9` to `GND`.
2. Connect USB to the PC.
3. Put the P4 side into boot mode as well so it does not interfere with the C6: press `BOOT` and `RST`, then release `RST` so the screen stays in boot mode.
4. Release `C6_IO9` from `GND`.
5. Flash the C6 firmware.

This sequence keeps the P4 out of the way while the external UART bridge talks directly to the C6.

## Project Layout

- `main/` boot flow and app installation.
- `components/apps/` native applications and emulator integration.
- `common_components/` board-specific and locally adapted support code.
- `managed_components/` ESP Component Manager dependencies.
- `third_party/` imported upstream code adapted into this firmware.
- `spiffs/` bundled non-media filesystem assets.

## Upstream Sources And Attributions

The firmware in this repository adapts upstream vendor and emulator code. These are the primary sources that should be credited when redistributing or reviewing changes:

- Espressif ESP-Brookesia: https://github.com/espressif/esp-brookesia
- Espressif ESP-WiFi-Remote component: https://components.espressif.com/components/espressif/esp_wifi_remote
- Espressif WiFi Remote over EPPP component: https://components.espressif.com/components/espressif/wifi_remote_over_eppp
- Espressif ESP32-P4 Function EV Board BSP: https://components.espressif.com/components/espressif/esp32_p4_function_ev_board
- Retro-Go upstream: https://github.com/ducalex/retro-go
- SMS Plus GX upstream: https://github.com/ekeeke/smsplus-gx

The local emulator integration under `components/apps/sega_emulator/` and the vendor-facing launcher / board adaptations in this repository were modified to fit the JC4880P443C_I_W firmware, storage layout, UI flow, and OTA constraints.

## Repository

GitHub repository:

```text
https://github.com/elik745i/JC4880P443C_I_W_Remote
```
