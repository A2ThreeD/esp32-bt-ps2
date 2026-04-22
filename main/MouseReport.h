#pragma once

#include <stdint.h>

struct MouseReport {
  int16_t dx = 0;
  int16_t dy = 0;
  int8_t wheel = 0;
  bool left = false;
  bool right = false;
  bool middle = false;

  bool hasMovement() const { return dx != 0 || dy != 0 || wheel != 0; }
  bool hasButtons() const { return left || right || middle; }
  bool isEmpty() const { return !hasMovement() && !hasButtons(); }
};
