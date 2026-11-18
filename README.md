# kuru

A battery-powered, room-aware **rotary controller for Home Assistant**, running on the **Waveshare ESP32-S3-Knob-Touch-LCD-1.8**.
Turn the knob to set a value, swipe or double-tap the glass for the next device, and the screen shows what you are asking for next
to what the house has reported back.

Firmware in C on ESP-IDF 5.5 and LVGL 9, built with PlatformIO. The look is taken from a hand-built HTML prototype
([`reference/knob-ui-prototype.html`](reference/knob-ui-prototype.html), open it in a browser) that is drawn only in colours a 16-bit panel can show.

## Status

| Works on the device | Still a stand-in |
|---|---|
| Four devices: **lights** (0-100 %), **aircon** (16-30 C with explicit on/off), **fan** (speeds 0-10, spinning icon), **curtains** (0 % open ... 100 % closed, animated pleats) | **Home Assistant**: a mock backend answers every command (the MQTT link is the next milestone) |
| Encoder feel that speeds up on a fast spin and returns to fine steps when you slow down | Deep sleep and battery management (screen-off only) |
| Requested / sent / reported state model, so a late or wrong answer never moves your dial | Haptics, long-press overview, room awareness |
| Touch gestures, dim at 20 s, screen off at 30 s, wake by spin or double tap | |
| Wake and reconnect sequence: last known state muted while it connects, scan animation, landing wave, "Changed from..." | |
| **Real Wi-Fi** with credentials typed over USB, stored in flash | |

## Hardware: which board

**Only the Waveshare ESP32-S3-Knob-Touch-LCD-1.8.** Pins, the display init table and the touch and encoder drivers are specific to it.
Another ESP32-S3 board with a different display would need a new `components/board` and `components/input`; the controller, UI and
network layers do not touch hardware. Facts about the board, and which of them are verified, are in [`docs/hardware.md`](docs/hardware.md).

Needed on the computer: Python 3, [PlatformIO](https://platformio.org/) (the `pio` command), a USB-C cable. No Node, no Arduino.

## Build and flash

```sh
python3 firmware/tools/gen_assets.py        # once: renders the image assets (about 10 s; they are not in git, ~60 MB of C)
cd firmware
pio run -e knob                             # build (the first one is slow: it compiles ESP-IDF and the ~60 MB of image C)
pio run -e knob -t upload                   # flash
pio device monitor                          # logs and the `kuru>` console
```

**USB gotcha:** the USB-C plug orientation decides which of the board's two chips the cable reaches. If the board shows up as a
`usbserial` port, or flashing says "This chip is ESP32, not ESP32-S3", **flip the plug**. If the S3 still does not appear, hold its BOOT
button while plugging in. The `usbmodem` number changes after a reset, so pass `--upload-port` with the current one.

### Wi-Fi

Type the credentials into the console; they are stored in the board's flash and never in the source, a log or git.

```
kuru> wifi set MyNetwork my-password        # quote names or passwords with spaces
kuru> wifi status                           # connection, IP, access point and signal (never the password)
kuru> wifi scan                             # access points in range, 2.4 GHz only
kuru> wifi clear
```

More in [`firmware/README.md`](firmware/README.md). Credentials are stored unencrypted in the `nvs` partition: anyone holding the
device can read them. Flash encryption is not set up.

## Repository

```
reference/    the UI prototype (HTML), the design source of truth
firmware/     the ESP-IDF project: main/ and components/{board,input,controller,network,power,ui}
              tools/gen_assets.py renders the ripple, tick, arc and icon images
docs/         architecture.md (layers, state model, connection sequence), hardware.md
tests/        host tests of the controller, link and power rules: sh tests/run.sh
third_party/  licence texts of bundled material
```

The layers are kept apart on purpose: `controller` (device state, encoder feel, connection state) is pure C with no LVGL, GPIO or
network, and has the host tests; `ui` draws whatever device it is given; `network` delivers commands; `board` and `input` are the only
hardware code. See [`docs/architecture.md`](docs/architecture.md).

## What we learned

**Hardware**
- One USB-C port, two chips, chosen by the plug orientation (above). Most "the board dropped off USB" episodes were this.
- The knob has two pulse contacts, not quadrature: a detent is one event on one contact. The feel (1 % per click, up to 3x on a fast
  spin, falling back as soon as you slow down) is a smoothed click rate through a smoothstep, tuned by turning the real knob.
- The radio is 2.4 GHz only. A network that is 5 GHz only never shows up in a scan.

**Making LVGL fast on this panel** (all measured on the board)
- The software renderer is CPU-bound. A full-screen redraw cost 300 ms at 24 bit and is about 100 ms at 16 bit with flat colours
  (the design was redrawn for 16 bit: no gradients, so nothing to band).
- Each image draw costs about 0.85 ms whatever its size or format, so the work is in drawing fewer images: ripples, ticks, arc
  wedges and the curtain pleats are **pre-rendered** (`gen_assets.py`) instead of drawn with LVGL primitives.
- LVGL redraws on **any** style or position write, even with an unchanged value: every setter is guarded.
- A widget that is resized or moved invalidates its old and new area. Animating two curtain panels as image objects cost a full redraw
  per frame; drawing one image through a custom draw callback and invalidating only the strip between the old and new edge costs a few ms.
- An `lv_line` is sized from the origin to its furthest point. With screen coordinates a 12 px check mark owned a 50,000 pixel object
  and every pulse frame redrew it (about 50 ms). Use local coordinates and hide what is not showing.
- Dirty-area overflow (more than 32 areas) falls back to a full redraw: `LV_INV_BUF_SIZE=128`. LVGL's own memory pool was 4x faster
  than the system malloc. Two draw threads were slower; larger S3 caches caused a boot loop.
- While the knob turns, only cheap things change; the arc, the tick gradient and the ripples settle one after another afterwards.

**Build and configuration**
- PlatformIO keeps one `sdkconfig.<env>` per environment: a change in `sdkconfig.defaults` only applies to symbols not already in it.
- The generated asset C is large. `gen_assets.py` leaves unchanged files alone so rebuilds stay incremental; a change to `sdkconfig`
  or the component set rebuilds all of it (about 7 minutes here).
- PlatformIO checks the image against the board definition's 8 MB, not `partitions.csv`: `board_upload.maximum_size` has to match.
- Typing into the console needs the primary console on USB Serial/JTAG; as a secondary console it is output only.

**Wi-Fi and the house**
- "Join the strongest access point" is best effort: the quick scan at connect time sometimes misses one. The ESP32 does not roam, so
  to tell which floor it is on, scan and compare the signal of every known BSSID instead of trusting the connected one.
- Home Assistant counts a cover's position the other way (100 = open). The curtains here count how *closed* they are, so the future
  adapter must map `value = 100 - position`.
- The UI is deliberately snappier than the prototype's animations; the real hardware (a curtain motor) keeps its own pace and the screen
  only reflects it through the status word and the "At N %" line.

## Tests

`sh tests/run.sh` builds and runs the host tests (pure C, no board): encoder feel, the aircon, fan and curtain rules, the connection
state machine and mock network, the Wi-Fi credential rules, and screen power. `pio run -e knob_tour` builds a scripted tour that
reports frame times per phase over the serial port.

## Licence and third-party material

**No licence has been chosen for this code yet**: until a `LICENSE` file is added, all rights are reserved.

- The UI font is **Outfit**, SIL Open Font License 1.1, copyright 2021 The Outfit Project Authors; its glyphs are compiled into
  `firmware/components/ui/fonts/`. Licence text: [`third_party/Outfit-OFL.txt`](third_party/Outfit-OFL.txt).
- Dependencies are fetched by the build: [LVGL](https://lvgl.io/) (MIT), [ESP-IDF](https://github.com/espressif/esp-idf) and the Espressif
  components `esp_lvgl_port` and `esp_lcd_sh8601` (Apache-2.0, see each package's licence).
- `firmware/components/board/lcd_init_cmds.h` (the display's register init table) was taken from an unofficial mirror of Waveshare's
  demo code. Its provenance and licence have not been checked against Waveshare's own release; do that before publishing.
- Not affiliated with or endorsed by Waveshare, Espressif or Home Assistant.
