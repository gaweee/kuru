// Rules for the Wi-Fi credentials typed on the console (pure C, host-tested). The device is a 2.4 GHz WPA2/WPA3 (or open)
// station: an SSID is 1-32 bytes, a password is 8-63 characters or 64 hex digits, and an empty password means an open
// network. Nothing here stores or logs a password.
#pragma once
#include <stdbool.h>

typedef enum {
    WIFI_CREDS_OK,
    WIFI_CREDS_SSID_EMPTY,
    WIFI_CREDS_SSID_TOO_LONG,
    WIFI_CREDS_PASS_TOO_SHORT,
    WIFI_CREDS_PASS_TOO_LONG,
} wifi_creds_err_t;

#define WIFI_SSID_MAX 32
#define WIFI_PASS_MAX 63

wifi_creds_err_t wifi_creds_validate(const char *ssid, const char *pass);   // pass NULL or "" = open network
const char *wifi_creds_error_text(wifi_creds_err_t e);
