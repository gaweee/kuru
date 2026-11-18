#include "looks.h"
#include <stdio.h>
#include "assets.h"

// ---- Lights ----
static float lights_ripple(float a) { return 0.15f + 0.85f * a; }       // prototype (mockup-2): 0.15 + 0.85 * a

static void lights_value_text(const device_val_t *v, char *num, size_t nn, char *unit, size_t un)
{
    if (v->on) { snprintf(num, nn, "%d", v->value); snprintf(unit, un, "%%"); }
    else { snprintf(num, nn, "Off"); unit[0] = 0; }
}

static void lights_short_text(const device_val_t *v, char *out, size_t n)
{
    if (v->on) snprintf(out, n, "%d%%", v->value); else snprintf(out, n, "off");
}

static void lights_context(const device_t *d, char *out, size_t n)
{
    snprintf(out, n, "%s", d->requested.on ? "Brightness" : "Turn to switch on");
}

const device_look_t LOOK_LIGHTS = {
    .name = "Lights",
    .accent = 0xFFC766,
    .min_label = "Off",
    .max_label = "100%",
    .icon = &img_icon_lights,
    .ticks = ticks_lights,
    .ripples = ripples_lights,
    .arc_a = arc_lights_a, .arc_b = arc_lights_b, .arc_c = arc_lights_c,
    .ripple = lights_ripple,
    .value_text = lights_value_text,
    .short_text = lights_short_text,
    .context = lights_context,
    .off_hint = NULL,
};

// ---- Aircon: setpoint in half-degrees (see DEVICE_AIRCON) ----
#define DEG "\xC2\xB0"

static void fmt_deg(int half, char *out, size_t n)       // "24" or "24.5"
{
    if (half % 2) snprintf(out, n, "%d.5", half / 2); else snprintf(out, n, "%d", half / 2);
}

static float aircon_ripple(float a) { return 0.25f + 0.75f * (1.0f - a); }   // prototype (mockup-2): fuller when colder

static void aircon_value_text(const device_val_t *v, char *num, size_t nn, char *unit, size_t un)
{
    if (v->on) { fmt_deg(v->value, num, nn); snprintf(unit, un, DEG); }
    else { snprintf(num, nn, "Off"); unit[0] = 0; }
}

static void aircon_short_text(const device_val_t *v, char *out, size_t n)
{
    if (v->on) { char t[16]; fmt_deg(v->value, t, sizeof t); snprintf(out, n, "%s" DEG, t); }
    else snprintf(out, n, "off");
}

static void aircon_context(const device_t *d, char *out, size_t n)
{
    if (d->requested.on) snprintf(out, n, "Room %d.%d" DEG, d->aux / 10, d->aux % 10);
    else { char t[16]; fmt_deg(d->resume_value, t, sizeof t); snprintf(out, n, "Turn to resume %s" DEG, t); }
}

const device_look_t LOOK_AIRCON = {
    .name = "Aircon",
    .accent = 0x8FD9D2,
    .min_label = "16" DEG,
    .max_label = "30" DEG,
    .icon = &img_icon_aircon,
    .ticks = ticks_aircon,
    .ripples = ripples_aircon,
    .arc_a = arc_aircon_a, .arc_b = arc_aircon_b, .arc_c = arc_aircon_c,
    .ripple = aircon_ripple,
    .value_text = aircon_value_text,
    .short_text = aircon_short_text,
    .context = aircon_context,
    .off_hint = "Turn again to switch off",
};

// ---- Fan: speed 0..10 (see DEVICE_FAN) ----
static float fan_ripple(float a) { return 0.2f + 0.8f * a; }               // prototype (mockup-2)

static void fan_value_text(const device_val_t *v, char *num, size_t nn, char *unit, size_t un)
{
    if (v->on) snprintf(num, nn, "%d", v->value); else snprintf(num, nn, "Off");
    unit[0] = 0;
}

static void fan_short_text(const device_val_t *v, char *out, size_t n)
{
    if (v->on) snprintf(out, n, "speed %d", v->value); else snprintf(out, n, "off");
}

static void fan_context(const device_t *d, char *out, size_t n)
{
    snprintf(out, n, "%s", d->requested.on ? "Speed" : "Turn to start");
}

const device_look_t LOOK_FAN = {
    .name = "Fan",
    .accent = 0xB4D48F,
    .min_label = "Off",
    .max_label = "10",
    .icon = &img_icon_fan,
    .ticks = ticks_fan,
    .ripples = ripples_fan,
    .arc_a = arc_fan_a, .arc_b = arc_fan_b, .arc_c = arc_fan_c,
    .tick_groups = 10,
    .spins = true,
    .ripple = fan_ripple,
    .value_text = fan_value_text,
    .short_text = fan_short_text,
    .context = fan_context,
    .off_hint = NULL,
};

// ---- Curtains: how closed, in percent: 0 = fully open, 100 = fully closed (see DEVICE_CURTAINS) ----
static float curtains_ripple(float a) { (void)a; return 0.0f; }             // no ripples: the pleats are the ambience

static void curtains_value_text(const device_val_t *v, char *num, size_t nn, char *unit, size_t un)
{
    snprintf(num, nn, "%d", v->value);
    snprintf(unit, un, "%%");
}

static void curtains_short_text(const device_val_t *v, char *out, size_t n)
{
    if (v->value >= 100) snprintf(out, n, "closed"); else snprintf(out, n, "%d%%", v->value);
}

static void curtains_context(const device_t *d, char *out, size_t n)
{
    if (d->moving) snprintf(out, n, "At %d%%", d->reported.value);            // the motor's real progress
    else if (d->requested.value >= 100) snprintf(out, n, "Closed");
    else if (d->requested.value <= 0) snprintf(out, n, "Fully open");
    else snprintf(out, n, "Open");
}

const device_look_t LOOK_CURTAINS = {
    .name = "Curtains",
    .accent = 0xF3AF8F,
    .min_label = "Open",
    .max_label = "Closed",
    .icon = &img_icon_curtains,
    .ticks = ticks_curtains,
    .ripples = ripples_curtains,
    .arc_a = arc_curtains_a, .arc_b = arc_curtains_b, .arc_c = arc_curtains_c,
    .pleats = &pleats_curtains,
    .ripple = curtains_ripple,
    .value_text = curtains_value_text,
    .short_text = curtains_short_text,
    .context = curtains_context,
    .off_hint = NULL,
};
