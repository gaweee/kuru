# What we learned

Notes from building kuru on the Waveshare ESP32-S3 knob: things that cost time, and what fixed them. Everything here was measured
or hit on the real board. If you are building something similar, this page is the one to read first.

## Hardware
- One USB-C port, two chips, chosen by the plug orientation (see [hardware.md](hardware.md)). Most "the board dropped off USB" episodes were this.
- The knob has two pulse contacts, not quadrature: a detent is one event on one contact. The feel (1 % per click, up to 3x on a fast
  spin, falling back as soon as you slow down) is a smoothed click rate through a smoothstep, tuned by turning the real knob.
- The radio is 2.4 GHz only. A network that is 5 GHz only never shows up in a scan.

## Making LVGL fast on this panel
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

## Build and configuration
- PlatformIO keeps one `sdkconfig.<env>` per environment: a change in `sdkconfig.defaults` only applies to symbols not already in it.
- The generated asset C is large. `gen_assets.py` leaves unchanged files alone so rebuilds stay incremental; a change to `sdkconfig`
  or the component set rebuilds all of it (about 7 minutes here).
- PlatformIO checks the image against the board definition's 8 MB, not `partitions.csv`: `board_upload.maximum_size` has to match.
- Typing into the console needs the primary console on USB Serial/JTAG; as a secondary console it is output only.

## Wi-Fi and the house
- "Join the strongest access point" is best effort: the quick scan at connect time sometimes misses one. The ESP32 does not roam, so
  to tell which floor it is on, scan and compare the signal of every known BSSID instead of trusting the connected one.
- Home Assistant counts a cover's position the other way (100 = open). The curtains here count how *closed* they are, so the future
  adapter must map `value = 100 - position`.
- The UI is deliberately snappier than the prototype's animations; the real hardware (a curtain motor) keeps its own pace and the screen
  only reflects it through the status word and the "At N %" line.
