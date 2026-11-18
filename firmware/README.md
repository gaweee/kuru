# kuru firmware (M2: Lights, Aircon, Fan, Curtains on the device)

ESP-IDF (C) + LVGL 9, built with PlatformIO (`framework = espidf`).

    python3 tools/gen_assets.py                       # once: pre-render ripple/tick/arc/icon images (stdlib only, ~10 s)
    ~/.platformio/penv/bin/pio run -e knob            # build
    ~/.platformio/penv/bin/pio run -e knob -t upload  # flash (board connected via USB)
    ~/.platformio/penv/bin/pio device monitor

- `components/board`: SH8601 QSPI display + LEDC backlight (GPIO 47) + LVGL port.
- `components/ui`: the device-agnostic screen renderer (`ui_screen.c`), per-device looks (`looks.c`) and Outfit fonts (generated once with `lv_font_conv`, committed).
- `components/controller`, `input`, `power`, `network`: behaviour, encoder/touch, screen sleep, mock Home Assistant. Host tests: `tests/run.sh`.
- Pin numbers and the SH8601 init table come from an unofficial mirror of Waveshare's demo and are NOT yet verified against Waveshare's docs.

## USB: the plug orientation picks the chip
The board has two MCUs, the ESP32-S3 (this firmware) and a plain ESP32-U4WDH, and one USB-C port. A switch connects the port to one
or the other **depending on which way the USB-C plug is inserted** (Waveshare wiki FAQ: "the two insertion orientations connect to different
download channels"). The S3 appears as `/dev/cu.usbmodem...` (Espressif, vendor 0x303A); the other chip appears as a CH340
`/dev/cu.usbserial-...`, and flashing it with the S3 firmware fails with "This chip is ESP32, not ESP32-S3". If the wrong one shows
up, flip the plug 180 degrees. If the S3 still does not appear, hold its BOOT button while plugging in (download mode).
The `usbmodem` number changes after a reset (2101 / 101): use `--upload-port` with the current name, or let PlatformIO detect it.

## Wi-Fi: setting the network name and password
The credentials are typed into the device's USB console (the same cable that flashes it) and stored in its flash. They are
never in the source, a log or git.

    ~/.platformio/penv/bin/pio device monitor        # or the PlatformIO "Monitor" button; type at the  kuru>  prompt

    kuru> wifi set MyNetwork my-password             # store and connect at once
    kuru> wifi set "My Network" "my password"        # quote names or passwords that contain spaces
    kuru> wifi set CafeGuest                         # an open network: no password
    kuru> wifi status                                # stored name, connection, IP address, signal (never the password)
    kuru> wifi scan                                  # access points in range with their BSSID and signal, strongest first
    kuru> wifi scan MyNetwork                        # only the access points that carry this name
    kuru> wifi clear                                 # forget the credentials

- The radio is **2.4 GHz only**: a 5 GHz-only network never shows up in `wifi scan`.
- WPA2 and WPA3 (or open) networks; a password is 8-63 characters, or 64 hex digits. Hidden and enterprise (802.1X) networks are not supported.
- The credentials survive reflashing (`pio run -t upload`) and power cycles. Only an erase of the whole flash removes them, or `wifi clear`.
- They are stored unencrypted in the `nvs` partition: anyone with the device and a USB cable can read them. Flash encryption is a later step.
- The screen shows the result: "Connecting" while joining, "No Wi-Fi" (and "Showing last known state") when it fails, and it retries every 4 s.
  With nothing stored it fails at once. A wrong password fails at once; an unreachable network after 10 s. `wifi status` and the log lines
  tagged `wifi` and `link` say why.
- Home Assistant is still a mock (milestone M3): after Wi-Fi is up, the "Home Assistant" and "fetch" steps are simulated.
  To simulate Wi-Fi too (no radio, no credentials), build with `-DKURU_LINK_REAL_WIFI=0` (see `mock_link.h`).

## Rendering notes (measured on the device)
- The display runs RGB565 (`reference/knob-ui-prototype.html` is drawn for it). A full-screen redraw is ~100 ms: floor 19, ripples 13, text 14, arcs 12, ticks ~41.
- LVGL's per-image draw overhead is ~0.85 ms whatever the format, so the work is in drawing fewer images, not cheaper ones.
- Ripples + baked track ring: 16 opaque 272x272 images per device. Ticks: 16 pre-coloured ARGB levels x 48. Arc: 30/5/1 degree wedges.
- The ripple level and tick gradient are frozen while the knob moves and step after it settles.
- `LV_INV_BUF_SIZE=128` (top-level CMakeLists): LVGL falls back to a full redraw when it has >32 dirty areas.
- The app partition is 14 MB (four devices of assets are ~10.5 MB). `gen_assets.py` skips unchanged files so rebuilds stay incremental.
- `pio run -e knob_perf` logs frame periods; `knob_synth` injects a synthetic spin / ablation / device switch.

## Gotcha: one sdkconfig per environment
PlatformIO keeps a separate `sdkconfig.<env>` for each environment (`knob`, `knob_perf`, `knob_synth`). Settings in
`sdkconfig.defaults` only apply when a symbol is not already in that file, so after changing the defaults either delete the
other `sdkconfig.*` files (full rebuild) or apply the same edit to each. A perf build with a stale config once crashed
on an exhausted 64 KB LVGL pool while the normal build was fine.

## Rendering experiments that did not help
- `LV_OS_FREERTOS` + `LV_DRAW_SW_DRAW_UNIT_CNT=2` (render on both cores): a full-screen redraw got slower, 430 ms vs 280 ms.
- Larger S3 instruction/data caches (`CONFIG_ESP32S3_*CACHE*`): boot loop with a reset about every 8 s. Do not re-add without testing.
- LVGL on the system `malloc` (`LV_USE_CLIB_MALLOC`): 8 us per alloc+free vs 2 us for LVGL's own pool; redraws several times slower.

## Changing sdkconfig costs a full rebuild
Adding a component or changing a setting regenerates `sdkconfig.h`, which makes every source recompile, including ~25 MB of generated asset
C (about 50 minutes). Batch such changes. (The USB console and the Wi-Fi stack were added together for that reason.)
