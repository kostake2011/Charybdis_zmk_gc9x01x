# ZMK Config — Charybdis 4×6 (dongle + GC9A01)

Wireless ZMK config for **Charybdis 4×6** with PMW3610 trackball, nRF52840 (Nice!Nano / clones) and a USB dongle with a round **GC9A01 240×240** display.

Based on [choovick/zmk-config-charybdis](https://github.com/choovick/zmk-config-charybdis).

---

## Hardware

| Role | Board | Notes |
|------|-------|--------|
| Left half | Nice!Nano / nRF52840 clone | Peripheral |
| Right half | Nice!Nano / nRF52840 clone | Peripheral + PMW3610 |
| Dongle | Nice!Nano / nRF52840 clone | Central, USB, GC9A01 |

### GC9A01 → dongle pinout

| Display | Controller (pro_micro) |
|---------|------------------------|
| SCL / SCK | 15 |
| SDA / MOSI | 16 |
| CS | 10 |
| DC | 9 |
| RES | 8 |
| VCC | 3V3 |
| GND | GND |
| BLK | 3V3 (always on) |

---

## Builds (`build.yaml`)

| Artifact | Shield | Purpose |
|----------|--------|---------|
| `charybdis_left-…uf2` | `charybdis_left` | Left half |
| `dongle_charybdis_right-…uf2` | `dongle_charybdis_right` | Right half (to dongle) |
| `dongle_nice_round dongle_screen-…uf2` | `dongle_nice_round` + `dongle_screen` | Dongle + GC9A01 + Studio |
| `settings_reset-…uf2` | `settings_reset` | Clear bonds / settings |

---

## Flashing

1. Flash **`settings_reset`** on all three controllers (double-tap Reset / short RST–GND, drop the `.uf2`).
2. Flash the **dongle** (keep it plugged into the PC).
3. Flash the **left** half, then the **right** half.  
   Pair left first so battery side `L` maps correctly.

---

## Bootloader

**Hardware** (any half / dongle):

- Double-tap the **Reset** button, or briefly short **RST** to **GND**.

**From the keymap** (RAISE layer):

- Hold **RAISE** (right thumb — hold on `BSPC`) and press the top-right key on the right half (`&bootloader`).

---

## ZMK Studio

Studio is enabled on the **dongle** build.

**Unlock combo** — press all three right thumb keys at once:

| Pos | Base key |
|-----|----------|
| 53 | `RET` |
| 54 | `SPACE` (hold → Symbols) |
| 55 | `BSPC` (hold → Raise) |

Hold them together → Studio unlocks. Then open [zmk.studio](https://zmk.studio) in the browser or the desktop app and edit the keymap live.

---

## Dongle screen

- Two quarter **battery arcs** (left / right edge, red below 20%) + `L xx%` / `R xx%`
- Four **link bars** at the top for each half; a red `X` and `--%` replace them when that half is disconnected
- Centre: active **layer** name from the keymap; turns into a red `LINK LOST: L / R / L+R` on disconnect
- Accent colour follows the active layer (table `layer_styles[]` in `src/status_screen.c`, keep it in sync with `charybdis_layers.h`)
- Bottom: Caps Lock `A`, or a `TRACKBALL` badge on the mouse layers, or the chip logo when idle

If left/right batteries look swapped, add to `boards/shields/dongle_screen/dongle_screen.conf`:

```conf
CONFIG_DONGLE_ROUND_SCREEN_SWAP_SIDES=y
