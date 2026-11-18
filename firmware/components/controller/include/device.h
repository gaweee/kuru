// Device-agnostic control state (pure C, no hardware / UI / network dependencies; host-testable).
//
// Separates three things, as required by docs/architecture.md:
//   requested : what the knob is asking for; changes on every encoder click, never jumps back
//   pending   : a command that has been sent but not yet confirmed (-1 = none)
//   reported  : the last state reported by the backend (Home Assistant; a mock until M3)
//
// A device is described once by a `device_def_t` (range, step, feel). Everything else is generic.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define DEVICE_OFF_FRAC (-0.15f)      // ring fraction used for "off": rests just before the first tick (prototype)
#define DEVICE_SEND_AFTER_MS 600      // one command after this much stillness (prototype)
#define DEVICE_DONE_SHOW_MS 1300      // how long "Set" stays visible after confirmation (prototype)
#define DEVICE_OFF_ARM_MS 1600        // EXPLICIT power: window for the second "switch off" click
#define DEVICE_PAUSE_RESET_MS 250     // a pause this long resets the click-rate estimate
#define DEVICE_RATE_ALPHA_UP 0.3f     // click-rate estimate rises gradually...
#define DEVICE_RATE_ALPHA_DOWN 0.75f  // ...but falls fast: slowing down returns to fine steps at once

// Encoder feel. One click moves `step` units at slow speed. As the smoothed click rate rises from
// rate_low_hz to rate_high_hz the gain eases (smoothstep) from 1x up to gain_max.
typedef struct {
    float step;
    float rate_low_hz;
    float rate_high_hz;
    float gain_max;
} device_feel_t;

// How "off" works.
//   OFF_AT_MIN : value == min means off (lights, fan). `on` is derived from the value.
//   ALWAYS_ON  : there is no off; the lowest value is just that value (curtains: 0 % is fully open).
//   EXPLICIT   : power is separate from the value (aircon). Turning up from off resumes the last setpoint; at `min`
//                one more click arms "turn again to switch off", and a second click within
//                DEVICE_OFF_ARM_MS switches it off.
typedef enum { DEVICE_POWER_OFF_AT_MIN, DEVICE_POWER_EXPLICIT, DEVICE_POWER_ALWAYS_ON } device_power_t;

typedef struct {
    const char *id;
    int min, max;          // value range in device units (lights: percent; aircon: half-degrees)
    device_power_t power;
    device_feel_t feel;
    bool higher_closes;    // a motorised device whose higher value means more closed (curtains): travelling up is "Closing"
    float travel_per_s;    // devices with a motor (curtains): units per second the real mechanism travels; 0 = changes at once.
                           // Only the mock backend uses it; a real backend reports whatever the motor does.
} device_def_t;

typedef struct {
    bool on;
    int value;             // meaningful only while on
} device_val_t;

typedef struct {
    const device_def_t *def;
    device_val_t requested;
    device_val_t reported;
    device_val_t pending;     // sent but unconfirmed; valid only while has_pending
    bool has_pending;
    bool editing;             // user still adjusting / command not yet sent
    int resume_value;         // last confirmed setpoint while on (EXPLICIT power: what turning on resumes)
    int aux;                  // backend-reported side reading shown by the look (aircon: room temperature, tenths of a degree)
    uint32_t off_armed_until_ms;   // EXPLICIT power: "turn again to switch off" is armed until this time
    bool moving;              // the backend says the mechanism is travelling (curtains), towards moving_to
    int moving_to;
    // The display followed a change it did not make (set at the wall, or while the controller slept): what it showed
    // before, and when. The screen counts the number to its new value and says "Changed from ..." for a moment.
    bool has_changed;
    device_val_t changed_from;
    uint32_t changed_ms;
    uint32_t last_turn_ms;
    uint32_t send_at_ms;
    uint32_t pending_at_ms;   // when the pending command was sent
    uint32_t done_until_ms;   // "Set" is shown until this time
    float rate_hz;            // smoothed click rate
    float residual;           // sub-unit remainder carried between clicks
} device_t;

typedef enum { DEVICE_STATUS_NONE, DEVICE_STATUS_SETTING, DEVICE_STATUS_SET, DEVICE_STATUS_OPENING, DEVICE_STATUS_CLOSING } device_status_t;

void device_init(device_t *d, const device_def_t *def, device_val_t initial);

// One encoder click (dir = +1 / -1). Returns false only when already at a limit.
bool device_turn(device_t *d, int dir, uint32_t now_ms);

// Advance timers: sends the command after DEVICE_SEND_AFTER_MS of stillness.
void device_tick(device_t *d, uint32_t now_ms);

// The backend reports the device's actual state. Confirms a matching pending command; otherwise,
// if the user is idle, the display follows (e.g. the light was changed at the wall switch).
void device_on_report(device_t *d, device_val_t v, uint32_t now_ms);

// Motorised devices (curtains): the backend accepted a command and the mechanism started (or finished) travelling.
// `moving` true: the pending command is no longer "Setting" but "Opening"/"Closing" until the mechanism arrives; position
// reports in between (device_on_report) only update `reported` and never drag the requested value. `moving` false:
// it arrived; "Set" shows briefly. Also covers motion started elsewhere (a wall remote): the display follows `to`.
void device_on_motion(device_t *d, bool moving, int to, uint32_t now_ms);

// Leaving the device: send any unsent change straight away (prototype).
void device_flush(device_t *d, uint32_t now_ms);

// True while the second "switch off" click would still switch off (EXPLICIT power).
bool device_off_armed(const device_t *d, uint32_t now_ms);

// What the status line should show about this device's own commands: Setting while one is unconfirmed, then Set
// briefly; Opening / Closing while a motor travels. (The connection's words come from link.h and outrank these.)
device_status_t device_status(const device_t *d, uint32_t now_ms);

bool device_val_equal(const device_val_t *a, const device_val_t *b);     // off == off regardless of value
float device_frac(const device_def_t *def, const device_val_t *v);      // 0..1 along the range, DEVICE_OFF_FRAC when off
float device_gain_for_rate(const device_feel_t *feel, float rate_hz);
