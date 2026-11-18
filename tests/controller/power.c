// Host check of the screen power rules from the prototype (reference/knob-ui-prototype.html).
#include <stdio.h>
#include "power.h"

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

int main(void)
{
    power_t p;
    uint32_t t = 1000;
    power_init(&p, t);
    EXPECT(p.state == POWER_AWAKE, "starts awake");

    // Idle timeline: awake -> dim at 20 s -> asleep at 30 s.
    EXPECT(!power_tick(&p, t + POWER_DIM_AFTER_MS) && p.state == POWER_AWAKE, "still awake at exactly 20 s");
    EXPECT(power_tick(&p, t + POWER_DIM_AFTER_MS + 1) && p.state == POWER_DIM, "dims after 20 s");
    EXPECT(!power_tick(&p, t + POWER_SLEEP_AFTER_MS) && p.state == POWER_DIM, "still dim at exactly 30 s");
    EXPECT(power_tick(&p, t + POWER_SLEEP_AFTER_MS + 1) && p.state == POWER_ASLEEP, "sleeps after 30 s");
    EXPECT(!power_tick(&p, t + 100000) && p.state == POWER_ASLEEP, "stays asleep");

    // The input that wakes from sleep does nothing, and so does input right after it.
    t += 60000;
    EXPECT(power_on_input(&p, t, true) == POWER_INPUT_WOKE_FROM_SLEEP && p.state == POWER_AWAKE, "first input wakes from sleep and does nothing else");
    EXPECT(power_on_input(&p, t + 200, true) == POWER_INPUT_IGNORED, "input 200 ms after waking is ignored (the rest of the spin)");
    EXPECT(power_on_input(&p, t + 449, true) == POWER_INPUT_IGNORED, "still ignored just inside 450 ms");
    EXPECT(power_on_input(&p, t + 450, true) == POWER_INPUT_ACT, "acted on from 450 ms");

    // Dim: the first input brings the brightness back and does nothing else; shorter ignore window.
    power_init(&p, t);
    power_tick(&p, t + POWER_DIM_AFTER_MS + 1);
    EXPECT(p.state == POWER_DIM, "dim");
    const uint32_t d = t + POWER_DIM_AFTER_MS + 100;
    EXPECT(power_on_input(&p, d, true) == POWER_INPUT_WOKE_FROM_DIM && p.state == POWER_AWAKE, "first input in dim only brightens");
    EXPECT(power_on_input(&p, d + 249, true) == POWER_INPUT_IGNORED, "ignored for 250 ms after dim wake");
    EXPECT(power_on_input(&p, d + 250, true) == POWER_INPUT_ACT, "acted on from 250 ms");

    // Every input, even an ignored one, restarts the idle timer.
    power_init(&p, 0);
    power_on_input(&p, 19000, true);
    EXPECT(!power_tick(&p, 19000 + POWER_DIM_AFTER_MS) && p.state == POWER_AWAKE, "activity at 19 s postpones dimming");
    power_t q;
    power_init(&q, 0);
    power_tick(&q, POWER_SLEEP_AFTER_MS + 1);
    power_on_input(&q, 50000, true);                         // wakes
    power_on_input(&q, 50100, true);                         // ignored, but still activity
    EXPECT(!power_tick(&q, 50100 + POWER_DIM_AFTER_MS) && q.state == POWER_AWAKE, "an ignored input still counts as activity");

    // Only a spin or a double tap wakes. A single tap / touch / swipe does nothing while dim or off.
    power_init(&p, 0);
    power_tick(&p, POWER_DIM_AFTER_MS + 1);
    EXPECT(power_on_input(&p, 21000, false) == POWER_INPUT_NOT_WAKING && p.state == POWER_DIM, "a single touch does not wake a dim screen");
    power_tick(&p, POWER_SLEEP_AFTER_MS + 1);
    EXPECT(p.state == POWER_ASLEEP, "...and the screen still sleeps on schedule: the touch did not restart the timer");
    EXPECT(power_on_input(&p, 40000, false) == POWER_INPUT_NOT_WAKING && p.state == POWER_ASLEEP, "a single touch does not wake a sleeping screen");
    EXPECT(power_on_input(&p, 40100, true) == POWER_INPUT_WOKE_FROM_SLEEP, "a spin or double tap wakes it (and only wakes it)");
    // While awake, a touch is activity.
    power_init(&p, 0);
    power_on_input(&p, 15000, false);
    EXPECT(!power_tick(&p, 15000 + POWER_DIM_AFTER_MS) && p.state == POWER_AWAKE, "while awake, a touch restarts the idle timer");

    // Normal use does not trigger anything.
    power_init(&p, 0);
    for (uint32_t i = 1; i <= 100; i++) EXPECT(power_on_input(&p, i * 10000, true) == POWER_INPUT_ACT, "regular input is acted on");

    EXPECT(power_backlight_target(POWER_AWAKE) == 255 && power_backlight_target(POWER_ASLEEP) == 0, "backlight: full awake, off asleep");
    EXPECT(power_backlight_target(POWER_DIM) > 80 && power_backlight_target(POWER_DIM) < 110, "backlight: dim is ~38 %");

    printf(failures ? "power: %d FAILED\n" : "power: all passed\n", failures);
    return failures != 0;
}
