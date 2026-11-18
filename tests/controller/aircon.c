// Host check of the aircon rules, written from the prototype (reference/knob-ui-prototype.html):
//   * stops at 16 degrees; one more click arms "turn again to switch off"; a second click within 1.6 s switches off
//   * from off, one click up resumes the last confirmed setpoint; turning down while off does nothing
//   * half-degree resolution, 16.0..30.0
#include <stdio.h>
#include "devices.h"

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

// One slow click (long pause before it, so the feel gain is 1x).
static bool click(device_t *d, int dir, uint32_t *t)
{
    *t += 400;
    return device_turn(d, dir, *t);
}

int main(void)
{
    device_t d;
    uint32_t t = 10000;
    device_init(&d, &DEVICE_AIRCON, (device_val_t){ true, 48 });          // 24.0 C
    EXPECT(d.requested.on && d.requested.value == 48 && d.resume_value == 48, "starts on at 24.0");

    click(&d, +1, &t);
    EXPECT(d.requested.value == 49, "one slow click = +0.5 degree");
    click(&d, -1, &t); click(&d, -1, &t);
    EXPECT(d.requested.value == 47, "one slow click = -0.5 degree");

    // Stops at the ceiling.
    device_init(&d, &DEVICE_AIRCON, (device_val_t){ true, 59 });
    click(&d, +1, &t);
    EXPECT(d.requested.value == 60, "reaches 30.0");
    EXPECT(!click(&d, +1, &t) && d.requested.value == 60, "stops at 30.0 (limit bump)");

    // Floor: stop at 16, arm, then switch off.
    device_init(&d, &DEVICE_AIRCON, (device_val_t){ true, 33 });
    click(&d, -1, &t);
    EXPECT(d.requested.on && d.requested.value == 32, "reaches 16.0");
    EXPECT(!click(&d, -1, &t), "first click below 16 only bumps");
    EXPECT(d.requested.on && d.requested.value == 32, "...and does not switch off yet");
    EXPECT(device_off_armed(&d, t), "...but arms 'turn again to switch off'");
    EXPECT(device_turn(&d, -1, t + 400) && !d.requested.on, "second click within 1.6 s switches off");
    EXPECT(!device_off_armed(&d, t + 400), "arming is consumed");

    // The arm expires.
    device_init(&d, &DEVICE_AIRCON, (device_val_t){ true, 32 });
    t += 5000;
    click(&d, -1, &t);                                       // arm
    const uint32_t armed_at = t;
    EXPECT(device_off_armed(&d, armed_at + DEVICE_OFF_ARM_MS - 1), "armed just before the window ends");
    EXPECT(!device_off_armed(&d, armed_at + DEVICE_OFF_ARM_MS), "arm expires after 1.6 s");
    t = armed_at + DEVICE_OFF_ARM_MS + 500;
    EXPECT(!click(&d, -1, &t) && d.requested.on, "a late click arms again instead of switching off");

    // Turning up cancels a pending arm.
    device_init(&d, &DEVICE_AIRCON, (device_val_t){ true, 32 });
    t += 5000;
    click(&d, -1, &t);
    click(&d, +1, &t);
    EXPECT(d.requested.value == 33 && !device_off_armed(&d, t), "turning up cancels the arm");

    // From off: down does nothing, one click up resumes the last confirmed setpoint.
    device_init(&d, &DEVICE_AIRCON, (device_val_t){ true, 52 });          // 26.0
    device_on_report(&d, (device_val_t){ true, 52 }, t);
    d.requested.on = false;                                  // as if the user switched it off
    EXPECT(!click(&d, -1, &t) && !d.requested.on, "turning down while off does nothing");
    EXPECT(click(&d, +1, &t) && d.requested.on && d.requested.value == 52, "one click up resumes the last setpoint (26.0)");

    // Switching off is a command; confirming it keeps the resume value.
    device_init(&d, &DEVICE_AIRCON, (device_val_t){ true, 48 });
    t += 5000;
    d.requested.on = false; d.editing = true; d.send_at_ms = t;
    device_tick(&d, t);
    EXPECT(d.has_pending && !d.pending.on, "off is sent as a command");
    device_on_report(&d, (device_val_t){ false, 0 }, t + 300);
    EXPECT(!d.has_pending && !d.reported.on, "off report confirms it");
    EXPECT(d.resume_value == 48, "the last setpoint is remembered while off");
    EXPECT(device_status(&d, t + 300) == DEVICE_STATUS_SET, "Set is shown after switching off");

    // Fraction along the ring: off rests before the first tick; 16 -> 0, 30 -> 1.
    EXPECT(device_frac(&DEVICE_AIRCON, &(device_val_t){ false, 0 }) == DEVICE_OFF_FRAC, "off fraction");
    EXPECT(device_frac(&DEVICE_AIRCON, &(device_val_t){ true, 32 }) == 0.0f, "16.0 is the start of the ring");
    EXPECT(device_frac(&DEVICE_AIRCON, &(device_val_t){ true, 60 }) == 1.0f, "30.0 is the end of the ring");

    // Leaving the device sends an unsent change immediately.
    device_init(&d, &DEVICE_AIRCON, (device_val_t){ true, 48 });
    t += 5000;
    click(&d, +1, &t);
    EXPECT(!d.has_pending, "nothing sent yet while editing");
    device_flush(&d, t + 50);
    EXPECT(d.has_pending && d.pending.value == 49, "flush sends the change straight away");

    printf(failures ? "aircon: %d FAILED\n" : "aircon: all passed\n", failures);
    return failures != 0;
}
