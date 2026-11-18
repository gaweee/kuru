# Hardware facts

The only supported board is the **Waveshare ESP32-S3-Knob-Touch-LCD-1.8** (1.8" round-corner 360 x 360 display with a rotary
knob). This page separates what has been **checked on a real board** from what is **taken from other sources** and still
unverified. Pin numbers in the source (`components/board`, `components/input`) are the ones listed here.

## Verified on the device

| What | Fact |
|---|---|
| SoC | ESP32-S3 (chip revision v0.2), 240 MHz, dual core |
| Memory | 8 MB octal PSRAM at 80 MHz; 16 MB flash (Boya), QIO |
| Display | SH8601 controller over quad SPI, 360 x 360, accepts RGB565 (the firmware runs at 16 bit) |
| Display pins | CS 14, PCLK 13, D0..D3 15..18, RST 21 |
| Touch | CST816 on I2C address 0x15, SDA 11, SCL 12, reset 10. Tap, double tap, swipe and hold are recognised in software from the raw touch points |
| Knob | Two plain pulse contacts on GPIO 8 and 7, **not a quadrature encoder**: each contact gives one event per detent, and the direction is which contact fired. No push switch on the knob |
| BOOT button | GPIO 0 (the firmware also uses it as a "next device" button) |
| Wi-Fi | Works on the S3: 2.4 GHz only, WPA2 / WPA3 / open networks |
| USB | USB Serial/JTAG (flashing, logs and the `kuru>` console) |

## From Waveshare's wiki (read, not all independently measured)

- The board carries **two MCUs**: the ESP32-S3R8 (this firmware) and a plain **ESP32-U4WDH**.
- There is **one USB-C port** and an analog switch (CH445P). **The orientation of the Type-C plug selects which MCU the cable
  reaches**: one way it is the S3's native USB (`/dev/cu.usbmodem...`, Espressif vendor id 0x303A), the other way it is the ESP32
  behind a USB-to-UART chip (`/dev/cu.usbserial...`, CH340). If flashing fails with "This chip is ESP32, not ESP32-S3", flip the plug.
- The S3's BOOT button forces download mode: hold it while powering on. After flashing in download mode, power-cycle.

## Not verified (do not rely on these)

- **GPIO 47 as backlight PWM**, taken from a community mirror of Waveshare's demo. The firmware drives it (dimming and screen-off
  both use it) but whether it really dims the panel has not been measured. The panel is also switched off with display commands.
- The SH8601 **init command table** (`components/board/lcd_init_cmds.h`) comes from the same mirror, not from Waveshare's own download.
  It works on the board. Its licence is not known (see the README).
- **Haptics**: a motor driver (DRV2605 was suggested) is not documented, so the interaction design lists haptic events but none
  are produced.
- **Battery** and power gating, and what the second MCU (ESP32-U4WDH) is meant to do, are not investigated.
- Screen-off is "screen power only": the CPU and the Wi-Fi radio keep running. Deep sleep is not implemented.
