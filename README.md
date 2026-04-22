# esp32-bt-ps2

ESP32-C3 SuperMini firmware for bridging a BLE mouse to a PS/2 mouse port.

This project is aimed at a Toshiba Libretto 50CT installation where the ESP32
is wired directly to the dock connector and drives the PS/2 mouse clock and
data lines from GPIO pins.

## Important Compatibility Note

The ESP32-C3 supports Bluetooth Low Energy (`BLE`) but not Bluetooth Classic.
This project targets `BLE HID` mice only. If your mouse is Bluetooth Classic
HID only, the ESP32-C3 is not the right pairing target.

## Project Status

The repository has been converted from an Arduino sketch layout to an
`ESP-IDF` project layout.

Current native pieces:

- `ESP-IDF` build files and `main/` component structure
- Bit-banged PS/2 mouse transmitter using IDF GPIO APIs
- Native NimBLE scanner for BLE mouse discovery
- `esp_hidh`-based HID host connection path for BLE mice
- Serial logging through the normal `ESP-IDF` monitor flow

Remaining work:

- Validate the new IDF build on a machine with `ESP-IDF` installed
- Continue BLE bring-up against the target mouse until `ESP_HIDH_OPEN_EVENT`
  succeeds reliably
- Bench-test PS/2 signaling against the Libretto dock connector

## Wiring

PS/2 uses open-collector style signaling. Do not drive the lines high. The
firmware only pulls each line low or releases it back to pull-up.

Default pin assignment:

- `GPIO2`: PS/2 clock
- `GPIO3`: PS/2 data
- `GPIO8`: status LED

Recommended electrical notes:

- Use pull-ups on PS/2 clock and data only in a way that is safe for the
  Libretto input and the ESP32-C3 GPIO
- Verify the ESP32-C3 is never exposed directly to `5V`
- Use an open-drain-safe interface strategy, such as transistor or MOSFET
  level shifting, before wiring into vintage hardware
- Share ground between the ESP32 board and the Libretto dock connection

## Build

This is now an `ESP-IDF` project.

Typical workflow:

```bash
. $HOME/esp/esp-idf/export.sh
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/cu.usbmodem113401 flash monitor
```

If your local serial port changes, list available ports first and use the new
device path.

Note: `idf.py` was not installed in the current shell during this conversion,
so the project structure was updated but not locally compiled in this session.

## Layout

- [CMakeLists.txt](/Volumes/External/GitHub/A2ThreeD/esp32-bt-ps2/CMakeLists.txt)
- [sdkconfig.defaults](/Volumes/External/GitHub/A2ThreeD/esp32-bt-ps2/sdkconfig.defaults)
- [main/main.cpp](/Volumes/External/GitHub/A2ThreeD/esp32-bt-ps2/main/main.cpp)
- [main/Config.h](/Volumes/External/GitHub/A2ThreeD/esp32-bt-ps2/main/Config.h)
- [main/MouseReport.h](/Volumes/External/GitHub/A2ThreeD/esp32-bt-ps2/main/MouseReport.h)
- [main/Ps2MouseDevice.h](/Volumes/External/GitHub/A2ThreeD/esp32-bt-ps2/main/Ps2MouseDevice.h)
- [main/Ps2MouseDevice.cpp](/Volumes/External/GitHub/A2ThreeD/esp32-bt-ps2/main/Ps2MouseDevice.cpp)
- [main/BleMouseClient.h](/Volumes/External/GitHub/A2ThreeD/esp32-bt-ps2/main/BleMouseClient.h)
- [main/BleMouseClient.cpp](/Volumes/External/GitHub/A2ThreeD/esp32-bt-ps2/main/BleMouseClient.cpp)

## Next Steps

1. Install or source a local `ESP-IDF` environment.
2. Build and flash the IDF project on the ESP32-C3.
3. Continue BLE HID host debugging with the Microsoft Bluetooth Ergonomic
   Mouse using the new native host stack.
