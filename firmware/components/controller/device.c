#include "device.h"

static device_val_t normalised(const device_def_t *def, device_val_t v)
{
    if (def->power == DEVICE_POWER_OFF_AT_MIN) v.on = v.value > def->min;
    else if (def->power == DEVICE_POWER_ALWAYS_ON) v.on = true;
    return v;
}

void device_init(device_t *d, const device_def_t *def, device_val_t initial)
{
    initial = normalised(def, initial);
    *d = (device_t){ .def = def, .requested = initial, .reported = initial, .resume_value = initial.value };
}

bool device_val_equal(const device_val_t *a, const device_val_t *b)
{
    return a->on == b->on && (!a->on || a->value == b->value);
}

float device_frac(const device_def_t *def, const device_val_t *v)
{
    if (!v->on) return DEVICE_OFF_FRAC;
    return (float)(v->value - def->min) / (float)(def->max - def->min);
}

static float smoothstep(float x) { x = x < 0 ? 0 : x > 1 ? 1 : x; return x * x * (3 - 2 * x); }

float device_gain_for_rate(const device_feel_t *f, float rate_hz)
{
    return 1.0f + (f->gain_max - 1.0f) * smoothstep((rate_hz - f->rate_low_hz) / (f->rate_high_hz - f->rate_low_hz));
}

static void begin_edit(device_t *d, uint32_t now_ms)
{
    d->editing = true;
    d->done_until_ms = 0;
    d->send_at_ms = now_ms + DEVICE_SEND_AFTER_MS;
}

// EXPLICIT power: the prototype's aircon rules.
static bool turn_explicit(device_t *d, int dir, int whole, uint32_t now_ms)
{
    const device_def_t *def = d->def;
    if (!d->requested.on) {
        if (dir < 0) return false;                         // already off; turning down does nothing
        d->requested.on = true;                            // one click up resumes the last setpoint
        d->requested.value = d->resume_value < def->min ? def->min : d->resume_value > def->max ? def->max : d->resume_value;
        begin_edit(d, now_ms);
        return true;
    }
    if (dir > 0) d->off_armed_until_ms = 0;
    int nv = d->requested.value + whole;
    if (nv > def->max) nv = def->max;
    if (nv < def->min) {
        if (d->requested.value > def->min) {
            nv = def->min;                                 // stop at the floor first
        } else if (d->off_armed_until_ms && (int32_t)(now_ms - d->off_armed_until_ms) < 0) {
            d->requested.on = false;                       // second click within the window: switch off
            d->off_armed_until_ms = 0;
            begin_edit(d, now_ms);
            return true;
        } else {
            d->off_armed_until_ms = now_ms + DEVICE_OFF_ARM_MS;   // first extra click: arm, and bump
            return false;
        }
    }
    if (nv == d->requested.value) { d->residual = 0; return false; }
    d->requested.value = nv;
    begin_edit(d, now_ms);
    return true;
}

bool device_turn(device_t *d, int dir, uint32_t now_ms)
{
    const device_feel_t *f = &d->def->feel;
    const uint32_t dt = now_ms - d->last_turn_ms;
    d->last_turn_ms = now_ms;
    if (dt > DEVICE_PAUSE_RESET_MS) {
        d->rate_hz = 0;
        d->residual = 0;
    } else {
        const float inst = 1000.0f / (float)(dt < 5 ? 5 : dt);
        d->rate_hz += (inst - d->rate_hz) * (inst > d->rate_hz ? DEVICE_RATE_ALPHA_UP : DEVICE_RATE_ALPHA_DOWN);
    }

    d->residual += dir * f->step * device_gain_for_rate(f, d->rate_hz);
    const int whole = (int)d->residual;            // truncates toward zero
    d->residual -= (float)whole;
    if (whole == 0 && !(d->def->power == DEVICE_POWER_EXPLICIT && !d->requested.on)) return true;   // still accumulating

    if (d->def->power == DEVICE_POWER_EXPLICIT) return turn_explicit(d, dir, whole, now_ms);

    int nv = d->requested.value + whole;
    if (nv < d->def->min) nv = d->def->min;
    if (nv > d->def->max) nv = d->def->max;
    if (nv == d->requested.value) { d->residual = 0; return false; }
    d->requested = normalised(d->def, (device_val_t){ .on = true, .value = nv });
    begin_edit(d, now_ms);
    return true;
}

void device_tick(device_t *d, uint32_t now_ms)
{
    if (d->editing && (int32_t)(now_ms - d->send_at_ms) >= 0) {
        d->editing = false;
        const device_val_t baseline = d->has_pending ? d->pending : d->reported;   // compare with what is already in flight
        if (!device_val_equal(&d->requested, &baseline)) {
            d->pending = d->requested;               // "send": the network layer picks this up
            d->has_pending = true;
            d->pending_at_ms = now_ms;
        }
    }
}

void device_flush(device_t *d, uint32_t now_ms)
{
    if (!d->editing) return;
    d->send_at_ms = now_ms;
    device_tick(d, now_ms);
}

void device_on_report(device_t *d, device_val_t v, uint32_t now_ms)
{
    v = normalised(d->def, v);
    d->reported = v;
    if (v.on) d->resume_value = v.value;
    if (d->has_pending && device_val_equal(&d->pending, &v)) {
        d->has_pending = false;
        if (!d->editing) d->done_until_ms = now_ms + DEVICE_DONE_SHOW_MS;
    } else if (!d->has_pending && !d->editing && !d->moving) {
        if (!device_val_equal(&d->requested, &v)) {  // changed elsewhere: the display follows, and remembers what it was
            d->has_changed = true;
            d->changed_from = d->requested;
            d->changed_ms = now_ms;
        }
        d->requested = v;                            // (not mid-travel: that is just progress)
    }
}

void device_on_motion(device_t *d, bool moving, int to, uint32_t now_ms)
{
    d->moving = moving;
    d->moving_to = to;
    if (moving) {
        if (d->has_pending && d->pending.on && d->pending.value == to) d->has_pending = false;   // accepted: no longer "Setting"
        else if (!d->has_pending && !d->editing) d->requested = (device_val_t){ true, to };      // started elsewhere
    } else if (!d->has_pending && !d->editing) {
        d->requested = d->reported;
        d->done_until_ms = now_ms + DEVICE_DONE_SHOW_MS;
    }
}

bool device_off_armed(const device_t *d, uint32_t now_ms)
{
    return d->off_armed_until_ms && (int32_t)(now_ms - d->off_armed_until_ms) < 0;
}

device_status_t device_status(const device_t *d, uint32_t now_ms)
{
    if (d->editing) return DEVICE_STATUS_NONE;
    if (d->has_pending) return DEVICE_STATUS_SETTING;
    if (d->moving) {
        const bool up = d->moving_to > d->reported.value;
        return up != d->def->higher_closes ? DEVICE_STATUS_OPENING : DEVICE_STATUS_CLOSING;
    }
    if (d->done_until_ms && (int32_t)(now_ms - d->done_until_ms) < 0) return DEVICE_STATUS_SET;
    return DEVICE_STATUS_NONE;
}
