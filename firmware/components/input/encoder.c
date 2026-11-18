#include "input_internal.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define PIN_A 8            // docs/hardware.md: unverified, from community mirror of Waveshare demo
#define PIN_B 7
#define POLL_US 3000       // matches Waveshare driver
#define DEBOUNCE_TICKS 2

typedef struct { uint8_t prev, deb; int8_t dir; } chan_t;
static chan_t s_a = { .dir = +1 }, s_b = { .dir = -1 };

// Same decision logic as Waveshare's bidi_switch_knob.c: an event fires on a debounced low->high edge.
static void channel(chan_t *c, uint8_t level)
{
    if (level == 0) {
        if (c->prev != 0) c->deb = 0; else c->deb++;
    } else if (c->prev != level && ++c->deb >= DEBOUNCE_TICKS) {
        c->deb = 0;
        input_push(INPUT_TURN, c->dir);
    } else {
        c->deb = 0;
    }
    c->prev = level;
}

static void poll_cb(void *arg)
{
    (void)arg;
    channel(&s_a, gpio_get_level(PIN_A));
    channel(&s_b, gpio_get_level(PIN_B));
}

void encoder_start(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_A) | (1ULL << PIN_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    s_a.prev = gpio_get_level(PIN_A);
    s_b.prev = gpio_get_level(PIN_B);
    const esp_timer_create_args_t t = { .callback = poll_cb, .name = "encoder", .dispatch_method = ESP_TIMER_TASK };
    esp_timer_handle_t h;
    ESP_ERROR_CHECK(esp_timer_create(&t, &h));
    ESP_ERROR_CHECK(esp_timer_start_periodic(h, POLL_US));
}
