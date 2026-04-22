#pragma once

#include <stdint.h>

#include "driver/gpio.h"

#include "MouseReport.h"

class Ps2MouseDevice {
 public:
  Ps2MouseDevice(gpio_num_t clockPin, gpio_num_t dataPin, uint16_t halfClockMicros);

  void begin();
  bool sendReport(const MouseReport& report);

 private:
  gpio_num_t clockPin_;
  gpio_num_t dataPin_;
  uint16_t halfClockMicros_;

  void releaseLine(gpio_num_t pin);
  void pullLineLow(gpio_num_t pin);
  bool busIdle() const;
  void writeBit(bool value);
  bool writeByte(uint8_t value);
  int16_t clampDelta(int16_t value, bool& overflow) const;
};
