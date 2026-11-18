#include "wifi_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "wifi_creds.h"

static const char *TAG = "wifi";

// ---- shared with the event handler (another task): plain flags, written there, read in wifi_link_tick ----
static volatile bool s_connected;        // associated and has an IP address
static volatile bool s_hard_fail;        // wrong password: do not retry within this attempt
static volatile bool s_attempting;       // the link is in its Wi-Fi step: the handler keeps trying until the loop gives up
static volatile int s_last_reason;       // last disconnect reason (esp_wifi_types WIFI_REASON_*)
static volatile bool s_creds_changed;
static volatile bool s_inflight;          // a connect is running: asking again only makes the driver log an error

static char s_ssid[WIFI_SSID_MAX + 1], s_pass[WIFI_PASS_MAX + 2];
static bool s_have_creds;
static bool s_started;
static uint32_t s_seen_attempt, s_attempt_start;
static bool s_warned_no_creds;

// ---- credentials in NVS ----
static void load_credentials(void)
{
    s_have_creds = false;
    nvs_handle_t h;
    if (nvs_open("kuru", NVS_READONLY, &h) != ESP_OK) return;
    size_t sl = sizeof s_ssid, pl = sizeof s_pass;
    if (nvs_get_str(h, "wifi_ssid", s_ssid, &sl) == ESP_OK && s_ssid[0]) {
        s_pass[0] = 0;
        nvs_get_str(h, "wifi_pass", s_pass, &pl);
        s_have_creds = true;
    }
    nvs_close(h);
}

static esp_err_t apply_config(void)
{
    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, s_ssid, sizeof cfg.sta.ssid);
    if (strlen(s_pass) == 64) memcpy(cfg.sta.password, s_pass, 64);            // a raw key: 64 hex digits, no terminator
    else strlcpy((char *)cfg.sta.password, s_pass, sizeof cfg.sta.password);
    cfg.sta.threshold.authmode = s_pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;   // WPA2 or better (WPA3 transition works)
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;                              // not "the first AP found": a mesh has several
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;                          // ... join the strongest one
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    return esp_wifi_set_config(WIFI_IF_STA, &cfg);
}

bool wifi_link_has_credentials(void) { return s_have_creds; }

esp_err_t wifi_link_set_credentials(const char *ssid, const char *pass)
{
    if (!pass) pass = "";
    if (wifi_creds_validate(ssid, pass) != WIFI_CREDS_OK) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t e = nvs_open("kuru", NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_str(h, "wifi_ssid", ssid);
    if (e == ESP_OK) e = nvs_set_str(h, "wifi_pass", pass);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) return e;
    strlcpy(s_ssid, ssid, sizeof s_ssid);
    strlcpy(s_pass, pass, sizeof s_pass);
    s_have_creds = true;
    s_warned_no_creds = false;
    s_hard_fail = false;
    if (s_started) { esp_wifi_disconnect(); s_inflight = false; apply_config(); }
    s_creds_changed = true;
    return ESP_OK;
}

esp_err_t wifi_link_clear_credentials(void)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open("kuru", NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    nvs_erase_key(h, "wifi_ssid");
    nvs_erase_key(h, "wifi_pass");
    e = nvs_commit(h);
    nvs_close(h);
    s_have_creds = false;
    s_ssid[0] = s_pass[0] = 0;
    if (s_started) { esp_wifi_disconnect(); s_inflight = false; }
    s_creds_changed = true;
    return e;
}

// ---- events (default event loop task) ----
static bool hard_failure(int reason)
{
    return reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || reason == WIFI_REASON_HANDSHAKE_TIMEOUT;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_have_creds) { s_inflight = true; esp_wifi_connect(); }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *ev = data;
        s_connected = false;
        s_inflight = false;
        s_last_reason = ev->reason;
        ESP_LOGW(TAG, "disconnected, reason %d", ev->reason);
        if (hard_failure(ev->reason)) { s_hard_fail = true; ESP_LOGW(TAG, "the password was rejected: check it with `wifi set`"); return; }
        if (ev->reason == WIFI_REASON_ASSOC_LEAVE) return;                    // we disconnected on purpose
        if (s_have_creds) { s_inflight = true; esp_wifi_connect(); }           // lost it, or the network is not there yet: keep trying
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = data;
        s_connected = true;
        s_inflight = false;
        s_hard_fail = false;
        wifi_ap_info_t ap;
        if (wifi_link_ap(&ap))
            ESP_LOGI(TAG, "connected, ip " IPSTR ", AP %02x:%02x:%02x:%02x:%02x:%02x, %d dBm, channel %d", IP2STR(&ev->ip_info.ip),
                     ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5], ap.rssi, ap.channel);
        else
            ESP_LOGI(TAG, "connected, ip " IPSTR, IP2STR(&ev->ip_info.ip));
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        s_connected = false;
    }
}

void wifi_link_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); e = nvs_flash_init(); }
    ESP_ERROR_CHECK(e);
    load_credentials();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(netif, "kuru");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));                  // credentials are ours, in NVS
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, on_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    if (s_have_creds) apply_config();
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);                                       // sleep between beacons: this runs on a battery
    s_started = true;
    if (s_have_creds) ESP_LOGI(TAG, "credentials for \"%s\" found", s_ssid);
    else ESP_LOGW(TAG, "no Wi-Fi credentials stored: type `wifi set <network> <password>` in the USB console");
}

static void begin_attempt(uint32_t now_ms)
{
    s_attempt_start = now_ms;
    s_hard_fail = false;
    s_attempting = true;
    if (s_have_creds && !s_connected && !s_inflight) { s_inflight = true; esp_wifi_connect(); }
}

void wifi_link_tick(link_t *l, uint32_t now_ms)
{
    if (s_creds_changed) {                                                     // new credentials (or none): start over
        s_creds_changed = false;
        s_connected = false;
        switch (l->state) {
        case LINK_SLEEP: break;                                                // it reconnects on the next wake
        case LINK_WIFI: begin_attempt(now_ms); break;
        case LINK_LIVE: case LINK_HA: case LINK_FETCH: link_lost(l, true, now_ms); link_retry_now(l, now_ms); break;
        default: link_retry_now(l, now_ms); break;
        }
        if (s_have_creds && s_started && !s_connected && !s_inflight) { s_inflight = true; esp_wifi_connect(); }
    }

    if (l->state == LINK_WIFI) {
        if (l->attempt != s_seen_attempt) { s_seen_attempt = l->attempt; begin_attempt(now_ms); }
        if (!s_have_creds) {
            if (!s_warned_no_creds) { s_warned_no_creds = true; ESP_LOGW(TAG, "no credentials: `wifi set <network> <password>`"); }
            s_attempting = false;
            link_on_wifi(l, false, now_ms);
        } else if (s_connected) {
            s_attempting = false;
            link_on_wifi(l, true, now_ms);
        } else if (s_hard_fail || (int32_t)(now_ms - s_attempt_start) > WIFI_ATTEMPT_TIMEOUT_MS) {
            ESP_LOGW(TAG, "giving up after %u ms (last reason %d)", (unsigned)(now_ms - s_attempt_start), s_last_reason);
            s_attempting = false;
            esp_wifi_disconnect();
            s_inflight = false;                                               // (a disconnect we asked for may not send an event)
            link_on_wifi(l, false, now_ms);
        }
    } else {
        s_attempting = false;
        if ((l->state == LINK_HA || l->state == LINK_FETCH || l->state == LINK_LIVE) && !s_connected) {
            ESP_LOGW(TAG, "connection lost");
            link_lost(l, true, now_ms);
        }
    }
}

bool wifi_link_ap(wifi_ap_info_t *out)
{
    wifi_ap_record_t ap;
    if (!s_connected || esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return false;
    out->connected = true;
    memcpy(out->bssid, ap.bssid, 6);
    out->rssi = ap.rssi;
    out->channel = ap.primary;
    return true;
}

void wifi_link_describe(char *out, size_t n)
{
    if (!s_have_creds) { snprintf(out, n, "no credentials stored"); return; }
    wifi_ap_info_t ap;
    esp_netif_ip_info_t ip;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (wifi_link_ap(&ap) && netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK)
        snprintf(out, n, "\"%s\": connected, ip " IPSTR ", AP %02x:%02x:%02x:%02x:%02x:%02x, signal %d dBm, channel %d", s_ssid, IP2STR(&ip.ip),
                 ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5], ap.rssi, ap.channel);
    else
        snprintf(out, n, "\"%s\": not connected (last disconnect reason %d)", s_ssid, s_last_reason);
}

void wifi_link_scan_print(const char *ssid_filter)
{
    wifi_scan_config_t sc = { 0 };
    sc.show_hidden = false;
    if (ssid_filter && ssid_filter[0]) sc.ssid = (uint8_t *)ssid_filter;                // only the APs with this name
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) { printf("scan failed (is a connection attempt running? try again)\n"); return; }
    // On the heap, not the stack: 20 records are ~2.5 KB and the console task's stack is small (a stack overflow here
    // rebooted the board).
    uint16_t n = 20, total = 0;
    wifi_ap_record_t *recs = malloc(n * sizeof *recs);
    if (!recs) { printf("out of memory\n"); esp_wifi_clear_ap_list(); return; }
    esp_wifi_scan_get_ap_num(&total);
    esp_wifi_scan_get_ap_records(&n, recs);
    printf("%u access points in range (2.4 GHz only), strongest first:\n", (unsigned)total);
    for (int i = 0; i < n; i++) {
        const char *auth = recs[i].authmode == WIFI_AUTH_OPEN ? "open" : recs[i].authmode == WIFI_AUTH_WEP ? "WEP"
                         : recs[i].authmode >= WIFI_AUTH_WPA3_PSK ? "WPA3" : recs[i].authmode == WIFI_AUTH_WPA_PSK ? "WPA" : "WPA2";
        const uint8_t *b = recs[i].bssid;
        printf("  %-32s %02x:%02x:%02x:%02x:%02x:%02x %4d dBm  ch %2d  %s\n", (const char *)recs[i].ssid, b[0], b[1], b[2], b[3], b[4], b[5],
               recs[i].rssi, recs[i].primary, auth);
    }
    free(recs);
}
