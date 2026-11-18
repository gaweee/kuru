// The real Wi-Fi adapter: joins the network and drives the Wi-Fi step of the link (link.h). Credentials live in flash
// (NVS namespace "kuru"), are set from the USB console (console.c) and never appear in the source, a log or git.
//
// While the link is in LINK_WIFI the adapter connects and reports link_on_wifi(true / false). A wrong password
// (authentication or handshake failure) fails at once; an unreachable network fails after WIFI_ATTEMPT_TIMEOUT_MS. With
// no credentials stored the step fails at once. A connection that drops while the link is up is reported as link_lost().
// The radio stays associated while the screen sleeps (waking is then mostly a state fetch; M4 revisits this for battery).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "link.h"

#define WIFI_ATTEMPT_TIMEOUT_MS 10000

void wifi_link_init(void);                                    // NVS, network stack, Wi-Fi driver in station mode
void wifi_link_tick(link_t *l, uint32_t now_ms);              // call every frame from the loop that owns the link

esp_err_t wifi_link_set_credentials(const char *ssid, const char *pass);   // validated, stored, and a new attempt starts
esp_err_t wifi_link_clear_credentials(void);
bool wifi_link_has_credentials(void);
void wifi_link_describe(char *out, size_t n);                 // one line for `wifi status`: no password
// The access point the device is associated with. On a network with several access points (a mesh, one per floor) the
// BSSID is what tells them apart: the name is the same on all of them. NOTE: the ESP32 does not roam by itself, so this
// is the AP that was strongest when it joined, not necessarily the nearest now; to tell where the device is, compare the
// signal of every BSSID with a scan (wifi_link_scan_print / esp_wifi_scan_*).
typedef struct { bool connected; uint8_t bssid[6]; int8_t rssi; uint8_t channel; } wifi_ap_info_t;
bool wifi_link_ap(wifi_ap_info_t *out);

void wifi_link_scan_print(const char *ssid_filter);           // `wifi scan [name]`: the networks in range, or only the APs of one name                              // `wifi scan`: networks in range (the radio is 2.4 GHz only)
