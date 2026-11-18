#include "power.h"

void power_init(power_t *p, uint32_t now_ms)
{
    *p = (power_t){ .state = POWER_AWAKE, .last_input_ms = now_ms };
}

power_input_t power_on_input(power_t *p, uint32_t now_ms, bool wakes)
{
    if (p->state != POWER_AWAKE && !wakes) return POWER_INPUT_NOT_WAKING;
    p->last_input_ms = now_ms;
    if (p->state == POWER_ASLEEP) {
        p->state = POWER_AWAKE;
        p->ignore_until_ms = now_ms + POWER_SLEEP_WAKE_IGNORE_MS;
        return POWER_INPUT_WOKE_FROM_SLEEP;
    }
    if (p->state == POWER_DIM) {
        p->state = POWER_AWAKE;
        p->ignore_until_ms = now_ms + POWER_DIM_WAKE_IGNORE_MS;
        return POWER_INPUT_WOKE_FROM_DIM;
    }
    if ((int32_t)(now_ms - p->ignore_until_ms) < 0) return POWER_INPUT_IGNORED;
    return POWER_INPUT_ACT;
}

bool power_tick(power_t *p, uint32_t now_ms)
{
    const power_state_t before = p->state;
    const uint32_t idle = now_ms - p->last_input_ms;
    if (p->state == POWER_AWAKE && idle > POWER_DIM_AFTER_MS) p->state = POWER_DIM;
    if (p->state == POWER_DIM && idle > POWER_SLEEP_AFTER_MS) p->state = POWER_ASLEEP;
    return p->state != before;
}

uint8_t power_backlight_target(power_state_t s)
{
    return s == POWER_AWAKE ? 255 : s == POWER_DIM ? 97 : 0;      // 97 = 38 % of 255
}
