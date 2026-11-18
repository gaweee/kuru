// Everything the user can do to the controller, as one event stream.
//   TURN        encoder click (dir = +1 clockwise, -1 counter-clockwise)
//   TOUCH_DOWN  a finger touched the glass (activity; first event of every touch)
//   TOUCH_UP    the finger lifted (last event of every touch, sent after any TAP / SWIPE / DOUBLE_TAP)
//   TAP         a single tap on the glass. Always reported, so a double tap is TAP, TAP, DOUBLE_TAP.
//   DOUBLE_TAP  two taps within 350 ms and 40 px of each other (the BOOT button sends this too, as a fallback)
//   SWIPE_*     horizontal drag on the glass; LEFT means the finger moved left
//   HOLD        press and hold the glass for 0.5 s
// Gesture thresholds follow the prototype (reference/knob-ui-prototype.html).
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { INPUT_TURN, INPUT_TOUCH_DOWN, INPUT_TAP, INPUT_DOUBLE_TAP, INPUT_SWIPE_LEFT, INPUT_SWIPE_RIGHT, INPUT_HOLD, INPUT_TOUCH_UP } input_type_t;

typedef struct {
    input_type_t type;
    int8_t dir;        // TURN only
    uint32_t ms;       // esp_timer time of the event, milliseconds
} input_event_t;

void input_start(void);
bool input_poll(input_event_t *ev);   // non-blocking; false when the queue is empty
