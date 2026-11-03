# Jaiba Hexa Drum — ESP32 7" UI

ESP32-S3 firmware for the Jaiba Hexa Drum touch UI. It runs on the **Elecrow
CrowPanel ESP32 HMI 7.0" (DIS08070H)** and will monitor the left/right sensor
Teensys and configure the knob/button hub through a single I2C link.

This is the **fourth firmware project** in the workspace:

| Project | Role |
|---|---|
| `jaiba-hexa-drum-sensor-code` | right/left sensor firmware |
| `jaiba-hexa-drum-ui` | Teensy control-hub firmware (+ legacy TFT UI env) |
| **`jaiba-hexa-drum-esp32-ui`** | **this repo — ESP32 7" LVGL UI** |
| `jaiba-sampler` | PC sampler (third MIDI input + fixed control map) |

The full architecture, protocols and two-Teensy test procedure live in the
workspace plan: `Docs/CONTROL_HUB_ESP32_PLAN.md`.

## Hardware

- **Module:** Elecrow CrowPanel ESP32 HMI 7.0" — `DIS08070H`
- **SoC:** ESP32-S3-WROOM-1-N4R8 — 4 MB flash, 8 MB octal PSRAM
- **Display:** 800 × 480 RGB TFT, EK9716BD3 + EK73002ACGB
- **Touch:** GT911 capacitive, I2C on GPIO19/20, reset via PCA9557 (0x18)
- **Power:** 5 V / 2 A (USB-C or DC)
- **Link to the drum PCB:** I2C only (SDA 19 / SCL 20 / GND) + power; UART0/J10
  and the microSD slot stay free.

### Display / touch pins

| Signal | GPIO |
|---|---|
| RGB data | 15, 7, 6, 5, 4, 9, 46, 3, 8, 16, 1, 14, 21, 47, 48, 45 |
| HSYNC / VSYNC / DE / PCLK | 39 / 40 / 41 / 0 |
| Backlight | 2 |
| GT911 SDA / SCL | 19 / 20 |
| PCA9557 touch reset | I2C 0x18 |
| UART0 / J10 (debug/programming) | IO44 RX, IO43 TX |

The RGB bus runs at 24 MHz with two full-frame buffers in PSRAM.

## Software

- PlatformIO + **pioarduino 55.03.38**, **Arduino 3.3.8** (vendor base; the
  LovyanGFX RGB patch needs the ESP-IDF 5.x APIs).
- The pioarduino tool creates a Python venv. On hosts without
  `python3-venv`/`ensurepip`, install the standalone `uv` binary and put it on
  `PATH`; the vendor's penv setup uses it.
- **LVGL 9.1.0**
- **LovyanGFX 1.2.25** + the vendor RGB patch (`patches/LovyanGFX/esp32s3/`,
  applied by `scripts/patch_lovyangfx.py`)
- Board JSON `esp32-s3-devkitc-1-myboard` (`qio_opi`, 4 MB flash, 8 MB PSRAM)
- Partition table `huge_app.csv` (3 MB app + 896 KB SPIFFS)

## Build & flash

```bash
pio run                 # build
pio run -t upload       # flash via the USB-C CH340
pio device monitor -b 115200
```

- No `upload_port` is pinned; PlatformIO auto-detects the CH340. Pass
  `--upload-port /dev/ttyUSB0` explicitly if needed.
- If upload cannot auto-reset the ESP32, hold **BOOT** and tap **RST**, then
  retry.
- The first build downloads the pioarduino platform, Arduino 3.3.8, LVGL and
  LovyanGFX; it is much slower than later builds.

## Current status

**Phase 0 — bring-up.** The screen shows a Jaiba title, the panel resolution
and a `TOUCH ME` button; each GT911 tap increments a counter. This verifies:

- the RGB panel and backlight,
- the LVGL 9.1 full-frame double buffer in PSRAM,
- the GT911/PCA9557 touch path.

Next phases (see the plan):

1. Teensy control hub in `U_UI`: sensor UART bridge + knobs/buttons + USB-MIDI
   `Jaiba Knobs`, I2C slave 0x42.
2. ESP32 `BridgeTransport`: monitor both sensors through the hub; knob
   configuration screen with the fixed control drop-down.
3. Sampler third MIDI input and fixed control map.
4. Third Teensy + restored left sensor; three MIDI ports.

The 5 left-side loop pads remain a later sensor-side feature; the UI will keep
placeholders.

## Notes

- The display is a **3.3 V** system; do not feed 5 V into the I2C/UART pins.
- Power the display from its own 5 V/2 A supply; keep a common ground with the
  drum PCB but do not bridge the two 3.3 V rails.
- The I2C bus is shared with the GT911/PCA9557 and the Teensy hub; the plan
  uses status-first polling and short reads to keep the touch response
  unaffected.

## References

- [CrowPanel ESP32 HMI 7.0" wiki](https://www.elecrow.com/wiki/esp32-display-702727-intelligent-touch-screen-wi-fi26ble-800480-hmi-display.html)
- [PlatformIO/LVGL tutorial](https://www.elecrow.com/wiki/CrowPanel_ESP32_7.0-inch_with_PlatformIO.html)
- [Vendor repo](https://github.com/Elecrow-RD/CrowPanel-7.0-HMI-ESP32-Display-800x480)
