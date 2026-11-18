// How a device looks. The screen renderer (ui_screen.c) is device-agnostic and draws whatever look it is given.
// Behaviour (range, step, feel) lives in the controller's device_def_t; this holds presentation only.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"
#include "device.h"

typedef struct {
    const char *name;                       // "Lights"
    uint32_t accent;                        // 0xRRGGBB
    const char *min_label, *max_label;      // under the ring: "Off" / "100%"
    const lv_image_dsc_t *icon;             // 30x30 A8 mask, tinted with the accent
    const lv_image_dsc_t *const *ticks;     // 48 ticks x ASSET_TICK_LEVELS colour levels, flat [level * 48 + tick]
    const lv_image_dsc_t *const *ripples;   // ASSET_RIPPLE_LEVELS opaque ripple images, fullest last
    // The thin reported-state arc in this device's accent: 9 x 30 deg, 54 x 5 deg, 270 x 1 deg wedges.
    const lv_image_dsc_t *const *arc_a, *const *arc_b, *const *arc_c;

    // Ring layout. 0: one tick per value step (lights, aircon). N > 0: the ring is split into N groups of ticks (evenly,
    // rounded: 4 or 5 each for 10 groups of 48 ticks), the last tick of each group is a gap, and a value step fills
    // a whole group (the fan: 10 speeds = 10 groups).
    int tick_groups;
    // The icon turns while the reported value is above zero, at (2.2 / value) seconds per turn (the fan).
    bool spins;

    // Curtains: the scene with both panels closed (RGB565, same size and place as a ripple image). The screen draws it
    // in a band at each side, and the bands grow as the position closes. NULL for every other device.
    const lv_image_dsc_t *pleats;

    // How full the ripples are for a reported position a = 0..1 (only called while the device is on): 0..1.
    float (*ripple)(float a);
    // The big value, e.g. "60" + "%", "24.5" + "°", or "Off" + "". `v` is in device units.
    void (*value_text)(const device_val_t *v, char *num, size_t num_n, char *unit, size_t unit_n);
    // Short form used after "From ", e.g. "60%" or "off".
    void (*short_text)(const device_val_t *v, char *out, size_t n);
    // Secondary line when idle, e.g. "Brightness", "Room 27.4°", "Turn to resume 24°".
    void (*context)(const device_t *d, char *out, size_t n);
    // Shown while "turn again to switch off" is armed (devices with explicit power); NULL otherwise.
    const char *off_hint;
} device_look_t;
