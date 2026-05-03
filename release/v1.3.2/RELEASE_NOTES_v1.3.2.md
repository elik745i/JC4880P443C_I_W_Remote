# JC4880P443C_I_W_Remote v1.3.2

## Highlights

- OTA-friendly flash layout now uses two balanced `0x7C0000` app slots and trims SPIFFS to `0x040000`, which materially increases headroom for in-place release refreshes.
- Browser and YouTube were removed from the launcher set to reduce flash and maintenance overhead, keeping the shipped media surface focused on active playback paths.
- Image Viewer now lazy-loads gallery thumbnails, keeps a dynamic `/sdcard/sys/thumbs` cache in sync with the image folder, prefers PSRAM-backed decode buffers, and keeps fullscreen/slideshow transitions on the safe LVGL thread.
- Internet Radio quick-access controls were refined so previous/next actions stay in the curtain instead of reopening the app, with wider capsule buttons and smoother in-place detail refresh.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `0x6FBE60` bytes, leaving `0x0C41A0` bytes free in either `0x7C0000` OTA slot.
- ESP32-C6 app image: `0x1C7E50` bytes, leaving `0x0181B0` bytes free in the 1920 KB app slot.
- ESP32-C6 merged flash image: `0x1D7E50` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.3.2_ota.bin`
- `JC4880P443C_I_W_Remote_v1.3.2_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.3.2_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.3.2_full_flash.zip`