#pragma once

#include "driver/gpio.h"

namespace config {

constexpr gpio_num_t kPs2ClockPin = GPIO_NUM_2;
constexpr gpio_num_t kPs2DataPin = GPIO_NUM_3;
constexpr gpio_num_t kStatusLedPin = GPIO_NUM_8;

constexpr bool kInvertY = true;
constexpr bool kEnableVerboseLogging = true;
constexpr bool kEnableTestPattern = false;
constexpr bool kLogAllBleAdvertisements = true;

constexpr char kPreferredMouseName[] = "";
constexpr uint16_t kPs2HalfClockMicros = 40;
constexpr uint32_t kStartupDelayMs = 750;
constexpr uint32_t kIdleBlinkMs = 500;
constexpr uint32_t kMainLoopDelayMs = 2;
constexpr uint32_t kDebugHeartbeatMs = 2000;
constexpr uint32_t kBleScanRestartMs = 3000;

}
