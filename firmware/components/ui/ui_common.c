#include "ui_common.h"

// Nearest 5/6-bit level, expanded back to 8 bits (prototype rep5 / rep6).
static uint8_t rep5(int v) { const int q = (v * 31 + 127) / 255; return (uint8_t)((q << 3) | (q >> 2)); }
static uint8_t rep6(int v) { const int q = (v * 63 + 127) / 255; return (uint8_t)((q << 2) | (q >> 4)); }

lv_color_t ui_snap(lv_color_t c) { return lv_color_make(rep5(c.red), rep6(c.green), rep5(c.blue)); }
lv_color_t ui_col(uint32_t hex) { return ui_snap(lv_color_hex(hex)); }

void ui_polar(float r, float deg, float *x, float *y)
{
    const float t = deg * (float)M_PI / 180.0f;
    *x = 180.0f + r * sinf(t);
    *y = 180.0f - r * cosf(t);
}

// Advance width of a short ASCII string, with extra per-glyph spacing (CSS letter-spacing).
int ui_text_w(const char *s, const lv_font_t *f, int spacing)
{
    int w = 0;
    for (; *s; s++) w += lv_font_get_glyph_width(f, (uint8_t)*s, (uint8_t)s[1]) + spacing;
    return w;
}

float ui_clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

// Mix of two colours snapped to the palette (prototype swatch()).
lv_color_t ui_mix(lv_color_t a, lv_color_t b, float t)
{
    t = ui_clampf(t, 0, 1);
    return ui_snap(lv_color_make((uint8_t)(a.red + (b.red - a.red) * t + 0.5f), (uint8_t)(a.green + (b.green - a.green) * t + 0.5f),
                                 (uint8_t)(a.blue + (b.blue - a.blue) * t + 0.5f)));
}

lv_obj_t *ui_plain_obj(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

lv_obj_t *ui_image_at(lv_obj_t *parent, const lv_image_dsc_t *img, int x, int y)
{
    lv_obj_t *i = lv_image_create(parent);
    if (img) lv_image_set_src(i, img);
    lv_obj_set_pos(i, x, y);
    lv_obj_set_clickable(i, false);
    return i;
}

lv_obj_t *ui_label_css(lv_obj_t *parent, const char *txt, const lv_font_t *f, uint32_t col,
                       int x, float css_top, float css_lh, int w, lv_text_align_t al)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, ui_col(col), 0);
    lv_obj_set_style_text_align(l, al, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(l, w);
    lv_obj_set_pos(l, x, (int)(css_top + (css_lh - lv_font_get_line_height(f)) / 2.0f + 0.5f));
    return l;
}
