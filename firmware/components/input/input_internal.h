#pragma once
#include "input.h"

void input_push(input_type_t type, int8_t dir);   // thread-safe, called from the input sources
void encoder_start(void);
void touch_start(void);
