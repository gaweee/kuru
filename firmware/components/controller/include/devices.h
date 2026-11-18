// Device definitions. To add a device: define it here, add its look in ui/looks.c,
// add it to tools/gen_assets.py, and register it in main.c (see docs/architecture.md).
#pragma once
#include "device.h"

extern const device_def_t DEVICE_LIGHTS;
extern const device_def_t DEVICE_AIRCON;
extern const device_def_t DEVICE_FAN;
extern const device_def_t DEVICE_CURTAINS;
