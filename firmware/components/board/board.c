#include "board.h"
#include "lcd_init_cmds.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_lvgl_port.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"

// Pins (docs/hardware.md): QSPI display
#define PIN_LCD_CS    14
#define PIN_LCD_PCLK  13
#define PIN_LCD_D0    15
#define PIN_LCD_D1    16
#define PIN_LCD_D2    17
#define PIN_LCD_D3    18
#define PIN_LCD_RST   21
#define PIN_LCD_BL    47
#define LCD_HOST      SPI2_HOST
#define BUF_LINES     24   // 360*24*2 B = 17 KB per buffer, internal DMA-capable RAM (RGB565)

static esp_lcd_panel_handle_t s_panel;

static void round_even_cb(lv_event_t *e)
{
    // SH8601 requires even start / odd end coordinates for partial updates.
    lv_area_t *a = lv_event_get_param(e);
    a->x1 &= ~1; a->y1 &= ~1;
    a->x2 |= 1;  a->y2 |= 1;
}

#ifdef KURU_PERF
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_freertos_hooks.h"
static volatile uint32_t s_idle0, s_idle1;
static bool idle_hook0(void) { s_idle0++; return false; }
static bool idle_hook1(void) { s_idle1++; return false; }
static uint32_t s_idle0_t0, s_idle1_t0;
static int64_t s_refr_t0, s_flush_t0;
static uint32_t s_refr_n, s_refr_us, s_flush_n, s_flush_px, s_flush_us;
static int s_tag, s_cur_tag;
static uint32_t s_refr_px, s_refr_flush_us, s_refr_flush_n;
static uint32_t s_tag_n, s_tag_us, s_tag_max, s_tag_px;
void board_perf_tag(int tag) { s_tag = tag; }
static void perf_cb(lv_event_t *e)
{
    switch (lv_event_get_code(e)) {
    case LV_EVENT_REFR_START: s_refr_t0 = esp_timer_get_time(); s_cur_tag = s_tag; s_refr_px = 0; s_refr_flush_us = 0; s_refr_flush_n = 0; s_idle0_t0 = s_idle0; s_idle1_t0 = s_idle1; break;
    case LV_EVENT_REFR_READY: {
        const uint32_t us = (uint32_t)(esp_timer_get_time() - s_refr_t0);
        s_refr_n++; s_refr_us += us;
        if (us > 15000) ESP_LOGI("slow", "refresh %u ms, %u px in %u flushes, time inside flush %u ms (avg %u us each); idle-task runs during it: core0=%u core1=%u", (unsigned)(us / 1000), (unsigned)s_refr_px, (unsigned)s_refr_flush_n, (unsigned)(s_refr_flush_us / 1000), (unsigned)(s_refr_flush_n ? s_refr_flush_us / s_refr_flush_n : 0), (unsigned)(s_idle0 - s_idle0_t0), (unsigned)(s_idle1 - s_idle1_t0));
        if (s_cur_tag == 0) { s_tag_n++; s_tag_us += us; if (us > s_tag_max) s_tag_max = us; }   // tag 0 = the knob is turning
        break; }
    case LV_EVENT_FLUSH_START: {
        const lv_area_t *a = lv_event_get_param(e);
        s_refr_px += lv_area_get_width(a) * lv_area_get_height(a);
        s_flush_px += lv_area_get_width(a) * lv_area_get_height(a); if (s_tag == 0) s_tag_px += lv_area_get_width(a) * lv_area_get_height(a); s_flush_n++; s_flush_t0 = esp_timer_get_time(); break; }
    case LV_EVENT_FLUSH_FINISH: { const uint32_t f = (uint32_t)(esp_timer_get_time() - s_flush_t0); s_flush_us += f; s_refr_flush_us += f; s_refr_flush_n++; break; }
    default: break;
    }
}
void board_perf_turning(uint32_t *n, uint32_t *us, uint32_t *max_us, uint32_t *px)
{
    *n = s_tag_n; *us = s_tag_us; *max_us = s_tag_max; *px = s_tag_px;
    s_tag_n = s_tag_us = s_tag_max = s_tag_px = 0;
}
void board_perf_take(uint32_t *refr_n, uint32_t *refr_us, uint32_t *flush_n, uint32_t *flush_px, uint32_t *flush_us)
{
    *refr_n = s_refr_n; *refr_us = s_refr_us; *flush_n = s_flush_n; *flush_px = s_flush_px; *flush_us = s_flush_us;
    s_refr_n = s_refr_us = s_flush_n = s_flush_px = s_flush_us = 0;
}
#endif

lv_display_t *board_display_init(void)
{
    const spi_bus_config_t bus = SH8601_PANEL_BUS_QSPI_CONFIG(
        PIN_LCD_PCLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3,
        BOARD_LCD_W * BUF_LINES * 2);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_spi_config_t io_cfg = SH8601_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, NULL, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));

    sh8601_vendor_config_t vendor = {
        .init_cmds = board_lcd_init_cmds,
        .init_cmds_size = sizeof(board_lcd_init_cmds) / sizeof(board_lcd_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    esp_lcd_panel_handle_t panel = NULL;
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    s_panel = panel;

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = BOARD_LCD_W * BUF_LINES,
        .double_buffer = true,
        .hres = BOARD_LCD_W,
        .vres = BOARD_LCD_H,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags = { .buff_dma = true, .swap_bytes = true },   // RGB565: DMA draw buffers, byte-swapped for the panel
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_ERROR_CHECK(disp ? ESP_OK : ESP_FAIL);
    lv_display_add_event_cb(disp, round_even_cb, LV_EVENT_INVALIDATE_AREA, NULL);
#ifdef KURU_PERF
    lv_display_add_event_cb(disp, perf_cb, LV_EVENT_ALL, NULL);
    esp_register_freertos_idle_hook_for_cpu(idle_hook0, 0);
    esp_register_freertos_idle_hook_for_cpu(idle_hook1, 1);
#endif
    return disp;
}

void board_backlight_set(uint8_t duty)
{
    static bool inited;
    if (!inited) {
        const ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
            .timer_num = LEDC_TIMER_3, .freq_hz = 50 * 1000, .clk_cfg = LEDC_AUTO_CLK };
        const ledc_channel_config_t c = { .gpio_num = PIN_LCD_BL, .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_1, .timer_sel = LEDC_TIMER_3, .duty = duty };
        ESP_ERROR_CHECK(ledc_timer_config(&t));
        ESP_ERROR_CHECK(ledc_channel_config(&c));
        inited = true;
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

bool board_lvgl_lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void board_lvgl_unlock(void) { lvgl_port_unlock(); }

// Shared I2C bus: touch (0x15) now, haptic driver later. Pins per docs/hardware.md (unverified).
#define PIN_I2C_SDA 11
#define PIN_I2C_SCL 12
i2c_master_bus_handle_t board_i2c_bus(void)
{
    static i2c_master_bus_handle_t bus;
    if (!bus) {
        const i2c_master_bus_config_t cfg = {
            .i2c_port = I2C_NUM_0, .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus));
    }
    return bus;
}

// Screen off for real: the panel stops scanning as well as the backlight going dark. The panel keeps its frame
// memory, so switching back on shows the last frame immediately.
void board_display_on(bool on)
{
    if (s_panel) esp_lcd_panel_disp_on_off(s_panel, on);
}
