#include "BleMouseClient.h"
#include "Config.h"
#include "Ps2MouseDevice.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

const char* kTag = "main";

BleMouseClient gBleMouse;
Ps2MouseDevice gPs2Mouse(config::kPs2ClockPin, config::kPs2DataPin,
                         config::kPs2HalfClockMicros);

MouseReport normalizeReport(const MouseReport& incoming) {
  MouseReport normalized = incoming;
  if (config::kInvertY) {
    normalized.dy = -normalized.dy;
  }
  return normalized;
}

void setupStatusLed() {
  gpio_reset_pin(config::kStatusLedPin);
  gpio_set_direction(config::kStatusLedPin, GPIO_MODE_OUTPUT);
  gpio_set_level(config::kStatusLedPin, 0);
}

MouseReport maybeGenerateTestPattern() {
  MouseReport report;
  if (!config::kEnableTestPattern) {
    return report;
  }

  static uint32_t phase = 0;
  static TickType_t lastTick = 0;
  if (xTaskGetTickCount() - lastTick < pdMS_TO_TICKS(1200)) {
    return report;
  }

  lastTick = xTaskGetTickCount();
  switch ((phase++) % 4) {
    case 0:
      report.dx = 20;
      break;
    case 1:
      report.dy = 20;
      break;
    case 2:
      report.dx = -20;
      break;
    default:
      report.dy = -20;
      break;
  }
  return report;
}

void logReport(const MouseReport& report, bool transmitted) {
  ESP_LOGI(kTag, "report dx=%d dy=%d wheel=%d L=%d R=%d M=%d tx=%s", report.dx,
           report.dy, report.wheel, report.left, report.right, report.middle,
           transmitted ? "ok" : "busy");
}

void logIdleHeartbeat(bool connected) {
  static TickType_t lastHeartbeat = 0;
  if (xTaskGetTickCount() - lastHeartbeat < pdMS_TO_TICKS(config::kDebugHeartbeatMs)) {
    return;
  }

  lastHeartbeat = xTaskGetTickCount();
  ESP_LOGI(kTag, connected ? "Idle: connected, waiting for mouse movement"
                           : "Idle: waiting for BLE HID mouse");
}

}  // namespace

extern "C" void app_main(void) {
  setupStatusLed();

  ESP_LOGI(kTag, "esp32-bt-ps2 starting");
  ESP_LOGI(kTag, "Target: BLE HID mouse -> PS/2");
  ESP_LOGI(kTag, "PS/2 clock pin: GPIO%d", static_cast<int>(config::kPs2ClockPin));
  ESP_LOGI(kTag, "PS/2 data pin: GPIO%d", static_cast<int>(config::kPs2DataPin));
  ESP_LOGI(kTag, "Status LED pin: GPIO%d", static_cast<int>(config::kStatusLedPin));

  vTaskDelay(pdMS_TO_TICKS(config::kStartupDelayMs));

  gPs2Mouse.begin();
  gBleMouse.begin();

  ESP_LOGI(kTag, "PS/2 transmitter ready");
  ESP_LOGI(kTag, "BLE client initialized");

  while (true) {
    gBleMouse.poll();

    MouseReport report;
    bool haveReport = gBleMouse.fetchReport(report);
    if (!haveReport) {
      report = maybeGenerateTestPattern();
      haveReport = !report.isEmpty();
    }

    if (haveReport) {
      const MouseReport normalized = normalizeReport(report);
      const bool transmitted = gPs2Mouse.sendReport(normalized);
      gpio_set_level(config::kStatusLedPin, transmitted ? 1 : 0);
      logReport(normalized, transmitted);
    } else {
      const bool ledState =
          ((xTaskGetTickCount() * portTICK_PERIOD_MS) / config::kIdleBlinkMs) % 2 == 0 &&
          gBleMouse.isConnected();
      gpio_set_level(config::kStatusLedPin, ledState ? 1 : 0);
      logIdleHeartbeat(gBleMouse.isConnected());
    }

    vTaskDelay(pdMS_TO_TICKS(config::kMainLoopDelayMs));
  }
}
