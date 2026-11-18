// MOCK network for the wake sequence (stands in for Wi-Fi + MQTT until M3). It plays the scenarios of
// reference/knob-ui-prototype.html ("Network when it wakes") against a link_t, so every state of the wake animation can be seen on
// the board without a network. A real adapter does the same job: whenever link->attempt changes it joins Wi-Fi
// (link_on_wifi), reaches Home Assistant (link_on_ha), fetches every device's state (device_on_report) and finally
// calls link_on_state(); a step that fails is reported as link_on_*(false).
#pragma once
#include "device.h"
#include "link.h"

typedef enum {
    MOCK_LINK_FAST,        // Wi-Fi 60-140 ms, HA 90-170 ms, state 70-140 ms: just the scan and the wave
    MOCK_LINK_RECONNECT,   // Wi-Fi 0.8-1.3 s, HA 0.2-0.4 s, state 0.1-0.2 s: words appear, as after a deep sleep
    MOCK_LINK_NO_WIFI,     // Wi-Fi never joins: fails after 5 s, retries every 4 s
    MOCK_LINK_HA_DOWN,     // Wi-Fi joins, Home Assistant never answers: fails after 3.5 s, retries every 4 s
} mock_link_scenario_t;

// Scenario of the product build; change it here to see another one on the board.
// 1: the Wi-Fi step is real (wifi_link.c, credentials from the USB console) and the mock plays only Home Assistant.
// 0: everything is simulated, and the scenario below decides how the wake goes (no radio, no credentials).
#ifndef KURU_LINK_REAL_WIFI
#define KURU_LINK_REAL_WIFI 1
#endif

#ifndef KURU_LINK_SCENARIO
#define KURU_LINK_SCENARIO MOCK_LINK_RECONNECT
#endif

typedef struct {
    mock_link_scenario_t scenario;
    bool wifi_is_real;          // a real adapter (wifi_link.c) drives the Wi-Fi step; the mock only plays Home Assistant
    uint32_t ha_attempt;
    bool change_while_asleep;   // while the screen sleeps, something changes at home; it shows up when the state arrives
    uint32_t seen_attempt;
    int stage;                  // 0 idle, 1 joining Wi-Fi, 2 reaching HA, 3 fetching state
    uint32_t stage_due_ms;
    uint32_t rng;
    bool asleep;
    int remote_dev;             // device that changed while asleep (-1: none)
    int remote_value;
} mock_link_t;

void mock_link_init(mock_link_t *m, mock_link_scenario_t scenario, bool change_while_asleep, uint32_t seed);
void mock_link_set_scenario(mock_link_t *m, mock_link_scenario_t scenario);
// With real Wi-Fi the mock waits for link_on_wifi() from the real adapter, then plays the Home Assistant and fetch steps
// of its scenario (the Wi-Fi timings and the no-Wi-Fi scenario are ignored).
void mock_link_use_real_wifi(mock_link_t *m, bool real);
void mock_link_tick(mock_link_t *m, link_t *link, device_t *devs, int count, uint32_t now_ms);
