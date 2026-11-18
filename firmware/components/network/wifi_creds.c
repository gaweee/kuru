#include "wifi_creds.h"
#include <ctype.h>
#include <string.h>

wifi_creds_err_t wifi_creds_validate(const char *ssid, const char *pass)
{
    const size_t sl = ssid ? strlen(ssid) : 0;
    if (sl == 0) return WIFI_CREDS_SSID_EMPTY;
    if (sl > WIFI_SSID_MAX) return WIFI_CREDS_SSID_TOO_LONG;
    const size_t pl = pass ? strlen(pass) : 0;
    if (pl == 0) return WIFI_CREDS_OK;                              // open network
    if (pl == 64) {                                                 // a raw 256-bit key, as 64 hex digits
        for (size_t i = 0; i < pl; i++) if (!isxdigit((unsigned char)pass[i])) return WIFI_CREDS_PASS_TOO_LONG;
        return WIFI_CREDS_OK;
    }
    if (pl < 8) return WIFI_CREDS_PASS_TOO_SHORT;
    if (pl > WIFI_PASS_MAX) return WIFI_CREDS_PASS_TOO_LONG;
    return WIFI_CREDS_OK;
}

const char *wifi_creds_error_text(wifi_creds_err_t e)
{
    switch (e) {
    case WIFI_CREDS_OK: return "ok";
    case WIFI_CREDS_SSID_EMPTY: return "the network name is empty";
    case WIFI_CREDS_SSID_TOO_LONG: return "the network name is longer than 32 bytes";
    case WIFI_CREDS_PASS_TOO_SHORT: return "a WPA2 password is at least 8 characters (leave it out for an open network)";
    case WIFI_CREDS_PASS_TOO_LONG: return "a WPA2 password is at most 63 characters (or 64 hex digits)";
    }
    return "?";
}
