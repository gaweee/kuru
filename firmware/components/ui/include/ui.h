// Device-agnostic knob screen: ring of ticks, requested handle, thin reported arc, value, status line, page dots.
// All functions must be called with the LVGL lock held.
#pragma once
#include "device.h"
#include "device_look.h"
#include "link.h"

void ui_init(void);                                                  // build the widgets once
void ui_set_link(const link_t *link);                                // the connection to show (before ui_set_look); NULL = always live

// Skin the screen for a device immediately (start-up). `d` is the device now on screen: its reported state
// sets the initial ripple level. `index`/`count` drive the page dots.
void ui_set_look(const device_look_t *look, int index, int count, const device_t *d);

// Switch to another device: everything snaps to the new device's state in a single redraw (no slide or sweep:
// both were measured too expensive on this panel). `dir` is kept for a future cheap transition.
void ui_switch_device(const device_look_t *look, int index, int count, const device_t *d, int dir);

void ui_render(const device_t *d, uint32_t now_ms);                  // call every frame
void ui_set_animating(bool on);                                      // false while the screen is off: stops the spinning icon
int ui_stage(void);                                                  // perf aid: 0 turning, 1 arc easing, 2 ticks, 3 ripples/idle
#ifdef KURU_PERF
void ui_perf_ablate(int step);   // perf aid
#endif
