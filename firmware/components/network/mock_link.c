#include "mock_link.h"

static uint32_t rnd(mock_link_t *m)
{
    m->rng ^= m->rng << 13; m->rng ^= m->rng >> 17; m->rng ^= m->rng << 5;       // xorshift32
    return m->rng;
}

static uint32_t between(mock_link_t *m, uint32_t lo, uint32_t hi) { return lo + rnd(m) % (hi - lo + 1); }

void mock_link_init(mock_link_t *m, mock_link_scenario_t scenario, bool change_while_asleep, uint32_t seed)
{
    *m = (mock_link_t){ .scenario = scenario, .change_while_asleep = change_while_asleep, .rng = seed ? seed : 1, .remote_dev = -1 };
}

void mock_link_set_scenario(mock_link_t *m, mock_link_scenario_t scenario) { m->scenario = scenario; }
void mock_link_use_real_wifi(mock_link_t *m, bool real) { m->wifi_is_real = real; }

// While the screen sleeps something changes at home (prototype: one of lights / curtains / fan, and the room cools).
static void plan_remote_change(mock_link_t *m, const device_t *devs, int count)
{
    static const int pct[] = { 0, 20, 40, 80, 100 };
    m->remote_dev = -1;
    for (int tries = 0; tries < 8 && m->remote_dev < 0; tries++) {
        const int i = (int)(rnd(m) % (uint32_t)count);
        const device_def_t *def = devs[i].def;
        if (def->power == DEVICE_POWER_EXPLICIT) continue;                       // aircon: its room temperature changes instead
        const int v = def->min + (def->max - def->min) * pct[rnd(m) % 5] / 100;
        if (devs[i].reported.on && devs[i].reported.value == v) continue;
        m->remote_dev = i;
        m->remote_value = v;
    }
}

static void apply_remote_change(mock_link_t *m, device_t *devs, int count, uint32_t now_ms)
{
    if (m->remote_dev >= 0) device_on_report(&devs[m->remote_dev], (device_val_t){ true, m->remote_value }, now_ms);
    m->remote_dev = -1;
    for (int i = 0; i < count; i++)                                              // aircon: the room cooled by 0.4 - 1.6 C
        if (devs[i].def->power == DEVICE_POWER_EXPLICIT && devs[i].reported.on && devs[i].aux > 200)
            devs[i].aux -= (int)between(m, 4, 16);
}

void mock_link_tick(mock_link_t *m, link_t *link, device_t *devs, int count, uint32_t now_ms)
{
    if (link->state == LINK_SLEEP) {
        if (!m->asleep) {
            m->asleep = true;
            m->stage = 0;
            if (m->change_while_asleep) plan_remote_change(m, devs, count);
        }
        return;
    }
    m->asleep = false;

    if (m->wifi_is_real) {                                                       // the Wi-Fi step is not ours
        if (link->attempt != m->seen_attempt) { m->seen_attempt = link->attempt; m->stage = 0; }
        if (link->state == LINK_HA && m->ha_attempt != link->attempt) {          // Wi-Fi is up: reach Home Assistant
            m->ha_attempt = link->attempt;
            m->stage = 2;
            m->stage_due_ms = now_ms + (m->scenario == MOCK_LINK_HA_DOWN ? 3500
                                        : m->scenario == MOCK_LINK_RECONNECT ? between(m, 180, 380) : between(m, 90, 170));
        }
    } else if (link->attempt != m->seen_attempt) {                               // a wake or a retry: connect afresh
        m->seen_attempt = link->attempt;
        m->stage = 1;
        m->stage_due_ms = now_ms + (m->scenario == MOCK_LINK_NO_WIFI ? 5000
                                    : m->scenario == MOCK_LINK_RECONNECT ? between(m, 800, 1300) : between(m, 60, 140));
    }
    if (m->stage == 0 || (int32_t)(now_ms - m->stage_due_ms) < 0) return;

    switch (m->stage) {
    case 1:
        link_on_wifi(link, m->scenario != MOCK_LINK_NO_WIFI, now_ms);
        if (m->scenario == MOCK_LINK_NO_WIFI) { m->stage = 0; break; }
        m->stage = 2;
        m->stage_due_ms = now_ms + (m->scenario == MOCK_LINK_HA_DOWN ? 3500
                                    : m->scenario == MOCK_LINK_RECONNECT ? between(m, 180, 380) : between(m, 90, 170));
        break;
    case 2:
        link_on_ha(link, m->scenario != MOCK_LINK_HA_DOWN, now_ms);
        if (m->scenario == MOCK_LINK_HA_DOWN) { m->stage = 0; break; }
        m->stage = 3;
        m->stage_due_ms = now_ms + (m->scenario == MOCK_LINK_RECONNECT ? between(m, 90, 190) : between(m, 70, 140));
        break;
    case 3:
        apply_remote_change(m, devs, count, now_ms);
        link_on_state(link, now_ms);
        m->stage = 0;
        break;
    }
}
