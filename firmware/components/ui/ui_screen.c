#include "ui.h"
#include <stdio.h>
#include <string.h>
#include "assets.h"
#include "misc/lv_area_private.h"          // lv_area_intersect (used by the curtain bands, as LVGL's own image widget does)
#include "ui_common.h"
#include "ui_status.h"
#include "link.h"

#define MAX_DOTS 4

// Curtains (prototype: two 128 px panels, 52..180 and 180..308, shrinking to 20 % of that when fully open). The value counts
// how closed they are, so a higher value means wider panels (the prototype counted how open: reversed here).
#define PLEAT_X0 52
#define PLEAT_X1 308
#define PLEAT_W 128.0f
#define PLEAT_OPEN_W (PLEAT_W * 0.2f)
// The prototype's panels follow the reported position at the motor's pace (14 %/s: seven seconds end to end). Here the
// panels, the thin arc and its dot all ease together, in about a quarter of a second, to where the motor was sent
// (one value, s_act, drives all three, so they cannot drift apart). The real motor takes its own time: the status line
// ("Opening") and the "At N%" line show its actual progress.
#define PLEAT_TAU_S 0.07f

static const device_look_t *s_look;
static lv_color_t s_accent;

// ---- widgets (created once in ui_init, skinned by ui_set_look) ----
static lv_obj_t *s_pleats;                    // curtains: custom-drawn bands, see pleats_draw
static float s_pw = PLEAT_W;              // current width of each curtain panel, px
static lv_obj_t *s_ripple, *s_adot, *s_halo, *s_kdot, *s_icon;
static lv_obj_t *s_ticks[N_TICKS];
static lv_obj_t *s_arc_a[ARC_A], *s_arc_b[ARC_B], *s_arc_c[ARC_C];   // thin reported-state arc, three granularities
static lv_obj_t *s_name, *s_num, *s_unit, *s_second, *s_min, *s_max;
static lv_obj_t *s_dots[MAX_DOTS];

// ---- render state ----
static int s_tick_lv[N_TICKS];            // current colour level of each tick (-1 = unknown)
static int s_arc_deg = -1;               // degrees of arc currently shown
#ifdef KURU_PERF
static bool s_perf_no_arc;
#endif
static float s_disp = DEVICE_OFF_FRAC, s_act = DEVICE_OFF_FRAC, s_ripple_o, s_hi = 1.0f;
static char s_num_txt[16], s_unit_txt[8], s_sec_txt[32];
typedef enum { VAL_NORMAL, VAL_EDITING, VAL_STALE } val_style_t;
static int s_val_style = -1;             // colour of the number: normal, being edited (accent tint), stale (last known, not live)
static bool s_sec_err;
static uint32_t s_last_ms;
static int s_ripple_i = -1;
static float s_spin_deg;                     // fan icon rotation, degrees
static int s_spin_i;                         // rotation currently applied, in 0.1 degree units
static uint32_t s_spin_ms;
static bool s_animate = true;
// ---- connection (the wake sequence, reference/knob-ui-prototype.html "Waking up") ----
#define SCAN_TAIL 7                      // ticks lit behind the scan head
#define LAND_MS 440                      // the landing wave crosses the ring in this long
#define NOTE_MS 2800                     // "Changed from ..." stays this long
#define PI_F 3.14159265f
static const link_t *s_link;             // NULL: always live
static uint32_t s_seen_wakes, s_wake_ms, s_counted_ms;
static float s_scan = -2, s_reveal = 1;
static struct { bool on; int from, to; uint32_t start; } s_count;   // the number counting to a value that changed while away
static int s_stage;                     // 0 turning, 1 arc easing, 2 tick re-normalise, 3 ripples / idle

static const int VALUE_CSS_TOP = 114, VALUE_CSS_LH = 104, UNIT_CSS_TOP = 134, UNIT_CSS_LH = 32;

static void place_value(const char *num, const char *unit)
{
    const int nw = ui_text_w(num, &font_outfit_200_92, -3);
    const int uw = unit[0] ? ui_text_w(unit, &font_outfit_300_32, 0) : 0;
    const int x0 = (360 - (nw + (uw ? 3 + uw : 0))) / 2;
    lv_obj_set_width(s_num, nw + 10);
    lv_obj_set_width(s_unit, uw + 6);
    lv_label_set_text(s_num, num);
    lv_label_set_text(s_unit, unit);
    lv_obj_set_x(s_num, x0);
    lv_obj_set_x(s_unit, x0 + nw + 3);
}

// Where the ring's leading edge sits for a position 0..1, in tick units (may be outside 0..N-1). One tick per value
// step: the last tick is lit at 1.0. Grouped (the fan): a group of ticks per step, the handle on the group's gap.
static int group_end(const device_look_t *look, int k)            // first tick after group k (k = 0..groups)
{
    return (k * N_TICKS + look->tick_groups / 2) / look->tick_groups;
}

static float ring_pos(const device_look_t *look, float frac)
{
    const int g = look->tick_groups;
    if (!g) return frac * (N_TICKS - 1);
    // Piecewise linear through the group ends, so that speed k lights exactly the first k groups (their gap tick hidden).
    const float u = frac * g;
    if (u <= 0) return -1.0f + u * group_end(look, 1);              // below zero (off): the same slope as the first group
    int k = (int)u;
    if (k >= g) k = g - 1;
    return (float)(group_end(look, k) - 1) + (u - k) * (float)(group_end(look, k + 1) - group_end(look, k));
}

static bool is_gap(const device_look_t *look, int tick)
{
    for (int k = 1; k <= look->tick_groups; k++) if (group_end(look, k) - 1 == tick) return true;
    return false;
}

// Draws the closed-curtains scene twice, each time clipped to one panel's band. Opaque 565 blits, and a width change only
// invalidates the sliver between the old and the new edge (pleats_move), so an animation frame costs a few ms. Resizing
// two image objects would invalidate both whole panels every frame instead (~a full-screen redraw).
static void pleats_draw(lv_event_t *e)
{
    if (!s_look || !s_look->pleats) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    const lv_area_t clip = layer->_clip_area;
    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    dsc.base.layer = layer;
    dsc.src = s_look->pleats;
    lv_area_t img = { 180 - ASSET_RIPPLE_HALF, 180 - ASSET_RIPPLE_HALF, 180 + ASSET_RIPPLE_HALF - 1, 180 + ASSET_RIPPLE_HALF - 1 };
    dsc.image_area = img;
    const int w = (int)(s_pw + 0.5f);
    const lv_area_t band[2] = { { PLEAT_X0, 0, PLEAT_X0 + w - 1, 359 }, { PLEAT_X1 - w, 0, PLEAT_X1 - 1, 359 } };
    for (int i = 0; i < 2; i++) {
        if (lv_area_intersect(&layer->_clip_area, &clip, &band[i])) lv_draw_image(layer, &dsc, &img);
    }
    layer->_clip_area = clip;
}

static void pleats_move(float w)
{
    const int old = (int)(s_pw + 0.5f), now = (int)(w + 0.5f);
    s_pw = w;
    if (old == now) return;
    const int lo = old < now ? old : now, hi = old < now ? now : old;
    lv_area_t a = { PLEAT_X0 + lo, 53, PLEAT_X0 + hi - 1, 307 };         // left panel: the columns between the two edges
    lv_obj_invalidate_area(s_pleats, &a);
    a.x1 = PLEAT_X1 - hi; a.x2 = PLEAT_X1 - lo - 1;                      // right panel: mirrored
    lv_obj_invalidate_area(s_pleats, &a);
}

// frac: how closed, 0 (fully open) .. 1 (fully closed)
static float pleat_width(float frac) { return PLEAT_OPEN_W + (PLEAT_W - PLEAT_OPEN_W) * ui_clampf(frac, 0, 1); }

// The reported position as shown: for a motorised device mid-travel, where the motor is heading (it is already on its
// way there), not the reading from along the route.
static float shown_frac(const device_look_t *look, const device_t *d)
{
    const device_val_t v = look->pleats && d->moving ? (device_val_t){ true, d->moving_to } : d->reported;
    return device_frac(d->def, &v);
}

void ui_set_link(const link_t *link) { s_link = link; }

// What the screen takes as "requested". A change that arrives while the sync is still running (the fetch step of the
// wake) is held back until the link is live, then the number counts to it: until then the old value stays on screen.
static device_val_t held_request(const device_t *d, bool live)
{
    if (!live && d->has_changed && (int32_t)(d->changed_ms - s_wake_ms) >= 0 && !d->editing && !d->has_pending) return d->changed_from;
    return d->requested;
}

void ui_init(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, ui_col(COL_BG), 0);          // flat background: nothing to band at 16 bit
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);

    // The ripples (the design's "glow") with the static track ring baked in: one pre-rendered opaque image per fill
    // level, swapped as the level changes.
    s_ripple = ui_image_at(scr, NULL, 180 - ASSET_RIPPLE_HALF, 180 - ASSET_RIPPLE_HALF);

    // Curtains: bands of the closed-curtains scene over the ripple image (hidden for every other device).
    s_pleats = ui_plain_obj(scr, PLEAT_X0, 53, PLEAT_X1 - PLEAT_X0, 255);
    lv_obj_add_event_cb(s_pleats, pleats_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_set_hidden(s_pleats, true);

    // Reported-state arc: pre-baked coloured wedges at three granularities (see tools/gen_assets.py). Any arc
    // length is drawn with at most 8 + 5 + 4 images; 160 tinted one-degree segments cost ~135 ms per full redraw.
    for (int k = 0; k < ARC_A; k++) { s_arc_a[k] = ui_image_at(scr, NULL, arc_a_xy[k][0], arc_a_xy[k][1]); lv_obj_set_hidden(s_arc_a[k], true); }
    for (int k = 0; k < ARC_B; k++) { s_arc_b[k] = ui_image_at(scr, NULL, arc_b_xy[k][0], arc_b_xy[k][1]); lv_obj_set_hidden(s_arc_b[k], true); }
    for (int k = 0; k < ARC_C; k++) { s_arc_c[k] = ui_image_at(scr, NULL, arc_c_xy[k][0], arc_c_xy[k][1]); lv_obj_set_hidden(s_arc_c[k], true); }

    s_adot = ui_plain_obj(scr, 0, 0, 8, 8);                   // reported-state dot
    lv_obj_set_style_radius(s_adot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_adot, ui_col(COL_DISC), 0);
    lv_obj_set_style_border_width(s_adot, 2, 0);
    lv_obj_set_style_border_opa(s_adot, 0, 0);
    lv_obj_set_style_bg_opa(s_adot, 0, 0);

    // 48 ticks: coloured images swapped per brightness level (a tinted mask cost ~0.8 ms per draw).
    for (int i = 0; i < N_TICKS; i++) s_ticks[i] = ui_image_at(scr, NULL, tick_xy[i][0], tick_xy[i][1]);

    // Requested-state handle: halo + knob dot at radius 154.
    s_halo = ui_plain_obj(scr, 0, 0, 22, 22);
    lv_obj_set_style_radius(s_halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_halo, 0, 0);
    s_kdot = ui_plain_obj(scr, 0, 0, 11, 11);
    lv_obj_set_style_radius(s_kdot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_kdot, ui_col(COL_KNOB), 0);
    lv_obj_set_style_bg_opa(s_kdot, 0, 0);

    s_icon = ui_image_at(scr, NULL, 165, 58);                  // 30 px mask, tinted with the accent
    lv_image_set_pivot(s_icon, 15, 15);                        // the fan icon turns about its centre
    lv_obj_set_style_image_recolor_opa(s_icon, LV_OPA_COVER, 0);

    // Text. CSS tops / line-heights from the prototype.
    s_name = ui_label_css(scr, "", &font_outfit_400_16, COL_NAME, 0, 92, 24, 360, LV_TEXT_ALIGN_CENTER);
    s_num = ui_label_css(scr, "", &font_outfit_200_92, COL_VALUE, 0, VALUE_CSS_TOP, VALUE_CSS_LH, 240, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_letter_space(s_num, -3, 0);
    s_unit = ui_label_css(scr, "", &font_outfit_300_32, COL_VALUE, 0, UNIT_CSS_TOP, UNIT_CSS_LH, 60, LV_TEXT_ALIGN_LEFT);
    s_second = ui_label_css(scr, "", &font_outfit_300_15, COL_SECOND, 44, 226, 22.5f, 272, LV_TEXT_ALIGN_CENTER);
    s_min = ui_label_css(scr, "", &font_outfit_300_12, COL_RANGE, 66, 287, 18, 64, LV_TEXT_ALIGN_CENTER);
    s_max = ui_label_css(scr, "", &font_outfit_300_12, COL_RANGE, 230, 287, 18, 64, LV_TEXT_ALIGN_CENTER);

    ui_status_create(scr, lv_color_white());

    for (int i = 0; i < MAX_DOTS; i++) {
        s_dots[i] = ui_plain_obj(scr, 0, 320, 5, 5);
        lv_obj_set_style_radius(s_dots[i], 3, 0);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
    }
}

void ui_set_look(const device_look_t *look, int index, int count, const device_t *d)
{
    s_look = look;
    s_accent = ui_col(look->accent);

    // Start at the ripple level that matches this device's reported state (no fade-up through every level).
    const device_val_t *rep = &d->reported;
    const bool live = link_live(s_link);
    s_ripple_o = live && rep->on ? look->ripple(ui_clampf(device_frac(d->def, rep), 0, 1)) : 0;
    s_ripple_i = (int)(ui_clampf(s_ripple_o, 0, 1) * (ASSET_RIPPLE_LEVELS - 1) + 0.5f);
    lv_image_set_src(s_ripple, look->ripples[s_ripple_i]);
    lv_obj_set_hidden(s_pleats, !look->pleats);
    s_reveal = live ? 1.0f : 0.0f;
    s_pw = live ? pleat_width(shown_frac(look, d)) : 0;                       // curtains start where the motor is, no sweep

    for (int k = 0; k < ARC_A; k++) lv_image_set_src(s_arc_a[k], look->arc_a[k]);
    for (int k = 0; k < ARC_B; k++) lv_image_set_src(s_arc_b[k], look->arc_b[k]);
    for (int k = 0; k < ARC_C; k++) lv_image_set_src(s_arc_c[k], look->arc_c[k]);
    lv_obj_set_style_border_color(s_adot, s_accent, 0);
    lv_obj_set_style_bg_color(s_halo, s_accent, 0);
    lv_image_set_src(s_icon, look->icon);
    lv_image_set_rotation(s_icon, 0);
    s_spin_deg = 0; s_spin_i = 0;
    lv_obj_set_style_image_recolor(s_icon, s_accent, 0);
    ui_status_set_accent(s_accent);

    lv_label_set_text(s_name, look->name);
    lv_label_set_text(s_min, look->min_label);
    lv_label_set_text(s_max, look->max_label);

    for (int i = 0; i < N_TICKS; i++) {
        s_tick_lv[i] = -1;                                     // force the tick images to be re-chosen for the new accent
        lv_obj_set_hidden(s_ticks[i], is_gap(look, i));   // grouped ring: gaps
    }
    s_num_txt[0] = s_sec_txt[0] = '\x01';                     // force text refresh
    s_val_style = -1;                                         // force the number colour
    s_sec_err = false;
    lv_obj_set_style_text_color(s_second, ui_col(COL_SECOND), 0);
    s_count.on = false;
    s_counted_ms = d->changed_ms;                             // an older change is not counted to again

    // Page dots: the active one is a 16 px pill in the accent colour.
    if (count > MAX_DOTS) count = MAX_DOTS;
    int x = (360 - (16 + (count - 1) * (5 + 6))) / 2;
    for (int i = 0; i < MAX_DOTS; i++) {
        lv_obj_set_hidden(s_dots[i], i >= count);
        if (i >= count) continue;
        const int w = i == index ? 16 : 5;
        lv_obj_set_pos(s_dots[i], x, 320);
        lv_obj_set_width(s_dots[i], w);
        lv_obj_set_style_bg_color(s_dots[i], i == index ? s_accent : ui_col(COL_DOT_OFF), 0);
        x += w + 6;
    }
}

// Switching devices is one redraw, with no slide and no sweep. The prototype slides the text out and in and eases
// the ring to the new state; on this panel that was a second of heavy frames (the 92 px number alone is ~10k pixels
// per frame to fade or move, and easing the ring repeatedly steps the ripple image, a big redraw each time).
void ui_switch_device(const device_look_t *look, int index, int count, const device_t *d, int dir)
{
    (void)dir;
    ui_set_look(look, index, count, d);
    const bool live = link_live(s_link);
    const device_val_t held = held_request(d, live);
    const float req = device_frac(d->def, &held), rep = live ? shown_frac(look, d) : DEVICE_OFF_FRAC;
    s_disp = req;                                         // snap: the ring, arc, tick gradient and ripples already match
    s_act = rep;
    s_hi = fmaxf(ring_pos(look, req), 1.0f);
}

// Show exactly `deg` degrees of arc from -135 deg using the fewest images: whole 30-degree wedges, then 5-degree
// wedges, then 1-degree wedges. Only objects whose visibility changes are touched.
static void set_arc_degrees(int deg)
{
#ifdef KURU_PERF
    if (s_perf_no_arc) return;
#endif
    if (deg == s_arc_deg) return;
    s_arc_deg = deg;
    const int a = deg / 30, r = deg % 30, b = r / 5, c = r % 5;
    for (int k = 0; k < ARC_A; k++) lv_obj_set_hidden(s_arc_a[k], k >= a);
    const int b0 = a * 6;                                  // first 5-degree slot after the 30-degree wedges
    for (int k = 0; k < ARC_B; k++) lv_obj_set_hidden(s_arc_b[k], !(k >= b0 && k < b0 + b));
    const int c0 = a * 30 + b * 5;                         // first 1-degree slot after the 5-degree wedges
    for (int k = 0; k < ARC_C; k++) lv_obj_set_hidden(s_arc_c[k], !(k >= c0 && k < c0 + c));
}

void ui_render(const device_t *d, uint32_t now_ms)
{
    if (!s_look) return;
    const device_look_t *look = s_look;
    const bool live = link_live(s_link);

    // A wake (or the first frame after boot): the ring starts from "off" and sweeps up to the last known state, while
    // what only live state can tell (the thin arc, the ripples, the curtain pleats) is hidden until the link is live.
    if (s_link && s_link->wakes != s_seen_wakes) {
        s_seen_wakes = s_link->wakes;
        s_wake_ms = now_ms;
        s_disp = s_act = DEVICE_OFF_FRAC;
        s_ripple_o = 0;
        s_reveal = 0;
        s_scan = -2;
        s_count.on = false;
    }

    const device_val_t held = held_request(d, live);
    const float req_frac = device_frac(d->def, &held);
    const float rep_frac = live ? shown_frac(look, d) : DEVICE_OFF_FRAC;

    // Frame-rate independent easing, same constants as the prototype (k at 60 fps: 0.2 / 0.12).
    float dt = (now_ms - s_last_ms) / 1000.0f;
    s_last_ms = now_ms;
    if (dt > 0.5f) dt = 0.5f;
    const float k1 = 1 - powf(0.8f, dt * 60), k2 = 1 - powf(0.88f, dt * 60);
    const bool landing = live && s_link && now_ms - s_link->live_at_ms < 900;      // prototype: the arc draws back in a little slower
    s_disp += (req_frac - s_disp) * k1;
    s_act += (rep_frac - s_act) * (look->pleats ? 1.0f - expf(-dt / PLEAT_TAU_S) : landing ? k2 * 0.8f : k2);

    // The tick brightness gradient is normalised to the handle position (prototype look). Re-normalising
    // changes every lit tick, which is expensive, so it is frozen while the knob moves: only the leading-edge
    // ticks change per frame. After a turn settles the expensive things happen one after another instead of at once:
    //   1. the reported arc eases (cheap)   2. tick gradient re-normalises (one frame)   3. ripple steps.
    // The first 1.5 s (wake sweep) is exempt so the ring can animate in together.
    const float L = ring_pos(look, s_disp);
    const bool settled = !d->editing && !d->has_pending;
    const bool arc_done = fabsf(s_act - rep_frac) < 0.003f || now_ms < 1500;
    const float want_hi = L > 1.0f ? L : 1.0f;
    if (settled && arc_done) s_hi = want_hi;
    const bool hi_done = fabsf(s_hi - want_hi) < 0.05f;
    s_stage = !settled ? 0 : !arc_done ? 1 : !hi_done ? 2 : 3;

    // The ticks. Not live: the last known state shows muted ("ghost": 30 % of the brightness) while a scan runs along
    // the ring, in white for Wi-Fi and in the accent for Home Assistant. A change the user queued shows at full
    // brightness. Going live: a wave lights the ring from the start (a white-ish front, then the accent). Every colour
    // comes from a pre-rendered ramp (unlit -> accent, per device; unlit -> white, shared).
    const bool intent = d->editing || (d->has_pending && !live);
    const bool scanning = link_connecting(s_link);
    const bool scan_ink = scanning && s_link->state == LINK_WIFI;
    if (scanning) {
        s_scan += dt * (scan_ink ? 46.0f : 64.0f);
        if (s_scan > N_TICKS + SCAN_TAIL) s_scan = -2;
    }
    const float wave = live ? (s_link ? (now_ms - s_link->live_at_ms) / (float)LAND_MS * (N_TICKS + 4) : 1000.0f) : -10.0f;
    const float ink_bright = 0.8f;                  // an ink tick vs an accent tick of the same ramp position, roughly
    for (int i = 0; i < N_TICKS; i++) {
        const float x = ui_clampf(L - i + 1, 0, 1);
        const float g = 0.38f + 0.62f * powf(ui_clampf(i / s_hi, 0, 1), 1.3f);
        const float lw = live ? ui_clampf((wave - i) / 3, 0, 1) : intent ? 1.0f : 0.0f;
        float u = x * g * (0.3f + 0.7f * lw);       // prototype swatch(UNLIT, accent, x*g), mixed ghost -> full by lw
        bool ink = false;
        const float w = wave - i;
        if (live && x > 0 && w > 0 && w < 3) { const float h = 0.45f * (1 - w / 3); u = h + (1 - h) * u * ink_bright; ink = true; }
        if (scanning) {
            const float sd = s_scan - i;
            if (sd >= 0 && sd <= SCAN_TAIL) {
                const float a = 0.85f * powf(1 - sd / SCAN_TAIL, 1.6f);
                if (scan_ink) { u = a + (1 - a) * (ink ? u : u * ink_bright); ink = true; }
                else u = u + (1 - u) * a;
            }
        }
        const int lv = (int)(ui_clampf(u, 0, 1) * (ASSET_TICK_LEVELS - 1) + 0.5f);
        const int enc = ink ? 100 + lv : lv;
        if (enc != s_tick_lv[i]) { s_tick_lv[i] = enc; lv_image_set_src(s_ticks[i], (ink ? ticks_ink : look->ticks)[lv * N_TICKS + i]); }
    }

    // Every setter below is guarded: LVGL invalidates (and redraws) on any style/position write,
    // even with an unchanged value, and a full-screen redraw costs hundreds of ms on this panel.
    static int kx = -999, ky, ko = -1, ho_i = -1, ax_i = -999, ay_i, ad_o = -1, kd_i = 11, hd_i = 22;
    float hx, hy;
    const float hc = ui_clampf(L, 0, N_TICKS - 1);
    ui_polar(154, A0 + hc * SA, &hx, &hy);
    const float ho = ui_clampf((s_disp + 0.05f) / 0.05f, 0, 1);
    // The handle: dim while the state is not live, then it pops as the landing wave passes it.
    const float h_live = live ? ui_clampf((wave - hc) / 3, 0, 1) : intent ? 1.0f : 0.0f;
    const float pop = live && wave - hc > 0 && wave - hc < 6 ? sinf(PI_F * (wave - hc) / 6) : 0.0f;
    const int nkd = (int)(2 * (5.5f + 2.5f * pop) + 0.5f), nhd = (int)(2 * (11 + 5 * pop) + 0.5f);
    const int nkx = (int)(hx - nkd / 2.0f), nky = (int)(hy - nkd / 2.0f), nko = (int)(255 * ho * (0.35f + 0.65f * h_live)), nho = (int)(255 * 0.22f * ho * h_live);
    if (nkd != kd_i || nhd != hd_i) { kd_i = nkd; hd_i = nhd; lv_obj_set_size(s_kdot, nkd, nkd); lv_obj_set_size(s_halo, nhd, nhd); kx = -999; }
    if (nkx != kx || nky != ky) { kx = nkx; ky = nky; lv_obj_set_pos(s_kdot, kx, ky); lv_obj_set_pos(s_halo, (int)(hx - nhd / 2.0f), (int)(hy - nhd / 2.0f)); }
    if (nko != ko) { ko = nko; lv_obj_set_style_bg_opa(s_kdot, (lv_opa_t)ko, 0); }
    if (nho != ho_i) { ho_i = nho; lv_obj_set_style_bg_opa(s_halo, (lv_opa_t)ho_i, 0); }

    // Reported state: thin arc + dot (not shown until live: s_act sits at "off").
    const float ac = ui_clampf(ring_pos(look, s_act), 0, N_TICKS - 1);
    const float ao = ui_clampf((s_act + 0.05f) / 0.05f, 0, 1);
    const int ndeg = (int)(ac / (N_TICKS - 1) * 270 + 0.5f), nado = (int)(255 * ao);
    set_arc_degrees(ndeg);
    float ax, ay;
    ui_polar(134, A0 + ac * SA, &ax, &ay);
    if ((int)(ax - 4) != ax_i || (int)(ay - 4) != ay_i) { ax_i = (int)(ax - 4); ay_i = (int)(ay - 4); lv_obj_set_pos(s_adot, ax_i, ay_i); }
    if (nado != ad_o) { ad_o = nado; lv_obj_set_style_bg_opa(s_adot, (lv_opa_t)nado, 0); lv_obj_set_style_border_opa(s_adot, (lv_opa_t)nado, 0); }

    // Curtain panels follow the same eased value as the arc (s_act), so the two always move together. Not live they are
    // not there; going live they slide in from the sides (s_reveal) while moving to the reported position.
    if (look->pleats) {
        s_reveal += ((live ? 1.0f : 0.0f) - s_reveal) * (1.0f - expf(-dt / 0.12f));
        if (fabsf((live ? 1.0f : 0.0f) - s_reveal) < 0.01f) s_reveal = live ? 1.0f : 0.0f;
        pleats_move(pleat_width(s_act) * s_reveal);
    }

    // Ripples: a big layer, so frozen while the knob moves; they fill toward the reported state once settled.
    if (settled && arc_done && hi_done) {
        const float rip_t = live && s_act > -0.02f ? look->ripple(ui_clampf(s_act, 0, 1)) : 0;
        s_ripple_o += (rip_t - s_ripple_o) * k2;
    }
    const int nrl = (int)(ui_clampf(s_ripple_o, 0, 1) * (ASSET_RIPPLE_LEVELS - 1) + 0.5f);
    if (nrl != s_ripple_i) { s_ripple_i = nrl; lv_image_set_src(s_ripple, look->ripples[nrl]); }

    // Fan icon: turns while the reported speed is above zero, 2.2 / speed seconds per turn (prototype). Updated at most
    // every 30 ms and in 3 degree steps; a rotated icon is a small redraw, not worth doing every frame.
    if (look->spins) {
        const bool spin = live && d->reported.on && d->reported.value > 0;
        int rot = s_spin_i;
        if (!spin) { rot = 0; s_spin_deg = 0; s_spin_ms = now_ms; }
        else if (s_animate && now_ms - s_spin_ms >= 30) {
            // The prototype is drawn for 6 speeds (2.2 / speed seconds per turn); scale so the top speed keeps its pace.
            const float eq = d->reported.value * 6.0f / d->def->max;
            s_spin_deg = fmodf(s_spin_deg + (now_ms - s_spin_ms) * 0.36f * eq / 2.2f, 360.0f);   // 360 deg per 2.2/eq s
            s_spin_ms = now_ms;
            rot = (int)(s_spin_deg / 3.0f) * 30;
        } else if (!s_animate) s_spin_ms = now_ms;
        if (rot != s_spin_i) { s_spin_i = rot; lv_image_set_rotation(s_icon, rot); }
    }

    // A value that changed while away (at the wall, or while this slept) is counted to, 120 ms after the link is live.
    if (live && d->has_changed && d->changed_ms != s_counted_ms) {
        s_counted_ms = d->changed_ms;
        if (d->def->power != DEVICE_POWER_EXPLICIT && !d->editing && !d->has_pending && !device_val_equal(&d->changed_from, &d->requested)) {
            s_count.on = true;
            s_count.from = d->changed_from.on ? d->changed_from.value : d->def->min;
            s_count.to = d->requested.on ? d->requested.value : d->def->min;
            s_count.start = now_ms + 120;
        }
    }
    device_val_t numv = held;
    if (s_count.on) {
        const float p = ui_clampf((int32_t)(now_ms - s_count.start) / 650.0f, 0, 1), e = 1 - powf(1 - p, 3.0f);
        numv.value = (int)(s_count.from + (s_count.to - s_count.from) * e + 0.5f);
        numv.on = d->def->power != DEVICE_POWER_OFF_AT_MIN || numv.value > d->def->min;
        if (p >= 1) s_count.on = false;
    }

    // Text only when it changes.
    char num[16], unit[8], sec[32];
    look->value_text(&numv, num, sizeof num, unit, sizeof unit);
    if (strcmp(num, s_num_txt) || strcmp(unit, s_unit_txt)) {
        strcpy(s_num_txt, num); strcpy(s_unit_txt, unit);
        place_value(num, unit);
    }
    const bool editing = d->editing || d->has_pending;       // being set, or queued until the link is live
    const int vstyle = editing ? VAL_EDITING : !live ? VAL_STALE : VAL_NORMAL;
    if (vstyle != s_val_style) {
        s_val_style = vstyle;
        const lv_color_t c = vstyle == VAL_EDITING ? ui_mix(s_accent, ui_col(0xFFFFFF), 0.22f)   // prototype: accent 78 % + white 22 %
                           : vstyle == VAL_STALE ? ui_col(0x8C8479) : ui_col(COL_VALUE);
        lv_obj_set_style_text_color(s_num, c, 0);
        lv_obj_set_style_text_color(s_unit, vstyle == VAL_STALE ? ui_col(0x6B645B) : c, 0);
    }

    // The line under the number, in the prototype's order of precedence.
    bool err = false;
    uint32_t note_from = d->changed_ms;
    if (s_link && (int32_t)(s_link->live_at_ms - note_from) > 0) note_from = s_link->live_at_ms;
    if (look->off_hint && device_off_armed(d, now_ms)) {
        snprintf(sec, sizeof sec, "%s", look->off_hint);              // overrides the line until the arm expires
    } else if (!live && editing) {
        snprintf(sec, sizeof sec, "Sends when connected");
    } else if (editing) {
        char from[16];
        look->short_text(&d->reported, from, sizeof from);
        snprintf(sec, sizeof sec, "From %s", from);
    } else if (s_link && s_link->state == LINK_NO_WIFI) {
        snprintf(sec, sizeof sec, "Showing last known state");
    } else if (s_link && s_link->state == LINK_NO_HA) {
        snprintf(sec, sizeof sec, "Can't reach Home Assistant");
        err = true;
    } else if (!live && now_ms - s_wake_ms > 600) {
        snprintf(sec, sizeof sec, "Last known");
    } else if (live && d->has_changed && (int32_t)(now_ms - note_from) < NOTE_MS) {
        char from[16];
        look->short_text(&d->changed_from, from, sizeof from);
        snprintf(sec, sizeof sec, "Changed from %s", from);
    } else {
        look->context(d, sec, sizeof sec);
    }
    if (strcmp(sec, s_sec_txt)) { strcpy(s_sec_txt, sec); lv_label_set_text(s_second, sec); }
    if (err != s_sec_err) { s_sec_err = err; lv_obj_set_style_text_color(s_second, ui_col(err ? 0xF2907C : COL_SECOND), 0); }

    // The status line: the connection's words while it is not live, then the device's own commands, then "Up to date".
    ui_status_t st = UI_STATUS_NONE;
    if (!live) {
        switch (link_status(s_link)) {
        case LINK_STATUS_CONNECTING: st = UI_STATUS_CONNECTING; break;
        case LINK_STATUS_SYNCING:    st = UI_STATUS_SYNCING; break;
        case LINK_STATUS_NO_WIFI:    st = UI_STATUS_NO_WIFI; break;
        case LINK_STATUS_OFFLINE:    st = UI_STATUS_OFFLINE; break;
        default: break;
        }
    } else {
        switch (device_status(d, now_ms)) {
        case DEVICE_STATUS_SETTING: st = UI_STATUS_SETTING; break;
        case DEVICE_STATUS_SET:     st = UI_STATUS_SET; break;
        case DEVICE_STATUS_OPENING: st = UI_STATUS_OPENING; break;
        case DEVICE_STATUS_CLOSING: st = UI_STATUS_CLOSING; break;
        default: if (link_up_to_date(s_link, now_ms)) st = UI_STATUS_UP_TO_DATE; break;
        }
    }
    ui_status_update(st);
}

int ui_stage(void) { return s_stage; }

void ui_set_animating(bool on) { s_animate = on; }

#ifdef KURU_PERF
#include "esp_heap_caps.h"
// Perf aid: hide one more group of widgets per call (cumulative) so a forced full redraw shows each group's cost.
void ui_perf_ablate(int step)
{
    switch (step) {
    case 1: s_perf_no_arc = true; for (int k = 0; k < ARC_A; k++) lv_obj_set_hidden(s_arc_a[k], true);
            for (int k = 0; k < ARC_B; k++) lv_obj_set_hidden(s_arc_b[k], true);
            for (int k = 0; k < ARC_C; k++) lv_obj_set_hidden(s_arc_c[k], true); break;
    case 2: for (int i = 0; i < N_TICKS; i++) lv_obj_set_hidden(s_ticks[i], true); break;
    case 3: { lv_obj_t *o[] = { s_name, s_num, s_unit, s_second, s_min, s_max, s_icon };
              for (unsigned i = 0; i < sizeof o / sizeof o[0]; i++) lv_obj_set_hidden(o[i], true); break; }
    case 4: { lv_obj_t *o[] = { s_adot, s_halo, s_kdot, s_dots[0], s_dots[1] };
              for (unsigned i = 0; i < sizeof o / sizeof o[0]; i++) lv_obj_set_hidden(o[i], true); break; }
    case 5: lv_obj_set_hidden(s_ripple, true); break;           // the ripple image
    case 6: { lv_obj_t *scr = lv_screen_active(); for (uint32_t i = 0; i < lv_obj_get_child_count(scr); i++) lv_obj_set_hidden(lv_obj_get_child(scr, i), true); break; }   // nothing but the screen background                 // leaves only the black screen fill
    default: break;
    }
}
#endif
