# JC4880P443C_I_W_Remote v1.3.7

## Highlights

- Fixes LoRa settings persistence so the selected radio module, radio parameters, and visible GPIO choices are saved from the LoRa Mesh app instead of silently falling back to stale state.
- Fixes the Settings-screen LoRa self-check flow so it persists the current LoRa UI before diagnostics start, which makes E22-400T22S UART tests follow the selected dropdown configuration.
- Heals stale E22-400T22S legacy UART pin maps, including hybrid stored reset-pin state, so saved transparent-UART modules migrate to `TX=31`, `RX=30`, `AUX=33`, `M0=51`, `M1=29`, `NRST=-1` and start cleanly on COM11.
- Refreshes the README, VS Code release task, and packaged release assets for a new `v1.3.7` GitHub release.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `7,761,632` bytes, leaving `364,832` bytes free in either `0x7C0000` OTA slot.
- ESP32-C6 app image: `1,869,456` bytes.
- ESP32-C6 merged flash image: `1,934,992` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.3.7_ota.bin`
- `JC4880P443C_I_W_Remote_v1.3.7_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.3.7_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.3.7_full_flash.zip`