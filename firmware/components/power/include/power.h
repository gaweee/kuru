// Screen power state machine (pure C, host-tested): awake -> dim -> asleep by inactivity, wake on input.
// Rules are the prototype's (reference/knob-ui-prototype.html): dim after 20 s, screen off after 30 s; the input that wakes the
// screen only wakes it, and input shortly after waking is ignored too, so a spin that wakes the knob
// does not also change a value. Deliberate change from the prototype: only a spin or a double tap WAKES the
// screen; a single tap, touch or swipe does nothing while it is dim or off (it cannot be woken by accident). This is screen power only: deep sleep / power gating is milestone M4.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define POWER_DIM_AFTER_MS 20000
#define POWER_SLEEP_AFTER_MS 30000
#define POWER_DIM_WAKE_IGNORE_MS 250     // after waking from dim
#define POWER_SLEEP_WAKE_IGNORE_MS 450   // after waking from screen-off

typedef enum { POWER_AWAKE, POWER_DIM, POWER_ASLEEP } power_state_t;

typedef struct {
    power_state_t state;
    uint32_t last_input_ms;
    uint32_t ignore_until_ms;
} power_t;

typedef enum {
    POWER_INPUT_ACT,               // act on it
    POWER_INPUT_WOKE_FROM_DIM,     // it only woke the screen: do nothing else
    POWER_INPUT_WOKE_FROM_SLEEP,   // it only woke the screen: do nothing else (the caller starts a sync)
    POWER_INPUT_IGNORED,           // too soon after waking: do nothing
    POWER_INPUT_NOT_WAKING,        // the screen is dim/off and this input does not wake it: do nothing, timer untouched
} power_input_t;

void power_init(power_t *p, uint32_t now_ms);

// A user input. `wakes` says whether this kind of input may wake a dim/off screen (spin, double tap).
// While awake, every input (even one that ends up ignored right after a wake) restarts the inactivity timer.
// While dim/off, an input that does not wake leaves everything unchanged, including the timer.
power_input_t power_on_input(power_t *p, uint32_t now_ms, bool wakes);

// Advance by inactivity. Returns true if the state changed.
bool power_tick(power_t *p, uint32_t now_ms);

// Backlight target, 0..255 (prototype: dimmed = the screen drawn 38 % as bright).
uint8_t power_backlight_target(power_state_t s);
