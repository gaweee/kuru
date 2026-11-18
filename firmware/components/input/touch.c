// CST816 capacitive touch (I2C 0x15). Pin numbers and address are from an unofficial mirror of Waveshare's demo and
// are not yet verified against Waveshare's documentation. If the chip is not found, touch is skipped (BOOT button remains).
#include "input_internal.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN_TOUCH_RST 10
#define TOUCH_ADDR 0x15
#define POLL_MS 10
#define TAP_SLOP_PX 12      // prototype: moved more than this and it is no longer a tap
#define SWIPE_PX 40         // prototype: horizontal travel needed for a swipe
#define HOLD_MS 500         // prototype: long press
#define DOUBLE_TAP_MS 350   // second tap must follow within this
#define DOUBLE_TAP_PX 40    // ...and land within this distance of the first

static const char *TAG = "touch";

static void touch_task(void *arg)
{
    (void)arg;
    i2c_master_bus_handle_t bus = board_i2c_bus();

    gpio_set_direction(PIN_TOUCH_RST, GPIO_MODE_OUTPUT);       // reset pulse
    gpio_set_level(PIN_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(60));

    if (i2c_master_probe(bus, TOUCH_ADDR, 200) != ESP_OK) {
        ESP_LOGW(TAG, "no device at I2C 0x%02x: touch disabled (BOOT button still works as a tap)", TOUCH_ADDR);
        vTaskDelete(NULL);
    }
    i2c_master_dev_handle_t dev;
    const i2c_device_config_t dcfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = TOUCH_ADDR, .scl_speed_hz = 400000 };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dcfg, &dev));
    const uint8_t no_sleep[2] = { 0xFE, 0x01 };                // keep the chip awake: it NAKs while in its auto-sleep
    i2c_master_transmit(dev, no_sleep, sizeof no_sleep, 100);
    ESP_LOGI(TAG, "CST816 found at 0x%02x", TOUCH_ADDR);

    bool down = false, moved = false, hold_fired = false;
    int x0 = 0, y0 = 0, x = 0, y = 0;
    int64_t t0 = 0, last_tap_ms = 0;
    int last_tap_x = 0, last_tap_y = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        uint8_t reg = 0x01, buf[6] = { 0 };                    // 0x01 gesture, 0x02 fingers, 0x03..0x06 x/y
        const bool ok = i2c_master_transmit_receive(dev, &reg, 1, buf, sizeof buf, 50) == ESP_OK;
        const bool touching = ok && buf[1] > 0;
        if (touching) {
            x = ((buf[2] & 0x0F) << 8) | buf[3];
            y = ((buf[4] & 0x0F) << 8) | buf[5];
        }
        const int64_t now = esp_timer_get_time() / 1000;
        if (touching && !down) {
            down = true; moved = false; hold_fired = false; x0 = x; y0 = y; t0 = now;
            input_push(INPUT_TOUCH_DOWN, 0);
        } else if (touching && down) {
            if (abs(x - x0) > TAP_SLOP_PX || abs(y - y0) > TAP_SLOP_PX) moved = true;
            if (!moved && !hold_fired && now - t0 >= HOLD_MS) { hold_fired = true; input_push(INPUT_HOLD, 0); }
        } else if (!touching && down) {
            down = false;
            if (!hold_fired) {
                const int dx = x - x0, dy = y - y0;
                if (moved && abs(dx) > SWIPE_PX && abs(dx) > abs(dy)) input_push(dx < 0 ? INPUT_SWIPE_LEFT : INPUT_SWIPE_RIGHT, 0);
                else if (!moved) {
                    input_push(INPUT_TAP, 0);
                    if (last_tap_ms && now - last_tap_ms <= DOUBLE_TAP_MS && abs(x - last_tap_x) <= DOUBLE_TAP_PX && abs(y - last_tap_y) <= DOUBLE_TAP_PX) {
                        input_push(INPUT_DOUBLE_TAP, 0);
                        last_tap_ms = 0;                       // a third tap starts a new pair
                    } else {
                        last_tap_ms = now; last_tap_x = x; last_tap_y = y;
                    }
                }
            }
            input_push(INPUT_TOUCH_UP, 0);
        }
    }
}

void touch_start(void)
{
    xTaskCreate(touch_task, "touch", 4096, NULL, 5, NULL);
}
