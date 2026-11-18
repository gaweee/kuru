#include "board.h"
#include "devices.h"
#include "input.h"
#include "looks.h"
#include "kuru_console.h"
#include "link.h"
#include "mock_ha.h"
#include "mock_link.h"
#include "wifi_link.h"
#include "power.h"
#include "ui.h"
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"

// The devices this controller offers. Index i uses LOOKS[i]. Adding a device = one entry in each array
// (and its definition in controller/devices.c, its look in ui/looks.c, its assets in tools/gen_assets.py).
#define NUM_DEVICES 4
static const device_def_t *const DEFS[NUM_DEVICES] = { &DEVICE_LIGHTS, &DEVICE_AIRCON, &DEVICE_FAN, &DEVICE_CURTAINS };
static const device_look_t *const LOOKS[NUM_DEVICES] = { &LOOK_LIGHTS, &LOOK_AIRCON, &LOOK_FAN, &LOOK_CURTAINS };
// Cached state shown on wake until the first report arrives (never treated as live).
static const device_val_t INITIAL[NUM_DEVICES] = { { true, 60 }, { true, 48 }, { true, 2 }, { true, 65 } };   // lights 60 %, aircon 24.0 C, fan speed 2, curtains 65 % closed (35 % open)
static const int INITIAL_AUX[NUM_DEVICES] = { 0, 274, 0, 0 };                         // aircon room temperature: 27.4 C (mock)

static device_t s_dev[NUM_DEVICES];

// ---- connection ----
// The wake sequence (docs/architecture.md, "Connection"): link.c holds the state, mock_link.c plays a network until the
// real Wi-Fi + MQTT adapter replaces it. Every change of state is logged: that is the minimal Wi-Fi test.
static link_t s_link;
static mock_link_t s_mock;

static void link_log(uint32_t now)
{
    static link_state_t prev = LINK_SLEEP;
    static uint32_t since;
    if (s_link.state == prev) return;
    ESP_LOGI("link", "%s -> %s after %u ms (attempt %u)", link_state_name(prev), link_state_name(s_link.state), (unsigned)(now - since), (unsigned)s_link.attempt);
    prev = s_link.state;
    since = now;
}
static int s_cur;                                        // the device on screen

// ---- screen power ----
// The rules live in components/power (prototype: dim at 20 s, off at 30 s; only a spin or a double tap wakes it, and
// that input does nothing else). This is the glue: backlight and panel, flush on sleep, sync on wake.
static power_t s_power;
static float s_bright = 255;                             // current backlight 0..255, eased toward the target
static bool s_panel_on = true;

static void power_apply(uint32_t dt_ms)
{
    const power_state_t st = s_power.state;
    const float target = power_backlight_target(st);
    const float rate = target < s_bright ? 255.0f / 800.0f : 255.0f / 250.0f;      // prototype: 0.8 s fade down, 0.25 s up
    if (st != POWER_ASLEEP && !s_panel_on) { board_display_on(true); s_panel_on = true; }   // panel on before the light comes up
    s_bright = target < s_bright ? fmaxf(target, s_bright - rate * dt_ms) : fminf(target, s_bright + rate * dt_ms);
    board_backlight_set((uint8_t)(s_bright + 0.5f));
    if (st == POWER_ASLEEP && s_bright <= 0.5f && s_panel_on) { board_display_on(false); s_panel_on = false; }
}

#ifdef KURU_PERF
// Frame-time logging, per-click logging, and (with KURU_SYNTH_SPIN) a synthetic spin. Not part of the product build.
static void perf_step(uint32_t now)
{
    static uint32_t last, win_start, n, worst, sum;
    if (last) { const uint32_t dt = now - last; n++; sum += dt; if (dt > worst) worst = dt; }
    last = now;
    board_perf_tag(ui_stage());
#if defined(KURU_SYNTH_SPIN)
    // Every 3 s: 60 clicks at 25 ms (a real-speed spin), alternating up and down.
    static uint32_t start; static int cycle = -1; static uint32_t spun;
    if (!start) start = now + 2000;
    if (now >= start) {
        const int c = (int)((now - start) / 3000);
        if (c != cycle) { cycle = c; spun = 0; }
        const uint32_t t = (now - start) % 3000;
        while (spun < 60 && t >= spun * 25) { device_turn(&s_dev[s_cur], (c & 1) ? -1 : +1, now); spun++; }
    }
#endif
    if (now - win_start >= 1000) {
        uint32_t rn, ru, fn, fpx, fus;
        board_perf_take(&rn, &ru, &fn, &fpx, &fus);
        ESP_LOGI("perf", "stage=%d frames=%u avg=%ums worst=%ums | refresh n=%u avg=%ums | flush n=%u px=%u spi=%ums | dev=%d link=%s req=%d rep=%d",
                 ui_stage(), (unsigned)n, n ? (unsigned)(sum / n) : 0, (unsigned)worst, (unsigned)rn, rn ? (unsigned)(ru / rn / 1000) : 0,
                 (unsigned)fn, (unsigned)fpx, (unsigned)(fus / 1000), s_cur, link_state_name(s_link.state), s_dev[s_cur].requested.value, s_dev[s_cur].reported.value);
        uint32_t tn, tus, tmax, tpx;
        board_perf_turning(&tn, &tus, &tmax, &tpx);
        if (tn) ESP_LOGI("perf", "  TURNING only: refreshes=%u avg=%.1fms max=%.1fms avg px=%u", (unsigned)tn, tus / 1000.0 / tn, tmax / 1000.0, (unsigned)(tpx / tn));
        win_start = now; n = sum = worst = 0;
    }
}
#endif

#ifdef KURU_SYNTH_ABLATE
// Perf: every 2 s force a full redraw; every 6 s hide one more group (segments, ticks, text, handles).
static void perf_ablate(uint32_t now)
{
    static uint32_t next = 3000; static int step;
    if (now < next) return;
    next = now + 2000;
    if (step % 3 == 0 && step / 3 <= 6) { ui_perf_ablate(step / 3); ESP_LOGI("perf", "=== ablation group %d hidden (cumulative)", step / 3); }
    step++;
    lv_obj_invalidate(lv_screen_active());
}
#endif

#ifdef KURU_SYNTH_SWITCH
// Perf: switch device every 4 s (starting at 3 s).
static bool perf_switch_due(uint32_t now)
{
    static uint32_t next = 3000;
    if (now < next) return false;
    next = now + 4000;
    return true;
}
#endif

// Interaction state machine (docs/architecture.md): the same rules the simulator prototype follows.
//   turn            -> adjust the current device
//   double tap      -> next device (also the BOOT button)
//   swipe           -> swipe-left goes to the next device, swipe-right to the previous one
//   single tap      -> nothing yet (reserved: confirm-before-apply, overview selection)
//   hold            -> (overview: not built yet)
//   asleep / dim    -> only a spin or a double tap wakes it, and that input does nothing else
static void go_to(int index, int dir, uint32_t now)
{
    device_flush(&s_dev[s_cur], now);                    // leaving a device sends any unsent change immediately
    s_cur = (index + NUM_DEVICES) % NUM_DEVICES;
    ui_switch_device(LOOKS[s_cur], s_cur, NUM_DEVICES, &s_dev[s_cur], dir);
}

#ifdef KURU_SYNTH_TOUR
// Perf tour (build with -DKURU_PERF=1 -DKURU_SYNTH_TOUR=1): every device in turn gets a hard spin at real speed
// (60 clicks, 25 ms apart), a settle period, and where it matters its idle animation, then device switches. The
// per-second "perf" lines that follow each "=== phase" line are that phase's frame statistics.
typedef struct { int dev; int spin; const char *what; } tour_phase_t;     // dev -1: switch devices; spin: 0 none, +-1 direction
#define TOUR_PHASE_MS 6000
static const tour_phase_t TOUR[] = {
    { 0, +1, "lights: spin up" },              { 0, 0, "lights: settle (arc, tick gradient, ripples step)" },
    { 0, -1, "lights: spin down" },            { 0, 0, "lights: settle" },
    { 1, +1, "aircon: spin up" },              { 1, 0, "aircon: settle" },
    { 2, 0,  "fan: idle, icon spinning" },     { 2, +1, "fan: spin up" },
    { 2, 0,  "fan: settle, icon spinning" },   { 2, -1, "fan: spin down" },
    { 3, +1, "curtains: spin up (then the panels animate)" }, { 3, 0, "curtains: animation + motor travel" },
    { 3, -1, "curtains: spin down" },          { 3, 0, "curtains: animation + motor travel" },
    { -1, 0, "device switches every 1.2 s" },
    { -2, 0, "wake replay 1: scan animation, joining Wi-Fi, landing wave" },
    { -2, 0, "wake replay 2" },
};

static void perf_tour(uint32_t now)
{
    static uint32_t start, spun, next_switch;
    static int phase = -1;
    if (!start) start = now + 3000;
    if (now < start) return;
    const int n = (int)(sizeof TOUR / sizeof TOUR[0]);
    const int p = (int)(((now - start) / TOUR_PHASE_MS) % n);
    const tour_phase_t *ph = &TOUR[p];
    if (p != phase) {
        phase = p; spun = 0; next_switch = now;
        ESP_LOGI("perf", "=== phase %d: %s", p, ph->what);
        if (ph->dev >= 0 && ph->dev != s_cur) go_to(ph->dev, +1, now);
        if (ph->dev == -2) { link_sleep(&s_link, now); link_wake(&s_link, now); }        // as if the screen slept and was woken
    }
    const uint32_t t = (now - start) % TOUR_PHASE_MS;
    if (ph->spin || t < 50) power_on_input(&s_power, now, true);     // the tour counts as user activity: stay awake
    if (ph->spin) while (spun < 60 && t >= spun * 25) { device_turn(&s_dev[s_cur], ph->spin, now); spun++; }
    if (ph->dev == -1 && now >= next_switch) { go_to(s_cur + 1, +1, now); next_switch = now + 1200; }
}
#endif

// Runs in the LVGL task every few ms: drain input, advance the model, redraw.
static void frame_cb(lv_timer_t *t)
{
    (void)t;
    input_event_t ev;
    while (input_poll(&ev)) {
        device_t *d = &s_dev[s_cur];
        if (ev.type == INPUT_TOUCH_UP) continue;
        // Every input goes through the power rules first: it may wake the screen (and then does nothing else),
        // be ignored right after a wake, or be ignored because a single touch cannot wake.
        const bool wakes = ev.type == INPUT_TURN || ev.type == INPUT_DOUBLE_TAP;
        const power_input_t pr = power_on_input(&s_power, ev.ms, wakes);
        if (pr == POWER_INPUT_WOKE_FROM_SLEEP) link_wake(&s_link, ev.ms);   // cached state is not live: reconnect, then fetch
        if (pr != POWER_INPUT_ACT) continue;
        switch (ev.type) {
        case INPUT_TURN:
#ifdef KURU_PERF
        {
            static uint32_t prev_ms;
            const uint32_t dt = ev.ms - prev_ms; prev_ms = ev.ms;
            device_turn(d, ev.dir, ev.ms);
            ESP_LOGI("turn", "dir=%+d dt=%ums rate=%.1fHz gain=%.2f req=%d", ev.dir, (unsigned)dt, d->rate_hz,
                     device_gain_for_rate(&d->def->feel, d->rate_hz), d->requested.value);
        }
#else
            device_turn(d, ev.dir, ev.ms);
#endif
            break;
        case INPUT_TOUCH_DOWN: case INPUT_TOUCH_UP: case INPUT_TAP: break;
        case INPUT_DOUBLE_TAP:
        case INPUT_SWIPE_LEFT:  go_to(s_cur + 1, +1, ev.ms); break;
        case INPUT_SWIPE_RIGHT: go_to(s_cur - 1, -1, ev.ms); break;
        case INPUT_HOLD: break;
        }
    }
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    static uint32_t last_ms;
    const uint32_t dt_ms = last_ms ? now - last_ms : 0;
    last_ms = now;
    if (power_tick(&s_power, now) && s_power.state == POWER_ASLEEP) {
        device_flush(&s_dev[s_cur], now);                  // never sleep on an unsent change
        link_sleep(&s_link, now);
    }
    power_apply(dt_ms);
    ui_set_animating(s_power.state != POWER_ASLEEP);     // the fan icon stops turning with the screen off
#ifdef KURU_SYNTH_ABLATE
    perf_ablate(now);
#endif
#ifdef KURU_SYNTH_TOUR
    perf_tour(now);
#endif
#ifdef KURU_SYNTH_SWITCH
    if (perf_switch_due(now)) { ESP_LOGI("perf", "=== switch at %u ms", (unsigned)now); go_to(s_cur + 1, +1, now); }
#endif
    link_tick(&s_link, now);
#if KURU_LINK_REAL_WIFI
    wifi_link_tick(&s_link, now);
#endif
    mock_link_tick(&s_mock, &s_link, s_dev, NUM_DEVICES, now);
    link_log(now);
    for (int i = 0; i < NUM_DEVICES; i++) {            // every device keeps syncing in the background
        device_tick(&s_dev[i], now);
        mock_ha_tick(&s_dev[i], &s_link, now);
    }
#ifdef KURU_PERF
    perf_step(now);
#endif
    ui_render(&s_dev[s_cur], now);
}

#ifdef KURU_PERF
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
// How fast is lv_malloc on this build, and where do its blocks live? (LVGL does thousands per refresh.)
static void perf_malloc_bench(void)
{
    void *p[64];
    const int64_t t0 = esp_timer_get_time();
    for (int r = 0; r < 200; r++) {
        for (int i = 0; i < 64; i++) p[i] = lv_malloc(160);
        for (int i = 0; i < 64; i++) lv_free(p[i]);
    }
    const int64_t us = esp_timer_get_time() - t0;
    void *q = lv_malloc(160);
    ESP_LOGI("perf", "lv_malloc+free x12800: %lld us (%.2f us each); sample block in %s; internal free=%u KB, psram free=%u KB",
             (long long)us, (double)us / 12800, esp_ptr_external_ram(q) ? "PSRAM" : "internal RAM",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    lv_free(q);
}
#endif

void app_main(void)
{
    board_display_init();
    board_backlight_set(255);
    for (int i = 0; i < NUM_DEVICES; i++) { device_init(&s_dev[i], DEFS[i], INITIAL[i]); s_dev[i].aux = INITIAL_AUX[i]; }
    power_init(&s_power, (uint32_t)(esp_timer_get_time() / 1000));
    link_init(&s_link);
    mock_link_init(&s_mock, KURU_LINK_SCENARIO, true, (uint32_t)esp_timer_get_time());
#if KURU_LINK_REAL_WIFI
    wifi_link_init();                                  // the radio is real; Home Assistant is still the mock (M3)
    kuru_console_start();
    mock_link_use_real_wifi(&s_mock, true);
#endif
    link_wake(&s_link, (uint32_t)(esp_timer_get_time() / 1000));      // boot is a wake: the cached state is not live yet
    input_start();
    if (board_lvgl_lock(-1)) {
#ifdef KURU_PERF
        perf_malloc_bench();
#endif
        ui_init();
        ui_set_link(&s_link);
        ui_set_look(LOOKS[s_cur], s_cur, NUM_DEVICES, &s_dev[s_cur]);
#ifdef KURU_PERF
        { lv_mem_monitor_t mm; lv_mem_monitor(&mm); ESP_LOGI("perf", "LVGL pool after UI build: used %u KB of %u KB (peak %u KB)", (unsigned)((mm.total_size - mm.free_size) / 1024), (unsigned)(mm.total_size / 1024), (unsigned)(mm.max_used / 1024)); }
#endif
        lv_timer_create(frame_cb, 8, NULL);
        board_lvgl_unlock();
    }
}
