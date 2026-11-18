// Host check of the connectivity states (link.c), the mock network that drives them (mock_link.c), and what the wake
// does to the devices: commands queue until live, a change made while asleep is followed and remembered.
#include <stdio.h>
#include "devices.h"
#include "mock_ha.h"
#include "mock_link.h"

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

typedef struct { link_t link; mock_link_t mock; device_t dev[3]; uint32_t t; } rig_t;

static void rig(rig_t *r, mock_link_scenario_t sc, bool change)
{
    link_init(&r->link);
    mock_link_init(&r->mock, sc, change, 12345);
    device_init(&r->dev[0], &DEVICE_LIGHTS, (device_val_t){ true, 60 });
    device_init(&r->dev[1], &DEVICE_AIRCON, (device_val_t){ true, 48 });
    device_init(&r->dev[2], &DEVICE_FAN, (device_val_t){ true, 2 });
    r->dev[1].aux = 274;
    r->t = 1000;
}

static void run(rig_t *r, uint32_t ms)
{
    for (uint32_t e = 0; e < ms; e += 8) {
        r->t += 8;
        link_tick(&r->link, r->t);
        mock_link_tick(&r->mock, &r->link, r->dev, 3, r->t);
        for (int i = 0; i < 3; i++) { device_tick(&r->dev[i], r->t); mock_ha_tick(&r->dev[i], &r->link, r->t); }
    }
}

int main(void)
{
    rig_t r;

    // The plain state machine.
    link_t l; link_init(&l);
    EXPECT(l.state == LINK_SLEEP && !link_live(&l) && link_live(NULL), "starts asleep; a missing link counts as live");
    link_wake(&l, 100);
    EXPECT(l.state == LINK_WIFI && l.attempt == 1 && l.wakes == 1 && link_connecting(&l), "wake starts joining Wi-Fi");
    link_wake(&l, 110);
    EXPECT(l.wakes == 1, "waking twice does nothing");
    link_on_ha(&l, true, 120);
    EXPECT(l.state == LINK_WIFI, "an event for another step is ignored");
    EXPECT(link_status(&l) == LINK_STATUS_NONE, "no words while a step is quick");
    link_tick(&l, 100 + LINK_WORDS_AFTER_MS);
    EXPECT(link_status(&l) == LINK_STATUS_NONE, "no words at exactly 250 ms");
    link_tick(&l, 100 + LINK_WORDS_AFTER_MS + 1);
    EXPECT(link_status(&l) == LINK_STATUS_CONNECTING, "Connecting once Wi-Fi took longer than 250 ms");
    link_on_wifi(&l, true, 500);
    EXPECT(l.state == LINK_HA && link_status(&l) == LINK_STATUS_SYNCING, "the words stay (now Syncing) until live");
    link_on_ha(&l, true, 600);
    link_on_state(&l, 700);
    EXPECT(link_live(&l) && l.live_at_ms == 700 && link_status(&l) == LINK_STATUS_NONE, "live");
    EXPECT(link_up_to_date(&l, 700 + LINK_UP_TO_DATE_MS - 1) && !link_up_to_date(&l, 700 + LINK_UP_TO_DATE_MS), "Up to date for 1.3 s");

    // Failures retry every 4 s, restarting from Wi-Fi.
    link_sleep(&l, 800);
    link_wake(&l, 900);
    link_on_wifi(&l, false, 6000);
    EXPECT(l.state == LINK_NO_WIFI && link_status(&l) == LINK_STATUS_NO_WIFI, "No Wi-Fi");
    EXPECT(!link_tick(&l, 6000 + LINK_RETRY_MS - 1), "no retry before 4 s");
    const uint32_t a = l.attempt;
    EXPECT(link_tick(&l, 6000 + LINK_RETRY_MS) && l.state == LINK_WIFI && l.attempt == a + 1 && !l.words_shown, "retry restarts from Wi-Fi");
    link_on_wifi(&l, true, 10100); link_on_ha(&l, false, 13700);
    EXPECT(l.state == LINK_NO_HA && link_status(&l) == LINK_STATUS_OFFLINE, "Offline when Home Assistant fails");
    link_sleep(&l, 14000);
    EXPECT(l.state == LINK_SLEEP && !link_tick(&l, 99999), "no retries while asleep");
    link_wake(&l, 20000); link_on_wifi(&l, true, 20100); link_on_ha(&l, true, 20200); link_on_state(&l, 20300);
    link_lost(&l, false, 25000);
    EXPECT(l.state == LINK_NO_HA, "a live link can drop");

    // The mock network, scenario by scenario.
    rig(&r, MOCK_LINK_FAST, false);
    link_wake(&r.link, r.t);
    run(&r, 600);
    EXPECT(link_live(&r.link) && r.link.live_at_ms - 1000 < 500, "Fast: live within half a second");

    rig(&r, MOCK_LINK_RECONNECT, false);
    link_wake(&r.link, r.t);
    run(&r, 700);
    EXPECT(r.link.state == LINK_WIFI && r.link.words_shown && link_status(&r.link) == LINK_STATUS_CONNECTING, "Reconnect: still joining Wi-Fi after 0.7 s, with words");
    run(&r, 2000);
    EXPECT(link_live(&r.link) && r.link.live_at_ms - 1000 >= 1000, "Reconnect: live after about 1.1-1.9 s");

    rig(&r, MOCK_LINK_NO_WIFI, false);
    link_wake(&r.link, r.t);
    run(&r, 4900);
    EXPECT(r.link.state == LINK_WIFI, "No Wi-Fi: still trying at 4.9 s");
    run(&r, 300);
    EXPECT(r.link.state == LINK_NO_WIFI, "No Wi-Fi: fails at 5 s");
    run(&r, LINK_RETRY_MS + 100);
    EXPECT(r.link.state == LINK_WIFI && r.link.attempt == 2, "No Wi-Fi: retries after 4 s");

    rig(&r, MOCK_LINK_HA_DOWN, false);
    link_wake(&r.link, r.t);
    run(&r, 3000);
    EXPECT(r.link.state == LINK_HA && link_status(&r.link) == LINK_STATUS_SYNCING, "HA down: Wi-Fi joined, waiting for Home Assistant");
    run(&r, 1000);
    EXPECT(r.link.state == LINK_NO_HA, "HA down: fails at about 3.5 s");

    // Commands given before the link is live are queued, and go out once it is.
    rig(&r, MOCK_LINK_RECONNECT, false);
    link_wake(&r.link, r.t);
    for (int i = 0; i < 5; i++) { r.t += 400; device_turn(&r.dev[0], +1, r.t); }
    run(&r, 700);
    EXPECT(r.dev[0].has_pending && r.dev[0].reported.value == 60, "queued: sent but not delivered while offline");
    run(&r, 3000);
    EXPECT(link_live(&r.link) && !r.dev[0].has_pending && r.dev[0].reported.value == 65, "queued command delivered after going live");

    // A change made while asleep is followed and remembered.
    rig(&r, MOCK_LINK_FAST, true);
    link_wake(&r.link, r.t); run(&r, 1000);
    link_sleep(&r.link, r.t); run(&r, 100);
    EXPECT(r.mock.remote_dev >= 0, "something changed at home while asleep");
    const int who = r.mock.remote_dev, was = r.dev[who].requested.value, aux = r.dev[1].aux;
    link_wake(&r.link, r.t); run(&r, 1000);
    EXPECT(link_live(&r.link) && r.dev[who].has_changed && r.dev[who].changed_from.value == was, "the display followed and remembers what it was");
    EXPECT(r.dev[who].requested.value == r.dev[who].reported.value && r.dev[who].requested.value != was, "and shows the new value");
    EXPECT(r.dev[1].aux <= aux, "the room cooled");

    // While the user is queueing a change of their own, a remote change does not override it.
    rig(&r, MOCK_LINK_RECONNECT, false);
    link_wake(&r.link, r.t);
    device_turn(&r.dev[0], +1, r.t);
    const int mine = r.dev[0].requested.value;
    run(&r, 300);
    device_on_report(&r.dev[0], (device_val_t){ true, 10 }, r.t);
    EXPECT(r.dev[0].requested.value == mine, "a remote change never overrides what the user is setting");

    if (!failures) printf("link: all passed\n");
    return failures ? 1 : 0;
}
