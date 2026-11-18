#!/bin/sh
# Builds and runs the host-side tests (pure C, no hardware). Usage: tests/run.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p tests/build
CC="${CC:-cc} -Wall -Wextra -Werror -I firmware/components/controller/include -I firmware/components/power/include -I firmware/components/network/include"
$CC tests/controller/feel.c   firmware/components/controller/device.c firmware/components/controller/devices.c -o tests/build/feel -lm
$CC tests/controller/aircon.c firmware/components/controller/device.c firmware/components/controller/devices.c -o tests/build/aircon -lm
$CC tests/controller/fan.c   firmware/components/controller/device.c firmware/components/controller/devices.c -o tests/build/fan -lm
$CC tests/controller/curtains.c firmware/components/controller/device.c firmware/components/controller/devices.c firmware/components/controller/link.c firmware/components/network/mock_ha.c -o tests/build/curtains -lm
$CC tests/controller/link.c firmware/components/controller/device.c firmware/components/controller/devices.c firmware/components/controller/link.c firmware/components/network/mock_ha.c firmware/components/network/mock_link.c -o tests/build/link -lm
$CC tests/controller/wifi.c firmware/components/controller/device.c firmware/components/controller/devices.c firmware/components/controller/link.c firmware/components/network/mock_link.c firmware/components/network/wifi_creds.c -o tests/build/wifi -lm
$CC tests/controller/power.c firmware/components/power/power.c -o tests/build/power
$CC tests/controller/status.c firmware/components/controller/device.c firmware/components/controller/devices.c -o tests/build/status -lm
tests/build/feel
tests/build/fan
tests/build/curtains
tests/build/link
tests/build/wifi
tests/build/status
tests/build/aircon
tests/build/power
