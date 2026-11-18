// The USB console (USB Serial/JTAG, the same cable that flashes the board): commands to configure the device.
#pragma once

void kuru_console_start(void);    // starts the REPL task; call once, after wifi_link_init()
