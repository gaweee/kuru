// Status line under the value: an icon and a word (prototype .status). One line serves both the device's own commands
// and the connection.
#pragma once
#include "lvgl.h"

typedef enum {
    UI_STATUS_NONE,
    UI_STATUS_SETTING,      // pulsing dot
    UI_STATUS_SET,          // check
    UI_STATUS_OPENING,      // pulsing dot (motorised devices)
    UI_STATUS_CLOSING,
    UI_STATUS_CONNECTING,   // Wi-Fi icon, its arcs pulsing one after the other
    UI_STATUS_SYNCING,      // pulsing dot
    UI_STATUS_NO_WIFI,      // cross, in the error colour
    UI_STATUS_OFFLINE,      // Home Assistant unreachable: cross, in the error colour
    UI_STATUS_UP_TO_DATE,   // check
} ui_status_t;

void ui_status_create(lv_obj_t *scr, lv_color_t accent);
void ui_status_set_accent(lv_color_t accent);
void ui_status_update(ui_status_t kind);       // cheap when the kind is unchanged
