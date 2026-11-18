# kuru architecture

Status: firmware milestone M2 in progress: interactive Lights, Aircon, Fan and Curtains screens on the real device, with touch
navigation and screen sleep. Design source of truth: `reference/knob-ui-prototype.html` (16-bit colour). Facts about the
hardware are tracked separately in `docs/hardware.md` (not written yet; see "Hardware facts" below).

## Layers

```
 encoder ──► main ──► controller (device_t) ◄── network (mock_ha → MQTT in M3)
                 │            │
                 └────────► ui (screen renderer + look)
                              │
                            board (display, backlight, LVGL port)
```

| Component | Knows about | Must not know about |
|---|---|---|
| `controller` | ranges, steps, feel, requested/pending/reported state, status sequence | LVGL, GPIO, MQTT |
| `network` | how to deliver a pending command and report back, and how to connect (`mock_ha.c`, `mock_link.c` for now) | LVGL, GPIO |
| `ui` | how a device *looks* (accent, icon, text, ripples, tick layout) and how to draw the ring | device behaviour, MQTT |
| `input` | the two encoder contacts, the touch panel, the BOOT button | everything else |
| `power` | when the screen dims, sleeps and wakes (pure rules) | LVGL, GPIO |
| `board` | SH8601 display, backlight, LVGL port | everything else |
| `main` | wiring: which devices exist, frame loop | — |

`controller` is pure C and is tested on the host: `tests/run.sh`.

## The state model

* **requested**: what the knob asks for. Changes on every click; never moves backwards because of the network.
* **pending**: a command sent but not yet confirmed (`-1` = none).
* **reported**: what the backend last reported. `device_on_report()` is the only way it changes.

Sequence for one adjustment: clicks (editing, no status) → 0.6 s of stillness → command sent (**Setting**) →
report matches → **Set** for 1.3 s. A report that arrives while the user is editing again never moves the target
and never flashes "Set". An idle display follows an external change (wall switch).

Motorised devices (curtains) add one more phase. The backend accepts the command and the mechanism starts to travel:
`device_on_motion(true, to)` turns **Setting** into **Opening** / **Closing** (the curtains count how *closed* they are, 0 % = open, 100 % = closed, so a higher value is Closing; `higher_closes` in the definition. A Home Assistant cover counts the other way: the M3 adapter maps value = 100 - position), position reports during the travel only
update `reported` (they never drag `requested`; the line under the number shows "At N%"), and `device_on_motion(false)`
means it arrived: **Set** for 1.3 s. Motion started elsewhere (a wall remote) is followed. The real motor takes its own
time; `mock_ha.c` simulates one from `device_def_t.travel_per_s` (curtains: 14 % per second, the prototype's pace).

The network layer's contract (also for the future MQTT adapter): take `device.pending` / `pending_at_ms`, deliver it,
and later call `device_on_report(d, actual_value, now)`.

## Interaction rules

| Input | Awake | Dim / asleep |
|---|---|---|
| spin | adjusts the current device | wakes the screen; does nothing else |
| double tap | next device (BOOT button does the same) | wakes the screen; does nothing else |
| swipe left / right | next / previous device | nothing (cannot wake) |
| single tap, touch | nothing yet (reserved for confirm and the overview) | nothing (cannot wake) |

Screen power (`components/power`, host-tested): dim to 38 % at 20 s idle, screen off at 30 s. Any input while awake
restarts the timer; a non-waking touch while dim/off does not. Input within 450 ms of waking from off (250 ms from
dim) is ignored, so a spin that wakes the knob does not also change a value. On sleep any unsent change is sent first.
On waking from off (and at boot) the connection is rebuilt, see "Connection" below: cached state is never treated as live.
This is screen power only: the CPU keeps running. Deep sleep and power gating are milestone M4.

## Connection (the wake sequence)

Design source: `reference/knob-ui-prototype.html`, "Waking up". `controller/link.c` holds the state (pure C, host-tested):

```
 SLEEP -> WIFI -> HA -> FETCH -> LIVE        a failed step says which one failed and retries every 4 s:
          |       |                          WIFI -> NO_WIFI,  HA -> NO_HA,  then back to WIFI
          v       v
       NO_WIFI  NO_HA
```

| State | Ring | Number | Line under it | Status line |
|---|---|---|---|---|
| Wi-Fi / HA / fetch | last known state, muted (30 %), a white scan (Wi-Fi) or accent scan (HA, fetch) runs along the ticks | grey | "Last known" after 0.6 s | "Connecting" (Wi-Fi icon) / "Syncing", only once a step took longer than 250 ms |
| No Wi-Fi | muted, no scan | grey | "Showing last known state" | "No Wi-Fi" (red) |
| HA down | muted, no scan | grey | "Can't reach Home Assistant" (red) | "Offline" (red) |
| going live | a wave lights the ring from the start, the handle pops, the thin arc and ripples draw back in, curtain panels slide in | normal; a value that changed while away counts to its new value (0.65 s) | "Changed from 60%" for 2.8 s | "Up to date" for 1.3 s |

Not live, the thin arc, the ripples and the curtain pleats are hidden (they are what Home Assistant *reports*, and nothing is known yet).
A change the user makes before it is live is queued: its ticks show at full brightness, the line says "Sends when connected", and
`mock_ha.c` delivers it once the link is live (its latency counts from then).

**Real Wi-Fi** (`network/wifi_link.c`) now drives the Wi-Fi step; `mock_link.c` plays only Home Assistant and the state fetch until
M3 (`KURU_LINK_REAL_WIFI`, default on). Credentials come from the USB console (`network/kuru_console.c`, commands in `firmware/README.md`)
and live in NVS. A wrong password fails the step at once, an unreachable network after 10 s, no credentials at once; a connection that
drops while up is `link_lost()`; typing new credentials restarts the attempt immediately. The radio stays associated while the screen
sleeps. Console input needs the primary console on USB Serial/JTAG (`sdkconfig.defaults`).

**Which access point (floor awareness, M5).** Several access points can share one network name (a mesh, one per floor); the BSSID tells
them apart. The adapter asks to join the *strongest* one (`WIFI_ALL_CHANNEL_SCAN` + sort by signal; the ESP32 default is the first one
found), but that is best effort: the quick scan done at connect time sometimes misses an AP, and in 4 test boots at one spot it joined the
strongest AP twice and a 15-20 dB weaker one twice (a standalone `wifi scan` right after saw all 4 APs in 3 of 4 runs).
`wifi_link_ap()` returns the BSSID, signal and channel of the AP it is on, and `wifi status`, `wifi scan [name]` and the connect log line
print them. The ESP32 also does not roam on its own. So for room or floor detection do not use the connected AP: scan, keep a smoothed
signal per known BSSID (several scans, so one miss does not matter), and take the strongest, with a table of BSSID -> floor stored in NVS.

**Wiring the rest of the network** (Home Assistant over MQTT) replaces `mock_link.c`; nothing else changes. Whatever connects watches `link->attempt` (it
changes at every wake and every retry) and drives the state:

1. state `WIFI`: join Wi-Fi, then `link_on_wifi(l, ok, now)` (a timeout is `false`);
2. state `HA`: reach Home Assistant, then `link_on_ha(l, ok, now)`;
3. state `FETCH`: fetch every device's state, call `device_on_report()` for each (a motor's travel: `device_on_motion()`), then
   `link_on_state(l, now)`;
4. a connection that drops while live: `link_lost(l, wifi, now)`.

`main.c` logs every transition (`link: wifi -> ha after 812 ms (attempt 1)`): with real hardware that log is the minimal Wi-Fi
test. The mock plays the prototype's four scenarios (`KURU_LINK_SCENARIO` in `mock_link.h`: `MOCK_LINK_FAST`,
`MOCK_LINK_RECONNECT` (default), `MOCK_LINK_NO_WIFI`, `MOCK_LINK_HA_DOWN`) and, while the screen sleeps, changes one device at
"home", so the "Changed from" count-up shows too. Rendering uses a shared white tick ramp (`ticks_ink`) for the Wi-Fi scan and
the landing wave, and a Wi-Fi icon split into a dot and three arcs that pulse in turn.

## Adding a device (lights, aircon, fan and curtains exist)

1. **Behaviour** — `controller/devices.c` + `devices.h`: a `device_def_t` (range, power model, feel).
   * Values are integers in the device's smallest unit (aircon: half-degrees; fan: speeds 0..10).
   * `DEVICE_POWER_OFF_AT_MIN` fits lights and fan (value == min means off). `DEVICE_POWER_EXPLICIT` is aircon's model:
     resume the last setpoint, "turn again at 16° to switch off".
   * `DEVICE_POWER_ALWAYS_ON`: no off state (curtains: 0 % is fully open).
   * Feel: `gain_max = 1.0` means no acceleration (the fan).
   * `travel_per_s > 0`: a motorised device; the mock backend simulates the travel (see the state model).
   * Add host tests in `tests/controller/` and register them in `tests/run.sh`.
2. **Look** — `ui/looks.c` + `looks.h`: a `device_look_t` (name, accent, labels, ripple curve, `value_text`,
   `short_text`, `context`). Two layout switches: `tick_groups` (fan: 10 groups of 4-5 ticks whose last is a gap, a speed
   fills a group) and `spins` (the icon turns at 2.2 / value seconds per turn), and `pleats` (curtains: the closed-curtains scene).
3. **Assets** — `tools/gen_assets.py`: one entry in `DEVICES` (accent, icon polylines, stroke). Run
   `python3 tools/gen_assets.py`; it regenerates ripples, ticks, arc wedges, icon, tables, `assets.h`, `assets.cmake`.
   Each device costs ~3 MB of flash (16 ripple images of 148 KB, 16 tick colour levels, arc wedges); the curtains need
   one ripple image and a pleat image instead. The script leaves identical files untouched, so a rebuild only
   recompiles what changed (the generated C is ~25 MB; a full compile takes tens of minutes).
4. **Register** — `main/main.c`: add to `DEFS`, `LOOKS`, `INITIAL`, `INITIAL_AUX`, bump `NUM_DEVICES`.

The app partition is 14 MB (`firmware/partitions.csv`); four devices use ~10.5 MB.

## Rendering rules (measured on the device; see `firmware/README.md`)

The LVGL software renderer is CPU-bound here, so the screen is built to avoid expensive redraws. The display runs at
RGB565 and every colour is snapped to that palette, as in the prototype.

* The flat background, the nine ripple rings and the static track ring are one pre-rendered opaque image per fill
  level (16 per device). Ticks are pre-coloured ARGB images (16 brightness levels x 48 ticks per device) because a
  tinted mask costs ~0.85 ms per draw. The thin reported arc is pre-baked wedges (30, 5 and 1 degree: at most 17 images).
* Every widget setter is guarded; LVGL redraws on any write, even an unchanged value.
* While the knob moves only cheap things change (ticks, handle, number). After it settles, the expensive ones run in
  order: reported arc eases, tick gradient re-normalises, ripples step.
* A full-screen redraw is ~100 ms (24-bit with scene images was 165 ms). Switching device is one redraw, no slide.
* LVGL uses its own pool allocator (112 KB). The system `malloc` was 4x slower.
* **Curtains animation.** The pleat panels are one pre-rendered opaque image drawn in two clipped bands (custom draw in
  `ui_screen.c`). A width change invalidates only the sliver between the old and new edge, so an animation frame is a
  few ms; resizing two image objects would redraw both whole panels every frame. The panels, the thin arc and its
  dot share one eased value (`s_act`, ~a quarter of a second) aimed at where the motor was sent, so they stay in sync
  (the prototype follows the motor's own pace, 14 %/s). The status line and "At N%" show the motor's real progress.
* The fan icon rotates while the reported speed is above zero (at most every 30 ms, 3 degree steps) and stops while the
  screen is asleep (`ui_set_animating`).

## Hardware facts

Verified on the device so far: ESP32-S3 with 8 MB octal PSRAM (80 MHz), 16 MB flash (Boya), SH8601 panel accepting
RGB565 over QSPI, the two encoder contacts working as pulse inputs on GPIO 8 / 7, CST816 touch at I2C 0x15. Everything else in
`firmware/components/board` (pin numbers, display init table) comes from an unofficial mirror of Waveshare's demo and
has not been checked against Waveshare's own documentation. Verified from the Waveshare wiki and on the device: the board has a second MCU (ESP32-U4WDH) and the USB-C plug orientation selects
which of the two the cable reaches (see `firmware/README.md`). Not yet investigated: haptics, battery, power gating, deep sleep, what the
second MCU is for.

## Known limitations

* Real Wi-Fi and the console are not tested against a network yet. Home Assistant is a mock. Credentials are stored unencrypted.
* The wake animation has not been seen on the board yet, and its frame cost is not measured (the scan changes ~15 tick images
  per frame). The prototype's slide-in of the text on wake is not built.
* No overview, haptics, or error states ("Not set"). The network is a mock (`mock_ha.c`).
* Screen power only: the CPU keeps running (deep sleep is M4). Whether GPIO 47 really dims the backlight is unverified.
* Ripples and tick levels are 16 steps; the ripple step is a full-screen redraw after you stop turning.
* The tick gradient re-normalises once when a turn settles (a slight brightness shift).
* Nothing is committed to git yet.
