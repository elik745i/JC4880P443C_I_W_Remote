# JC4880P443C_I_W_Remote v1.3.1

## Highlights

- Added four new launcher apps: Browser, YouTube, E-Reader, and MQTT.
- Browser and YouTube now use SD-card-backed cache when available and fall back to PSRAM when the card is not mounted.
- Image Viewer now scans only `/sdcard/image` and no longer falls back to SPIFFS or extra image folders.
- The P4 project name and generated firmware image remain `ESP32P4_Remote`, and the README was refreshed to document the expanded launcher surface and SD-card expectations.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `0x71C540` bytes, leaving `0x073AC0` bytes free in the smaller OTA slot.
- ESP32-C6 app image: `0x1C7E50` bytes, leaving `0x0181B0` bytes free in the 1920 KB app slot.
- ESP32-C6 merged flash image: `0x1D7E50` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.3.1_ota.bin`
- `JC4880P443C_I_W_Remote_v1.3.1_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.3.1_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.3.1_full_flash.zip`
