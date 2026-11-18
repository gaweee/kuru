#include "input_internal.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define PIN_BOOT 0        // BOOT button: a fallback "next device" in case the touch controller is not found
static const char *TAG = "input";
static QueueHandle_t s_q;

void input_push(input_type_t type, int8_t dir)
{
    const input_event_t ev = { .type = type, .dir = dir, .ms = (uint32_t)(esp_timer_get_time() / 1000) };
    xQueueSend(s_q, &ev, 0);
}

bool input_poll(input_event_t *ev) { return xQueueReceive(s_q, ev, 0) == pdTRUE; }

static void button_poll(void *arg)
{
    (void)arg;
    static int low_ticks;
    if (gpio_get_level(PIN_BOOT) == 0) {
        if (++low_ticks == 3) input_push(INPUT_DOUBLE_TAP, 0);          // 30 ms debounce, fires once per press
    } else {
        low_ticks = 0;
    }
}

void input_start(void)
{
    s_q = xQueueCreate(32, sizeof(input_event_t));
    encoder_start();
    touch_start();
    const gpio_config_t cfg = { .pin_bit_mask = 1ULL << PIN_BOOT, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    const esp_timer_create_args_t t = { .callback = button_poll, .name = "boot_btn", .dispatch_method = ESP_TIMER_TASK };
    esp_timer_handle_t h;
    ESP_ERROR_CHECK(esp_timer_create(&t, &h));
    ESP_ERROR_CHECK(esp_timer_start_periodic(h, 10000));
    ESP_LOGI(TAG, "input started (encoder, touch, BOOT button)");
}
