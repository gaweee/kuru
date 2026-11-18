#include "devices.h"

// Lights: brightness in whole percent. Feel tuned on the device (see tests/controller/feel.c):
// 1% per click when slow, up to 3x on a hard spin; 0->100% takes roughly 2.3 knob turns.
const device_def_t DEVICE_LIGHTS = {
    .id = "lights",
    .min = 0,
    .max = 100,
    .power = DEVICE_POWER_OFF_AT_MIN,
    .feel = { .step = 1.0f, .rate_low_hz = 8.0f, .rate_high_hz = 36.0f, .gain_max = 3.0f },
};

// Fan: speed 0..10 (0 = off), one step per click and no acceleration (prototype: "fan speeds move one at a time").
const device_def_t DEVICE_FAN = {
    .id = "fan",
    .min = 0,
    .max = 10,
    .power = DEVICE_POWER_OFF_AT_MIN,
    .feel = { .step = 1.0f, .rate_low_hz = 8.0f, .rate_high_hz = 36.0f, .gain_max = 1.0f },
};

// Curtains: how closed they are, in whole percent: 0 = fully open, 100 = fully closed. (A Home Assistant cover counts the
// other way, position 100 = open: the future adapter maps value = 100 - position.) Always on. Same feel as the lights. The real motor takes
// its own time (prototype mock: 14 % per second); the screen does not imitate that pace, see ui_screen.c.
const device_def_t DEVICE_CURTAINS = {
    .id = "curtains",
    .min = 0,
    .max = 100,
    .power = DEVICE_POWER_ALWAYS_ON,
    .feel = { .step = 1.0f, .rate_low_hz = 8.0f, .rate_high_hz = 36.0f, .gain_max = 3.0f },
    .higher_closes = true,
    .travel_per_s = 14.0f,
};

// Aircon: setpoint in half-degrees, 16.0..30.0 C. Power is explicit (see device_power_t).
// Prototype: 0.5 degree per click, doubled on a fast spin. Feel is a starting point, to be tuned on the device.
const device_def_t DEVICE_AIRCON = {
    .id = "aircon",
    .min = 32,
    .max = 60,
    .power = DEVICE_POWER_EXPLICIT,
    .feel = { .step = 1.0f, .rate_low_hz = 12.0f, .rate_high_hz = 30.0f, .gain_max = 2.0f },
};
