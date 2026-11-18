// Internal helpers and layout constants shared by the ui/ sources. Geometry and colours are copied from reference/knob-ui-prototype.html.
#pragma once
#include <math.h>
#include "lvgl.h"
#include "fonts.h"

#define N_TICKS 48
#define ARC_A 9      // 30-degree wedges
#define ARC_B 54     // 5-degree wedges
#define ARC_C 270    // 1-degree wedges
#define A0 (-135.0f)
#define A1 (135.0f)
#define SA ((A1 - A0) / (N_TICKS - 1))

// Colours from reference/knob-ui-prototype.html. Every colour goes through ui_col(), which snaps it to the nearest RGB565 value
// exactly like the prototype's snapRGB(), so what the panel shows is what the prototype shows.
#define COL_BG       0x181410
#define COL_TICK     0x393429      // unlit tick
#define COL_DISC     0x181410
#define COL_KNOB     0xF8F3EA
#define COL_NAME     0xD2C9BD
#define COL_VALUE    0xF6F0E7
#define COL_SECOND   0x9C9387
#define COL_RANGE    0x7D7469
#define COL_STATUS   0xC2B8AB
#define COL_DOT_OFF  0x4A433B

lv_color_t ui_col(uint32_t hex);                              // 0xRRGGBB snapped to the RGB565 palette
lv_color_t ui_snap(lv_color_t c);
void ui_polar(float r, float deg, float *x, float *y);        // centre (180,180); deg clockwise from 12 o'clock
int ui_text_w(const char *s, const lv_font_t *f, int spacing);
float ui_clampf(float v, float a, float b);
lv_color_t ui_mix(lv_color_t a, lv_color_t b, float t);
lv_obj_t *ui_plain_obj(lv_obj_t *parent, int x, int y, int w, int h);
lv_obj_t *ui_image_at(lv_obj_t *parent, const lv_image_dsc_t *img, int x, int y);
// Place a label so its glyphs sit where CSS would put them for a given line-height.
lv_obj_t *ui_label_css(lv_obj_t *parent, const char *txt, const lv_font_t *f, uint32_t col,
                       int x, float css_top, float css_lh, int w, lv_text_align_t al);
