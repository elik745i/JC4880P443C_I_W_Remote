# JC4880P443C_I_W_Remote v1.3.1

## Highlights

- Added four new launcher apps: Browser, YouTube, E-Reader, and MQTT.
- Browser and YouTube now use SD-card-backed cache when available and fall back to PSRAM when the card is not mounted.
- Browser and YouTube launcher icons are now wired from the packaged app assets, Browser opens into a Chrome-like shell with `google.com` as the first-launch home page, and YouTube opens directly into a popular-video feed.
- Image Viewer now scans only `/sdcard/image`, guards failed directory opens, and no longer crashes when exiting back to the launcher after partial initialization.
- SD-card power-handle cleanup was hardened so failed mount attempts and unmount paths do not leak the on-chip SD LDO handle.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `0x7240A0` bytes, leaving `0x06BF60` bytes free in the smaller OTA slot.
- ESP32-C6 app image: `0x1C7E50` bytes, leaving `0x0181B0` bytes free in the 1920 KB app slot.
- ESP32-C6 merged flash image: `0x1D7E50` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.3.1_ota.bin`
- `JC4880P443C_I_W_Remote_v1.3.1_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.3.1_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.3.1_full_flash.zip`
