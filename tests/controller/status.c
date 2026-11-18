// Host check of the Setting -> Set status sequence and of report reconciliation.
#include <stdio.h>
#include "devices.h"

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

// Stand-in for the network layer: confirm the pending command after `latency` ms.
static void confirm(device_t *d, uint32_t now) { device_on_report(d, d->pending, now); }
#define LIGHTS(v) (device_val_t){ true, (v) }

int main(void)
{
    device_t d; device_init(&d, &DEVICE_LIGHTS, LIGHTS(60));
    uint32_t t = 1000;
    EXPECT(device_status(&d, t) == DEVICE_STATUS_NONE, "idle shows nothing");

    device_turn(&d, +1, t);
    EXPECT(device_status(&d, t) == DEVICE_STATUS_NONE, "editing shows nothing");
    const uint32_t last_turn = t;
    device_tick(&d, last_turn + DEVICE_SEND_AFTER_MS - 1);
    EXPECT(!d.has_pending, "nothing sent before the stillness window ends");

    t = last_turn + DEVICE_SEND_AFTER_MS;
    device_tick(&d, t);
    EXPECT(d.has_pending && d.pending.value == 61, "command sent after stillness");
    EXPECT(device_status(&d, t) == DEVICE_STATUS_SETTING, "Setting once sent");

    t += 300;
    confirm(&d, t);
    EXPECT(d.reported.value == 61 && !d.has_pending, "report confirms the pending command");
    EXPECT(device_status(&d, t) == DEVICE_STATUS_SET, "Set after confirmation");
    EXPECT(device_status(&d, t + DEVICE_DONE_SHOW_MS - 1) == DEVICE_STATUS_SET, "Set visible until timeout");
    EXPECT(device_status(&d, t + DEVICE_DONE_SHOW_MS) == DEVICE_STATUS_NONE, "Set disappears after 1.3 s");

    device_turn(&d, +1, t + 500);
    EXPECT(device_status(&d, t + 500) == DEVICE_STATUS_NONE, "a new turn hides Set");

    // A confirmation that arrives while the user is editing again must not flash "Set" nor move the target.
    device_init(&d, &DEVICE_LIGHTS, LIGHTS(60));
    t = 5000;
    device_turn(&d, +1, t); device_tick(&d, t + DEVICE_SEND_AFTER_MS);
    const uint32_t sent = t + DEVICE_SEND_AFTER_MS;
    device_turn(&d, +1, sent + 100);
    const int target = d.requested.value;
    confirm(&d, sent + 300);
    EXPECT(device_status(&d, sent + 300) == DEVICE_STATUS_NONE, "no Set while editing");
    EXPECT(d.requested.value == target, "late confirmation never moves the displayed target backwards");

    // Reconciliation: idle display follows an external change; editing display does not.
    device_init(&d, &DEVICE_LIGHTS, LIGHTS(60));
    device_on_report(&d, LIGHTS(20), 9000);
    EXPECT(d.requested.value == 20 && d.reported.value == 20, "idle display follows a wall-switch change");
    device_turn(&d, +1, 9100);
    const int mine = d.requested.value;
    device_on_report(&d, LIGHTS(80), 9150);
    EXPECT(d.requested.value == mine && d.reported.value == 80, "an external change never overrides what the user is setting");

    // Turning back to the reported value while a command is in flight must send a correction.
    device_init(&d, &DEVICE_LIGHTS, LIGHTS(60));
    t = 20000;
    device_turn(&d, +1, t); device_tick(&d, t + DEVICE_SEND_AFTER_MS);               // 61 in flight
    t += DEVICE_SEND_AFTER_MS + 50;
    device_turn(&d, -1, t); device_tick(&d, t + DEVICE_SEND_AFTER_MS);               // back to 60
    EXPECT(d.has_pending && d.pending.value == 60, "going back to the old value still sends a correction while another command is in flight");

    // A change that arrives while idle is followed and remembered (the screen counts to it and says "Changed from").
    device_init(&d, &DEVICE_LIGHTS, LIGHTS(60));
    device_on_report(&d, LIGHTS(60), 30000);
    EXPECT(!d.has_changed, "a report that changes nothing is not a change");
    device_on_report(&d, LIGHTS(40), 30500);
    EXPECT(d.reported.value == 40 && d.requested.value == 40, "the fresh state replaces the cached one");
    EXPECT(d.has_changed && d.changed_from.value == 60 && d.changed_ms == 30500, "and remembers what the display showed");

    printf(failures ? "status: %d FAILED\n" : "status: all passed\n", failures);
    return failures != 0;
}
