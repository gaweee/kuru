#include "link.h"
#include <stddef.h>

static void enter(link_t *l, link_state_t s, uint32_t now_ms)
{
    l->state = s;
    l->since_ms = now_ms;
}

static void begin_attempt(link_t *l, uint32_t now_ms)
{
    enter(l, LINK_WIFI, now_ms);
    l->words_shown = false;
    l->attempt++;
}

static void fail(link_t *l, link_state_t s, uint32_t now_ms)
{
    enter(l, s, now_ms);
    l->retry_at_ms = now_ms + LINK_RETRY_MS;
}

void link_init(link_t *l)
{
    *l = (link_t){ .state = LINK_SLEEP };
}

void link_wake(link_t *l, uint32_t now_ms)
{
    if (l->state != LINK_SLEEP) return;
    l->wakes++;
    begin_attempt(l, now_ms);
}

void link_sleep(link_t *l, uint32_t now_ms)
{
    enter(l, LINK_SLEEP, now_ms);
    l->words_shown = false;
}

void link_on_wifi(link_t *l, bool ok, uint32_t now_ms)
{
    if (l->state != LINK_WIFI) return;
    if (ok) enter(l, LINK_HA, now_ms); else fail(l, LINK_NO_WIFI, now_ms);
}

void link_on_ha(link_t *l, bool ok, uint32_t now_ms)
{
    if (l->state != LINK_HA) return;
    if (ok) enter(l, LINK_FETCH, now_ms); else fail(l, LINK_NO_HA, now_ms);
}

void link_on_state(link_t *l, uint32_t now_ms)
{
    if (l->state != LINK_FETCH) return;
    enter(l, LINK_LIVE, now_ms);
    l->live_at_ms = now_ms;
    l->words_shown = false;
}

void link_lost(link_t *l, bool wifi, uint32_t now_ms)
{
    if (l->state != LINK_LIVE && l->state != LINK_HA && l->state != LINK_FETCH) return;
    fail(l, wifi ? LINK_NO_WIFI : LINK_NO_HA, now_ms);
}

bool link_tick(link_t *l, uint32_t now_ms)
{
    if (link_connecting(l) && (int32_t)(now_ms - l->since_ms) > LINK_WORDS_AFTER_MS) l->words_shown = true;
    if ((l->state == LINK_NO_WIFI || l->state == LINK_NO_HA) && (int32_t)(now_ms - l->retry_at_ms) >= 0) {
        begin_attempt(l, now_ms);
        return true;
    }
    return false;
}

bool link_retry_now(link_t *l, uint32_t now_ms)
{
    if (l->state != LINK_NO_WIFI && l->state != LINK_NO_HA) return false;
    begin_attempt(l, now_ms);
    return true;
}

bool link_live(const link_t *l) { return !l || l->state == LINK_LIVE; }

bool link_connecting(const link_t *l)
{
    return l && (l->state == LINK_WIFI || l->state == LINK_HA || l->state == LINK_FETCH);
}

link_status_t link_status(const link_t *l)
{
    if (!l) return LINK_STATUS_NONE;
    switch (l->state) {
    case LINK_WIFI:    return l->words_shown ? LINK_STATUS_CONNECTING : LINK_STATUS_NONE;
    case LINK_HA:
    case LINK_FETCH:   return l->words_shown ? LINK_STATUS_SYNCING : LINK_STATUS_NONE;
    case LINK_NO_WIFI: return LINK_STATUS_NO_WIFI;
    case LINK_NO_HA:   return LINK_STATUS_OFFLINE;
    default:           return LINK_STATUS_NONE;
    }
}

bool link_up_to_date(const link_t *l, uint32_t now_ms)
{
    return l && l->state == LINK_LIVE && (int32_t)(now_ms - l->live_at_ms) < LINK_UP_TO_DATE_MS;
}

const char *link_state_name(link_state_t s)
{
    static const char *const n[] = { "sleep", "wifi", "ha", "fetch", "live", "no-wifi", "no-ha" };
    return (unsigned)s < sizeof n / sizeof n[0] ? n[s] : "?";
}
