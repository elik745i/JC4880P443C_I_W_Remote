# JC4880P443C_I_W_Remote v1.3.2

## Highlights

- OTA update awareness now supports default-enabled automatic update start from the Firmware OTA page and can roll back automatically if a freshly updated image crashes before it is marked healthy.
- Image Viewer now lazy-loads gallery thumbnails, prefers PSRAM-backed decode buffers, falls back across hardware JPEG, TJpgDec, and libjpeg-turbo decode paths, and routes fullscreen/slideshow opens through the LVGL thread to avoid the previous tap/open instability.
- The fullscreen viewer overlay is forced back to the foreground when an image opens, so tile taps and slideshow entry now switch cleanly from the gallery into the full-screen viewer instead of leaving the gallery visible.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `0x751940` bytes, leaving `0x03E6C0` bytes free in the smaller OTA slot.
- ESP32-C6 app image: `0x1C7E50` bytes, leaving `0x0181B0` bytes free in the 1920 KB app slot.
- ESP32-C6 merged flash image: `0x1D7E50` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.3.2_ota.bin`
- `JC4880P443C_I_W_Remote_v1.3.2_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.3.2_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.3.2_full_flash.zip`