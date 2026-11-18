// MOCK Home Assistant backend (M1). Stands in for the MQTT adapter planned for M3.
// Contract the real adapter must also satisfy: take a device's pending command, deliver it (only while the link is
// live: commands given before that stay queued and go out once it is), and later call device_on_report() with the
// device's actual state. A motorised device reports its travel with device_on_motion().
#pragma once
#include "device.h"
#include "link.h"

#define MOCK_HA_LATENCY_MS 300

// `link` may be NULL (always live). A queued command's latency counts from the moment the link went live.
void mock_ha_tick(device_t *d, const link_t *link, uint32_t now_ms);
