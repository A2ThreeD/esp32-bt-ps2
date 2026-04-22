#include "Ps2MouseDevice.h"

#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

Ps2MouseDevice::Ps2MouseDevice(gpio_num_t clockPin, gpio_num_t dataPin,
                               uint16_t halfClockMicros)
    : clockPin_(clockPin), dataPin_(dataPin), halfClockMicros_(halfClockMicros) {}

void Ps2MouseDevice::begin() {
  gpio_reset_pin(clockPin_);
  gpio_reset_pin(dataPin_);
  releaseLine(clockPin_);
  releaseLine(dataPin_);
}

void Ps2MouseDevice::releaseLine(gpio_num_t pin) {
  gpio_set_direction(pin, GPIO_MODE_INPUT);
  gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
}

void Ps2MouseDevice::pullLineLow(gpio_num_t pin) {
  gpio_set_direction(pin, GPIO_MODE_OUTPUT);
  gpio_set_level(pin, 0);
}

bool Ps2MouseDevice::busIdle() const {
  return gpio_get_level(clockPin_) == 1 && gpio_get_level(dataPin_) == 1;
}

void Ps2MouseDevice::writeBit(bool value) {
  if (value) {
    releaseLine(dataPin_);
  } else {
    pullLineLow(dataPin_);
  }

  esp_rom_delay_us(halfClockMicros_);
  pullLineLow(clockPin_);
  esp_rom_delay_us(halfClockMicros_);
  releaseLine(clockPin_);
  esp_rom_delay_us(halfClockMicros_);
}

bool Ps2MouseDevice::writeByte(uint8_t value) {
  TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(50);
  while (!busIdle()) {
    if (xTaskGetTickCount() > deadline) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  uint8_t parity = 1;

  writeBit(false);
  for (uint8_t bit = 0; bit < 8; ++bit) {
    const bool bitValue = (value >> bit) & 0x01;
    parity ^= bitValue;
    writeBit(bitValue);
  }

  writeBit((parity & 0x01) != 0);
  writeBit(true);
  releaseLine(dataPin_);
  esp_rom_delay_us(halfClockMicros_ * 2);
  return true;
}

int16_t Ps2MouseDevice::clampDelta(int16_t value, bool& overflow) const {
  overflow = value < -255 || value > 255;
  if (value < -255) {
    return -255;
  }
  if (value > 255) {
    return 255;
  }
  return value;
}

bool Ps2MouseDevice::sendReport(const MouseReport& report) {
  bool xOverflow = false;
  bool yOverflow = false;

  const int16_t clampedX = clampDelta(report.dx, xOverflow);
  const int16_t clampedY = clampDelta(report.dy, yOverflow);
  const uint8_t x = static_cast<uint8_t>(clampedX & 0xFF);
  const uint8_t y = static_cast<uint8_t>(clampedY & 0xFF);

  uint8_t status = 0x08;
  if (report.left) {
    status |= 0x01;
  }
  if (report.right) {
    status |= 0x02;
  }
  if (report.middle) {
    status |= 0x04;
  }
  if (clampedX < 0) {
    status |= 0x10;
  }
  if (clampedY < 0) {
    status |= 0x20;
  }
  if (xOverflow) {
    status |= 0x40;
  }
  if (yOverflow) {
    status |= 0x80;
  }

  return writeByte(status) && writeByte(x) && writeByte(y);
}
