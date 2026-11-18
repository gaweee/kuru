#include "mock_ha.h"
#include <math.h>

// Motor simulation for devices with travel_per_s > 0 (curtains): the command is accepted after the usual latency, then
// the position is reported while the mechanism travels, and finally "arrived". A real backend does exactly this by
// itself (Home Assistant cover: opening / closing with current_position), so none of it carries over to M3.
typedef struct { const device_t *d; float pos; uint32_t last_ms; } motor_t;

static motor_t *motor_of(const device_t *d)
{
    static motor_t m[8];
    for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++) {
        if (m[i].d == d) return &m[i];
        if (!m[i].d) { m[i].d = d; return &m[i]; }
    }
    return &m[0];
}

void mock_ha_tick(device_t *d, const link_t *link, uint32_t now_ms)
{
    const bool live = link_live(link);
    uint32_t sent_at = d->pending_at_ms;
    if (link && (int32_t)(link->live_at_ms - sent_at) > 0) sent_at = link->live_at_ms;      // queued while offline
    if (live && d->has_pending && (int32_t)(now_ms - sent_at) >= MOCK_HA_LATENCY_MS) {
        if (d->def->travel_per_s > 0 && d->pending.on && (d->moving || d->pending.value != d->reported.value)) {
            motor_t *m = motor_of(d);
            if (!d->moving) m->pos = (float)d->reported.value;
            m->last_ms = now_ms;
            device_on_motion(d, true, d->pending.value, now_ms);     // accepted: the motor starts (or is re-aimed)
        } else {
            device_on_report(d, d->pending, now_ms);                 // the mock always confirms, after a fixed delay
        }
    }
    if (d->moving) {
        motor_t *m = motor_of(d);
        const float step = d->def->travel_per_s * (float)(now_ms - m->last_ms) / 1000.0f;
        m->last_ms = now_ms;
        const float to = (float)d->moving_to;
        m->pos = fabsf(to - m->pos) <= step ? to : m->pos + (to > m->pos ? step : -step);
        const int ip = (int)(m->pos + 0.5f);
        if (ip != d->reported.value) device_on_report(d, (device_val_t){ true, ip }, now_ms);
        if (m->pos == to) device_on_motion(d, false, d->moving_to, now_ms);
    }
}
