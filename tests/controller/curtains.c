// Host check of the curtains rules (written from reference/knob-ui-prototype.html) and of the mock motor behind them:
//   * 0..100 % closed (0 = fully open, 100 = fully closed), always on: never "off"
//   * command -> Setting -> (accepted) Opening / Closing -> arrived -> Set
//   * position reports while travelling never drag the requested value
//   * motion started elsewhere is followed; turning again mid-travel re-aims the motor
#include <stdio.h>
#include "devices.h"
#include "mock_ha.h"

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

static void run(device_t *d, uint32_t *t, uint32_t ms)
{
    for (uint32_t e = 0; e < ms; e += 8) { *t += 8; device_tick(d, *t); mock_ha_tick(d, NULL, *t); }
}

int main(void)
{
    device_t d;
    uint32_t t = 10000;
    device_init(&d, &DEVICE_CURTAINS, (device_val_t){ true, 35 });
    EXPECT(d.requested.on && d.requested.value == 35, "starts at 35 %");

    // Always on: down to 0 (fully open) stays on, and sits on the first tick, not below the ring.
    for (int i = 0; i < 60; i++) { t += 400; device_turn(&d, -1, t); }
    EXPECT(d.requested.on && d.requested.value == 0, "0 % is fully open, still on");
    EXPECT(device_frac(&DEVICE_CURTAINS, &d.requested) == 0.0f, "fully open sits at the first tick, not below the ring");
    device_init(&d, &DEVICE_CURTAINS, (device_val_t){ true, 35 });

    // One adjustment: 35 -> 55.
    for (int i = 0; i < 20; i++) { t += 400; device_turn(&d, +1, t); }
    EXPECT(d.requested.value == 55 && d.editing, "20 slow clicks = +20 %");
    EXPECT(device_status(&d, t) == DEVICE_STATUS_NONE, "no status while adjusting");
    run(&d, &t, 700);
    EXPECT(d.has_pending && device_status(&d, t) == DEVICE_STATUS_SETTING, "sent: Setting");
    run(&d, &t, 400);
    EXPECT(d.moving && !d.has_pending && device_status(&d, t) == DEVICE_STATUS_CLOSING, "accepted: 35 -> 55 is more closed, so Closing");
    EXPECT(d.requested.value == 55, "requested stays at the target");
    run(&d, &t, 700);
    EXPECT(d.reported.value > 35 && d.reported.value < 55 && d.requested.value == 55, "mid-travel report is progress only");
    run(&d, &t, 900);                       // 20 % at 14 %/s arrives about 1.4 s after the motor starts
    EXPECT(!d.moving && d.reported.value == 55 && d.requested.value == 55, "arrived at 55");
    EXPECT(device_status(&d, t) == DEVICE_STATUS_SET, "arrived: Set");
    run(&d, &t, 1500);
    EXPECT(device_status(&d, t) == DEVICE_STATUS_NONE, "Set fades after 1.3 s");

    // Opening (the value goes down), with a re-aim mid-travel.
    for (int i = 0; i < 40; i++) { t += 400; device_turn(&d, -1, t); }
    run(&d, &t, 1700);
    EXPECT(d.moving && device_status(&d, t) == DEVICE_STATUS_OPENING, "55 -> 15 is more open, so Opening");
    const int mid = d.reported.value;
    for (int i = 0; i < 5; i++) { t += 400; device_turn(&d, +1, t); }
    EXPECT(d.editing && d.requested.value == 20, "turning mid-travel edits again (15 + 5)");
    EXPECT(device_status(&d, t) == DEVICE_STATUS_NONE, "no status while adjusting again");
    run(&d, &t, 700 + 400);
    EXPECT(d.moving && d.moving_to == 20, "re-aimed");
    run(&d, &t, 4000);
    EXPECT(!d.moving && d.reported.value == 20 && mid <= 55, "finished at the new target");

    // Motion started elsewhere (wall remote): the display follows.
    device_on_motion(&d, true, 90, t);
    EXPECT(d.requested.value == 90 && device_status(&d, t) == DEVICE_STATUS_CLOSING, "external motion is followed (20 -> 90: Closing)");
    device_on_motion(&d, false, 90, t);

    if (!failures) printf("curtains: all passed\n");
    return failures ? 1 : 0;
}
