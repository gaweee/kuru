#include "ui_status.h"
#include "ui_common.h"

#include "assets.h"

#define COL_ERR 0xF2907C        // prototype: .status[data-kind="err"]

static lv_obj_t *s_label, *s_pip, *s_check, *s_cross;
static lv_obj_t *s_wifi[4];                        // dot, arc 1..3 (the Wi-Fi icon, 13 x 12)
static lv_point_precise_t s_check_pts[3], s_cross_pts[5];
static ui_status_t s_kind = UI_STATUS_NONE;
static int32_t s_alpha, s_pulse = 255, s_wifi_phase;
static bool s_anim_pulse, s_anim_wifi;

typedef enum { ICON_PIP, ICON_CHECK, ICON_WIFI, ICON_CROSS } icon_t;
static icon_t s_icon = ICON_PIP;                   // the icon on screen (it keeps fading out after the kind went NONE)

static icon_t icon_of(ui_status_t k)
{
    switch (k) {
    case UI_STATUS_SET: case UI_STATUS_UP_TO_DATE: return ICON_CHECK;
    case UI_STATUS_CONNECTING: return ICON_WIFI;
    case UI_STATUS_NO_WIFI: case UI_STATUS_OFFLINE: return ICON_CROSS;
    default: return ICON_PIP;
    }
}

// Prototype @keyframes wifiArc: 1.2 s, opacity .2 -> 1 at 40 % -> .2, ease in-out; the arcs are 0, .15 and .3 s apart.
static int wifi_arc_opa(int arc)
{
    int t = (s_wifi_phase - arc * 150 + 2400) % 1200;
    const float p = t / 1200.0f;
    const float u = p < 0.4f ? p / 0.4f : 1.0f - (p - 0.4f) / 0.6f;
    const float e = u * u * (3 - 2 * u);
    return (int)((0.2f + 0.8f * e) * 255);
}

static void apply(void)
{
    // Only the icon in use is written: a write on a visible object invalidates its area, and a hidden one is free.
    lv_obj_set_style_text_opa(s_label, (lv_opa_t)s_alpha, 0);
    switch (s_icon) {
    case ICON_PIP:   lv_obj_set_style_bg_opa(s_pip, (lv_opa_t)(s_alpha * s_pulse / 255), 0); break;
    case ICON_CHECK: lv_obj_set_style_line_opa(s_check, (lv_opa_t)s_alpha, 0); break;
    case ICON_CROSS: lv_obj_set_style_line_opa(s_cross, (lv_opa_t)s_alpha, 0); break;
    case ICON_WIFI:
        lv_obj_set_style_image_opa(s_wifi[0], (lv_opa_t)s_alpha, 0);
        for (int a = 1; a < 4; a++) lv_obj_set_style_image_opa(s_wifi[a], (lv_opa_t)(s_alpha * wifi_arc_opa(a - 1) / 255), 0);
        break;
    }
}
static void alpha_cb(void *v, int32_t val) { (void)v; s_alpha = val; apply(); }
static void pulse_cb(void *v, int32_t val) { (void)v; s_pulse = val; apply(); }
static void wifi_cb(void *v, int32_t val) { (void)v; s_wifi_phase = val; apply(); }

static void fade(int32_t to)                       // prototype: opacity transition 0.2 s
{
    lv_anim_delete(&s_alpha, alpha_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_alpha);
    lv_anim_set_exec_cb(&a, alpha_cb);
    lv_anim_set_values(&a, s_alpha, to);
    lv_anim_set_duration(&a, 200);
    lv_anim_start(&a);
}

static void pulse(bool on)                         // prototype: opacity .35 <-> 1 over 1 s, ease-in-out, repeating
{
    if (on == s_anim_pulse) return;
    s_anim_pulse = on;
    lv_anim_delete(&s_pulse, pulse_cb);
    s_pulse = 255;
    if (!on) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_pulse);
    lv_anim_set_exec_cb(&a, pulse_cb);
    lv_anim_set_values(&a, 90, 255);
    lv_anim_set_duration(&a, 500);
    lv_anim_set_reverse_duration(&a, 500);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void wifi_pulse(bool on)
{
    if (on == s_anim_wifi) return;
    s_anim_wifi = on;
    lv_anim_delete(&s_wifi_phase, wifi_cb);
    s_wifi_phase = 0;
    if (!on) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_wifi_phase);
    lv_anim_set_exec_cb(&a, wifi_cb);
    lv_anim_set_values(&a, 0, 1200);
    lv_anim_set_duration(&a, 1200);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static const char *text_of(ui_status_t k)
{
    switch (k) {
    case UI_STATUS_SET: return "Set";
    case UI_STATUS_OPENING: return "Opening";
    case UI_STATUS_CLOSING: return "Closing";
    case UI_STATUS_CONNECTING: return "Connecting";
    case UI_STATUS_SYNCING: return "Syncing";
    case UI_STATUS_NO_WIFI: return "No Wi-Fi";
    case UI_STATUS_OFFLINE: return "Offline";
    case UI_STATUS_UP_TO_DATE: return "Up to date";
    default: return "Setting";
    }
}

static void show(ui_status_t kind)
{
    const icon_t icon = s_icon = icon_of(kind);
    const char *txt = text_of(kind);
    const int iw = icon == ICON_PIP ? 6 : icon == ICON_WIFI ? 13 : 12;
    const int x0 = (360 - (iw + 6 + ui_text_w(txt, &font_outfit_400_13, 0))) / 2;
    const bool err = icon == ICON_CROSS;
    lv_label_set_text(s_label, txt);
    lv_obj_set_x(s_label, x0 + iw + 6);
    lv_obj_set_style_text_color(s_label, ui_col(err ? COL_ERR : COL_STATUS), 0);
    // The unused icons are hidden, not just transparent: every animation frame writes the opacities, and a write on a
    // visible object invalidates its whole area. An lv_line is sized from the origin to its furthest point, so with
    // screen coordinates the checkmark's object spanned ~50k pixels and each pulse frame redrew them (~50 ms). Their
    // points are therefore local to the object, which sits on the 12x12 box.
    lv_obj_set_hidden(s_pip, icon != ICON_PIP);
    lv_obj_set_hidden(s_check, icon != ICON_CHECK);
    lv_obj_set_hidden(s_cross, icon != ICON_CROSS);
    for (int i = 0; i < 4; i++) lv_obj_set_hidden(s_wifi[i], icon != ICON_WIFI);
    switch (icon) {
    case ICON_CHECK: lv_obj_set_pos(s_check, x0, 272); break;           // svg 12x12 path "M2.4 6.3 5 8.6l4.6-5.2", centred on y=278
    case ICON_CROSS: lv_obj_set_pos(s_cross, x0, 272); break;
    case ICON_WIFI:  for (int i = 0; i < 4; i++) lv_obj_set_pos(s_wifi[i], x0, 272); break;
    case ICON_PIP:   lv_obj_set_pos(s_pip, x0, 275); break;
    }
    pulse(icon == ICON_PIP);
    wifi_pulse(icon == ICON_WIFI);
}

void ui_status_create(lv_obj_t *scr, lv_color_t accent)
{
    s_label = ui_label_css(scr, "", &font_outfit_400_13, COL_STATUS, 0, 268, 20, 100, LV_TEXT_ALIGN_LEFT);
    s_pip = ui_plain_obj(scr, 0, 275, 6, 6);
    lv_obj_set_style_radius(s_pip, LV_RADIUS_CIRCLE, 0);

    const float c[3][2] = { { 2.4f, 6.3f }, { 5.0f, 8.6f }, { 9.6f, 3.4f } };        // svg path, local to the 12x12 box
    for (int i = 0; i < 3; i++) { s_check_pts[i].x = (lv_value_precise_t)c[i][0]; s_check_pts[i].y = (lv_value_precise_t)c[i][1]; }
    const float x[5][2] = { { 3, 3 }, { 9, 9 }, { 6, 6 }, { 9, 3 }, { 3, 9 } };      // "M3 3l6 6M9 3 3 9" as one polyline through the centre
    for (int i = 0; i < 5; i++) { s_cross_pts[i].x = (lv_value_precise_t)x[i][0]; s_cross_pts[i].y = (lv_value_precise_t)x[i][1]; }
    s_check = lv_line_create(scr);
    lv_line_set_points(s_check, s_check_pts, 3);
    s_cross = lv_line_create(scr);
    lv_line_set_points(s_cross, s_cross_pts, 5);
    lv_obj_t *lines[2] = { s_check, s_cross };
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_line_width(lines[i], 2, 0);
        lv_obj_set_style_line_rounded(lines[i], true, 0);
        lv_obj_set_clickable(lines[i], false);
    }
    lv_obj_set_style_line_color(s_cross, ui_col(COL_ERR), 0);

    const lv_image_dsc_t *wifi[4] = { &img_wifi_dot, &img_wifi_arc1, &img_wifi_arc2, &img_wifi_arc3 };
    for (int i = 0; i < 4; i++) {
        s_wifi[i] = ui_image_at(scr, wifi[i], 0, 272);
        lv_obj_set_style_image_recolor(s_wifi[i], ui_col(COL_STATUS), 0);
        lv_obj_set_style_image_recolor_opa(s_wifi[i], LV_OPA_COVER, 0);
    }
    show(UI_STATUS_CONNECTING);                    // create everything once, then hide the lot until a status is shown
    for (int i = 0; i < 4; i++) lv_obj_set_hidden(s_wifi[i], true);
    lv_obj_set_hidden(s_pip, true); lv_obj_set_hidden(s_check, true); lv_obj_set_hidden(s_cross, true);
    wifi_pulse(false);
    ui_status_set_accent(accent);
    apply();
}

void ui_status_set_accent(lv_color_t accent)
{
    lv_obj_set_style_bg_color(s_pip, accent, 0);
    lv_obj_set_style_line_color(s_check, accent, 0);
}

void ui_status_update(ui_status_t kind)
{
    if (kind == s_kind) return;
    const ui_status_t prev = s_kind;
    s_kind = kind;
    if (kind == UI_STATUS_NONE) { fade(0); return; }
    show(kind);
    if (prev == UI_STATUS_NONE) fade(255);
    else { lv_anim_delete(&s_alpha, alpha_cb); s_alpha = 255; apply(); }    // Setting -> Set swaps instantly, like the prototype
}
