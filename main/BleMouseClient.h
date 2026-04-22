#pragma once

#include <stdint.h>

#include "MouseReport.h"

class BleMouseClient {
 public:
  void begin();
  void poll();
  bool isConnected() const;
  bool fetchReport(MouseReport& report);

 private:
  struct Impl;
  static Impl& impl();
};
