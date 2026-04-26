# JC4880P443C_I_W_Remote v1.2.11

## Highlights

- Refined the Local Joypad layout workflow so the Local controller uses a real BLE-style `LS` thumb-stick overlay instead of a grouped badge proxy.
- Updated the Joypad Layout Configurator so Local `LS` is directly selectable and editable, while wheel scrolling over the canvas now controls zoom.
- Cleaned up the Local controller preview so BLE-only Local leftovers are removed from the visible overlay path and Local-only controls behave more predictably.
- Updated release documentation and README content for the v1.2.11 release.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `0x6E2FA0` bytes, leaving `0x0AD060` bytes free in the smaller OTA slot.
- ESP32-C6 app image: `0x1C7E50` bytes, leaving `0x0181B0` bytes free in the 1920 KB app slot.
- ESP32-C6 merged flash image: `0x1D7E50` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.2.11_ota.bin`
- `JC4880P443C_I_W_Remote_v1.2.11_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.2.11_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.2.11_full_flash.zip`