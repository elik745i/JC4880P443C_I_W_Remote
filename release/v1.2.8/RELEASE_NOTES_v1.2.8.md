# JC4880P443C_I_W_Remote v1.2.8

## Highlights

- Added runtime power-management setup on the main ESP32-P4 firmware so tickless idle can actually enter light sleep and reduce idle CPU utilization.
- Expanded PSRAM-first placement for subsystem memory, including NVS cache, mDNS allocations, NimBLE heap usage, the audio echo test buffer, and more background worker stacks.
- Added newer attachable joystick enclosure assets and fresh 3D reference exports while keeping the matching ESP32-C6 coprocessor release assets for BLE and ZigBee support.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `0x6D0AD0` bytes, leaving `0x0BF530` bytes free in the smaller OTA slot.
- ESP32-C6 app image: `0x181AD0` bytes, leaving `0x05E530` bytes free in the 1920 KB app slot.
- ESP32-C6 merged flash image: `0x191AD0` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.2.8_ota.bin`
- `JC4880P443C_I_W_Remote_v1.2.8_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.2.8_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.2.8_full_flash.zip`