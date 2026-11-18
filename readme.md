# ZMK Config for Charybdis 4x6 (Nice!Nano Dongle + Round GC9A01 Display)

Custom ZMK firmware configuration (Zephyr 4.1) for the **Charybdis 4x6** wireless split keyboard using a Nice!Nano v2 dongle and a round GC9A01 IPS display, based heavily on the original repository [choovick/zmk-config-charybdis](https://github.com).

## Hardware & Pinout
* **Left/Right Halves & Dongle**: Nice!Nano v2 controllers (with trackball on the right half and a central USB dongle with ZMK Studio enabled).
* **Display**: Round 1.28" GC9A01 IPS LCD (240×240, SPI) wired to the Nice!Nano: SCL/SCK (15), SDA/MOSI (16), CS (10), DC (9), RES (8), VCC (3V3), GND, and BLK (3V3).

## Dongle Screen Features
* **Arcs**: Real-time battery levels for both halves (turns red below 20%), with a swap option (`CONFIG_DONGLE_ROUND_SCREEN_SWAP_SIDES=y`) if sides appear inverted.
* **Status**: Active layer name/index in the center, a `CAPS` indicator pill at the top, and exact battery percentages (`L` / `R`) at the bottom.

## Firmware & Flashing
Download `firmware.zip` from GitHub Actions containing four `.uf2` files (`charybdis_left`, `dongle_charybdis_right`, `dongle_nice_round dongle_screen`, and `settings_reset`). 

1. **Reset**: Double-tap `Reset` (or short `RST` + `GND`) and flash `settings_reset-nice_nano-zmk.uf2` to all three controllers.
2. **Flash**: Upload the dongle firmware to the dongle (kept plugged into the PC), then the left half (pair first so `L` maps correctly), and finally the right half.

## ZMK Studio
Enabled on the dongle build:
* **Unlock Combo**: Press all three right thumb keys simultaneously (`RET` + `SPACE/SYMBOLS` + `BSPC/RAISE`).
* **Edit Keymap**: Use [zmk.studio](https://zmk.studio) in your browser or via the desktop app.
