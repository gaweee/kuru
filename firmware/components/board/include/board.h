// Waveshare ESP32-S3-Knob-Touch-LCD-1.8: display + backlight bring-up.
// Pin numbers: see docs/hardware.md (status per pin is tracked there).
#pragma once
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "lvgl.h"

#define BOARD_LCD_W 360
#define BOARD_LCD_H 360

lv_display_t *board_display_init(void);
void board_backlight_set(uint8_t duty);      // 0..255 via LEDC PWM on GPIO47
void board_display_on(bool on);              // panel scanning on/off (the backlight is separate)
i2c_master_bus_handle_t board_i2c_bus(void);   // created on first use
bool board_lvgl_lock(int timeout_ms);
void board_lvgl_unlock(void);

#ifdef KURU_PERF
void board_perf_tag(int tag);   // tag the refreshes that start from now on (0 = the knob is turning)
void board_perf_turning(uint32_t *n, uint32_t *us, uint32_t *max_us, uint32_t *px);
void board_perf_take(uint32_t *refr_n, uint32_t *refr_us, uint32_t *flush_n, uint32_t *flush_px, uint32_t *flush_us);
#endif
