// Host check of the fan rules, written from the prototype (reference/knob-ui-prototype.html):
//   * speeds 0..10, exactly one step per click however fast you spin (no acceleration)
//   * 0 is off; turning down at 0 does nothing; turning up from off starts at speed 1
//   * the ring position: speed n fills n of 10 groups
#include <stdio.h>
#include "devices.h"

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

int main(void)
{
    device_t d;
    uint32_t t = 10000;
    device_init(&d, &DEVICE_FAN, (device_val_t){ true, 2 });
    EXPECT(d.requested.on && d.requested.value == 2, "starts on at speed 2");

    // A hard spin: 40 clicks 20 ms apart (50 Hz) still moves one speed per click.
    for (int i = 0; i < 3; i++) { t += 20; device_turn(&d, +1, t); }
    EXPECT(d.requested.value == 5, "three fast clicks = three speeds");
    for (int i = 0; i < 40; i++) { t += 20; device_turn(&d, +1, t); }
    EXPECT(d.requested.value == 10, "stops at 10");
    t += 20;
    EXPECT(!device_turn(&d, +1, t) && d.requested.value == 10, "limit bump at 10");

    for (int i = 0; i < 10; i++) { t += 15; device_turn(&d, -1, t); }
    EXPECT(d.requested.value == 0 && !d.requested.on, "speed 0 is off");
    t += 15;
    EXPECT(!device_turn(&d, -1, t) && !d.requested.on, "turning down at off does nothing");

    t += 400;
    device_turn(&d, +1, t);
    EXPECT(d.requested.on && d.requested.value == 1, "one click up from off starts at speed 1");

    EXPECT(device_frac(&DEVICE_FAN, &(device_val_t){ true, 5 }) == 0.5f, "speed 5 is half way");
    EXPECT(device_frac(&DEVICE_FAN, &(device_val_t){ false, 0 }) == DEVICE_OFF_FRAC, "off sits below the ring");

    if (!failures) printf("fan: all passed\n");
    return failures ? 1 : 0;
}
