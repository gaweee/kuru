// Host check of the credential rules and of the mock network with a real Wi-Fi adapter in front of it.
#include <stdio.h>
#include <string.h>
#include "devices.h"
#include "mock_link.h"
#include "wifi_creds.h"

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

int main(void)
{
    EXPECT(wifi_creds_validate("MyNetwork", "correct horse") == WIFI_CREDS_OK, "a normal network");
    EXPECT(wifi_creds_validate("Cafe", NULL) == WIFI_CREDS_OK && wifi_creds_validate("Cafe", "") == WIFI_CREDS_OK, "an open network");
    EXPECT(wifi_creds_validate("", "password1") == WIFI_CREDS_SSID_EMPTY && wifi_creds_validate(NULL, NULL) == WIFI_CREDS_SSID_EMPTY, "no name");
    EXPECT(wifi_creds_validate("12345678901234567890123456789012", "password1") == WIFI_CREDS_OK, "a 32 byte name");
    EXPECT(wifi_creds_validate("123456789012345678901234567890123", "password1") == WIFI_CREDS_SSID_TOO_LONG, "a 33 byte name");
    EXPECT(wifi_creds_validate("n", "1234567") == WIFI_CREDS_PASS_TOO_SHORT, "7 characters is too short");
    EXPECT(wifi_creds_validate("n", "12345678") == WIFI_CREDS_OK, "8 characters");
    char p63[64], p64[65], p64x[65];
    memset(p63, 'a', 63); p63[63] = 0;
    memset(p64, 'f', 64); p64[64] = 0;
    memset(p64x, 'f', 64); p64x[64] = 0; p64x[10] = 'z';
    EXPECT(wifi_creds_validate("n", p63) == WIFI_CREDS_OK, "63 characters");
    EXPECT(wifi_creds_validate("n", p64) == WIFI_CREDS_OK, "64 hex digits are a raw key");
    EXPECT(wifi_creds_validate("n", p64x) == WIFI_CREDS_PASS_TOO_LONG, "64 characters that are not hex");
    EXPECT(strlen(wifi_creds_error_text(WIFI_CREDS_PASS_TOO_SHORT)) > 10, "errors have text");

    // The mock behind a real Wi-Fi adapter: nothing moves until the adapter says Wi-Fi is up.
    link_t l; mock_link_t m; device_t dev[1];
    link_init(&l);
    mock_link_init(&m, MOCK_LINK_RECONNECT, false, 7);
    mock_link_use_real_wifi(&m, true);
    device_init(&dev[0], &DEVICE_LIGHTS, (device_val_t){ true, 60 });
    uint32_t t = 1000;
    link_wake(&l, t);
    for (int i = 0; i < 400; i++) { t += 8; link_tick(&l, t); mock_link_tick(&m, &l, dev, 1, t); }
    EXPECT(l.state == LINK_WIFI, "the mock does not join Wi-Fi itself");
    link_on_wifi(&l, true, t);
    for (int i = 0; i < 400; i++) { t += 8; link_tick(&l, t); mock_link_tick(&m, &l, dev, 1, t); }
    EXPECT(link_live(&l), "after the real Wi-Fi joined, the mock plays Home Assistant and the fetch");

    // A real failure, then credentials fixed: link_retry_now starts the next attempt at once.
    link_sleep(&l, t); link_wake(&l, t + 10);
    link_on_wifi(&l, false, t + 20);
    EXPECT(l.state == LINK_NO_WIFI, "the adapter reports a failure");
    const uint32_t a = l.attempt;
    EXPECT(link_retry_now(&l, t + 30) && l.state == LINK_WIFI && l.attempt == a + 1, "retry now does not wait for 4 s");
    EXPECT(!link_retry_now(&l, t + 40), "and does nothing while an attempt is running");

    if (!failures) printf("wifi: all passed\n");
    return failures ? 1 : 0;
}
