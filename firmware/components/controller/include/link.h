// Connectivity of the controller after it wakes (pure C, host-tested). Rules from reference/knob-ui-prototype.html, "Waking up":
//
//   SLEEP -> WIFI -> HA -> FETCH -> LIVE          a wake walks through these steps
//            |       |
//            v       v
//         NO_WIFI  NO_HA                            a failed step says which one failed and retries every 4 s
//
// While anything but LIVE, the screen shows the last known state muted (the ring, the number), hides what only live
// state can tell (the thin arc, the ripples, the curtain pleats) and scans the ticks. Status words ("Connecting",
// "Syncing") appear only once a step has taken longer than 250 ms, so a fast wake is just the scan and the wave.
// Turns made before LIVE are queued (see mock_ha.c) and sent once it is.
//
// This module only holds the state. Whatever actually connects (today mock_link.c, later Wi-Fi + MQTT) drives it
// through the link_on_* calls and starts connecting whenever `attempt` changes:
//
//   wake:  link_wake()  -> start joining Wi-Fi          -> link_on_wifi(ok)
//   then:  state HA     -> reach Home Assistant         -> link_on_ha(ok)
//   then:  state FETCH  -> fetch every device's state   -> device_on_report() for each, then link_on_state()
//   a step that fails (timeout) is reported as link_on_wifi(false) / link_on_ha(false); link_tick() retries.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define LINK_RETRY_MS 4000            // after a failure, the next attempt starts this much later
#define LINK_WORDS_AFTER_MS 250       // status words only once a step has taken longer than this
#define LINK_UP_TO_DATE_MS 1300       // "Up to date" stays this long after going live

typedef enum {
    LINK_SLEEP,       // screen off: nothing is being connected
    LINK_WIFI,        // joining Wi-Fi
    LINK_HA,          // reaching Home Assistant
    LINK_FETCH,       // fetching the latest state
    LINK_LIVE,        // the state on screen is fresh
    LINK_NO_WIFI,     // Wi-Fi failed; retrying
    LINK_NO_HA,       // Home Assistant unreachable; retrying
} link_state_t;

// What the status line says about the link (not a device command).
typedef enum {
    LINK_STATUS_NONE,         // nothing (live, or a step that is still quick)
    LINK_STATUS_CONNECTING,   // joining Wi-Fi, slowly
    LINK_STATUS_SYNCING,      // reaching Home Assistant / fetching state, slowly
    LINK_STATUS_NO_WIFI,
    LINK_STATUS_OFFLINE,      // Home Assistant unreachable
} link_status_t;

typedef struct {
    link_state_t state;
    uint32_t since_ms;        // when the state was entered
    uint32_t live_at_ms;      // when it last became LIVE (the ring's landing wave starts here)
    uint32_t retry_at_ms;     // NO_*: when the next attempt starts
    bool words_shown;         // latched: once a step was slow the words stay until LIVE
    uint32_t attempt;         // counts attempts (the wake and every retry): connect afresh whenever it changes
    uint32_t wakes;           // counts wakes from sleep: the screen restarts its ring sweep
} link_t;

void link_init(link_t *l);                            // starts in SLEEP
void link_wake(link_t *l, uint32_t now_ms);           // SLEEP -> WIFI (a new attempt); ignored in any other state
void link_sleep(link_t *l, uint32_t now_ms);          // any -> SLEEP
void link_on_wifi(link_t *l, bool ok, uint32_t now_ms);   // WIFI -> HA, or NO_WIFI
void link_on_ha(link_t *l, bool ok, uint32_t now_ms);     // HA -> FETCH, or NO_HA
void link_on_state(link_t *l, uint32_t now_ms);           // FETCH -> LIVE (call after the devices were updated)
void link_lost(link_t *l, bool wifi, uint32_t now_ms);    // LIVE / HA / FETCH -> NO_WIFI or NO_HA (connection dropped)
bool link_tick(link_t *l, uint32_t now_ms);           // words latch + retry timer; true if a new attempt just began

bool link_retry_now(link_t *l, uint32_t now_ms);          // NO_WIFI / NO_HA: start the next attempt at once (credentials changed)

bool link_live(const link_t *l);                      // NULL counts as live (tests, builds without a link)
bool link_connecting(const link_t *l);                // WIFI / HA / FETCH: the tick scan runs
link_status_t link_status(const link_t *l);
bool link_up_to_date(const link_t *l, uint32_t now_ms);   // LIVE for less than LINK_UP_TO_DATE_MS
const char *link_state_name(link_state_t s);          // for logs
