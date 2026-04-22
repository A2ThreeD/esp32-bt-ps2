#include "BleMouseClient.h"

#include <string.h>
#include <strings.h>

#include "Config.h"

#include "esp_event.h"
#include "esp_hid_common.h"
#include "esp_hidh.h"
#include "esp_hidh_nimble.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/ble_sm.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"

namespace {

constexpr uint16_t kHidServiceUuid = 0x1812;
constexpr uint16_t kMouseAppearance = ESP_HID_APPEARANCE_MOUSE;

const char* kTag = "BleMouseClient";

const char* addrTypeToString(uint8_t type) {
  switch (type) {
    case BLE_ADDR_PUBLIC:
      return "public";
    case BLE_ADDR_RANDOM:
      return "random";
#ifdef BLE_ADDR_PUBLIC_ID
    case BLE_ADDR_PUBLIC_ID:
      return "public_id";
#endif
#ifdef BLE_ADDR_RANDOM_ID
    case BLE_ADDR_RANDOM_ID:
      return "random_id";
#endif
    default:
      return "unknown";
  }
}

void formatAddress(const uint8_t* addr, char* out, size_t outLen) {
  snprintf(out, outLen, "%02x:%02x:%02x:%02x:%02x:%02x", addr[5], addr[4], addr[3],
           addr[2], addr[1], addr[0]);
}

bool nameMatchesPreferred(const char* name) {
  if (config::kPreferredMouseName[0] == '\0') {
    return true;
  }
  if (name == nullptr) {
    return false;
  }
  return strcasecmp(name, config::kPreferredMouseName) == 0;
}

}  // namespace

struct BleMouseClient::Impl {
  portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
  MouseReport pendingReport{};
  esp_hidh_dev_t* device = nullptr;
  uint8_t ownAddrType = BLE_ADDR_PUBLIC;
  uint8_t openingAddr[6] = {0};
  uint32_t nextScanAllowedMs = 0;
  bool stackReady = false;
  bool scanning = false;
  bool opening = false;
  bool connected = false;
  bool reportPending = false;

  static Impl& instance() {
    static Impl impl;
    return impl;
  }

  static void nimbleHostTask(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
  }

  static void onReset(int reason) {
    ESP_LOGW(kTag, "NimBLE reset, reason=%d", reason);
    Impl::instance().stackReady = false;
    Impl::instance().scanning = false;
    Impl::instance().opening = false;
    Impl::instance().connected = false;
  }

  static void onSync() {
    auto& self = Impl::instance();
    int rc = ble_hs_id_infer_auto(0, &self.ownAddrType);
    if (rc != 0) {
      ESP_LOGE(kTag, "ble_hs_id_infer_auto failed: rc=%d", rc);
      return;
    }

    self.stackReady = true;
    ESP_LOGI(kTag, "NimBLE synced, own address type=%s (%u)",
             addrTypeToString(self.ownAddrType), self.ownAddrType);
    self.startScan("sync");
  }

  static void hidEventHandler(void* arg, esp_event_base_t, int32_t eventId,
                              void* eventData) {
    static_cast<Impl*>(arg)->handleHidEvent(static_cast<esp_hidh_event_t>(eventId),
                                            static_cast<esp_hidh_event_data_t*>(eventData));
  }

  void begin() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_nimble_hci_init());
    ESP_ERROR_CHECK(nimble_port_init());

    ble_hs_cfg.reset_cb = Impl::onReset;
    ble_hs_cfg.sync_cb = Impl::onSync;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;

    esp_hidh_config_t hidhConfig{};
    hidhConfig.callback = &Impl::hidEventHandler;
    hidhConfig.event_stack_size = 6144;
    hidhConfig.callback_arg = this;
    ESP_ERROR_CHECK(esp_hidh_init(&hidhConfig));

    nextScanAllowedMs = config::kStartupDelayMs;
    ESP_LOGI(kTag, "BLE HID host initialized");
    nimble_port_freertos_init(Impl::nimbleHostTask);
  }

  void poll() {
    if (stackReady && !connected && !opening && !scanning &&
        xTaskGetTickCount() * portTICK_PERIOD_MS >= nextScanAllowedMs) {
      startScan("poll");
    }
  }

  bool fetchReport(MouseReport& report) {
    portENTER_CRITICAL(&lock);
    const bool available = reportPending;
    if (available) {
      report = pendingReport;
      pendingReport = MouseReport{};
      reportPending = false;
    }
    portEXIT_CRITICAL(&lock);
    return available;
  }

  void handleHidEvent(esp_hidh_event_t eventId, esp_hidh_event_data_t* event) {
    switch (eventId) {
      case ESP_HIDH_OPEN_EVENT: {
        if (event->open.status != ESP_OK) {
          ESP_LOGW(kTag, "HID open failed: status=%s", esp_err_to_name(event->open.status));
        }

        if (event->open.dev == nullptr) {
          ESP_LOGW(kTag, "HID open event without device");
          opening = false;
          nextScanAllowedMs = millis() + config::kBleScanRestartMs;
          break;
        }

        device = event->open.dev;
        opening = false;
        connected = true;

        char addr[18];
        const uint8_t* bda = esp_hidh_dev_bda_get(device);
        if (bda != nullptr) {
          formatAddress(bda, addr, sizeof(addr));
        } else {
          snprintf(addr, sizeof(addr), "unknown");
        }
        const char* usage = esp_hid_usage_str(esp_hidh_dev_usage_get(device));
        ESP_LOGI(kTag, "HID device opened: %s name=%s appearance=0x%04x usage=%s",
                 addr, esp_hidh_dev_name_get(device) ? esp_hidh_dev_name_get(device) : "",
                 esp_hidh_dev_appearance_get(device), usage ? usage : "unknown");
        esp_hidh_dev_dump(device, stdout);
        break;
      }

      case ESP_HIDH_INPUT_EVENT: {
        if (event->input.usage != ESP_HID_USAGE_MOUSE || event->input.length < 3) {
          break;
        }

        parseInputReport(event->input.report_id, event->input.data, event->input.length);
        break;
      }

      case ESP_HIDH_CLOSE_EVENT: {
        char addr[18] = "";
        if (event->close.dev != nullptr && esp_hidh_dev_bda_get(event->close.dev) != nullptr) {
          formatAddress(esp_hidh_dev_bda_get(event->close.dev), addr, sizeof(addr));
        }
        ESP_LOGW(kTag, "HID device closed: %s reason=%d", addr, event->close.reason);

        if (event->close.dev != nullptr) {
          esp_hidh_dev_free(event->close.dev);
        }

        device = nullptr;
        connected = false;
        opening = false;
        scanning = false;
        nextScanAllowedMs = millis() + config::kBleScanRestartMs;
        break;
      }

      case ESP_HIDH_BATTERY_EVENT:
        ESP_LOGI(kTag, "Mouse battery level: %u%%", event->battery.level);
        break;

      default:
        break;
    }
  }

  void parseInputReport(uint16_t reportId, const uint8_t* data, uint16_t length) {
    size_t offset = 0;
    if (reportId != 0 && length >= 4) {
      offset = 1;
    }

    if (length < offset + 3) {
      return;
    }

    MouseReport report;
    const uint8_t buttons = data[offset];
    report.left = (buttons & 0x01) != 0;
    report.right = (buttons & 0x02) != 0;
    report.middle = (buttons & 0x04) != 0;
    report.dx = static_cast<int8_t>(data[offset + 1]);
    report.dy = static_cast<int8_t>(data[offset + 2]);
    report.wheel = length >= offset + 4 ? static_cast<int8_t>(data[offset + 3]) : 0;

    portENTER_CRITICAL(&lock);
    pendingReport = report;
    reportPending = true;
    portEXIT_CRITICAL(&lock);

    if (config::kEnableVerboseLogging) {
      ESP_LOGI(kTag, "Mouse report id=%u dx=%d dy=%d wheel=%d buttons=%02x", reportId,
               report.dx, report.dy, report.wheel, buttons);
    }
  }

  void startScan(const char* reason) {
    if (!stackReady || scanning || opening || connected) {
      return;
    }

    ble_gap_disc_params params{};
    params.itvl = 0x50;
    params.window = 0x30;
    params.filter_policy = 0;
    params.limited = 0;
    params.passive = 0;
    params.filter_duplicates = 1;

    const int rc = ble_gap_disc(ownAddrType, BLE_HS_FOREVER, &params, &Impl::gapEvent, this);
    if (rc != 0) {
      ESP_LOGE(kTag, "ble_gap_disc failed: rc=%d", rc);
      nextScanAllowedMs = millis() + config::kBleScanRestartMs;
      return;
    }

    scanning = true;
    ESP_LOGI(kTag, "BLE scan start: %s", reason);
  }

  bool isCandidate(const ble_gap_disc_desc& desc, const ble_hs_adv_fields& fields) const {
    char name[32] = {0};
    if (fields.name != nullptr && fields.name_len > 0) {
      const size_t count = fields.name_len < sizeof(name) - 1 ? fields.name_len : sizeof(name) - 1;
      memcpy(name, fields.name, count);
      name[count] = '\0';
    }

    if (!nameMatchesPreferred(name[0] != '\0' ? name : nullptr)) {
      return false;
    }

    for (uint8_t i = 0; i < fields.num_uuids16; ++i) {
      if (fields.uuids16[i].value == kHidServiceUuid) {
        return true;
      }
    }

    return fields.appearance_is_present && fields.appearance == kMouseAppearance;
  }

  void logAdvertisement(const ble_gap_disc_desc& desc, const ble_hs_adv_fields& fields) const {
    if (!config::kLogAllBleAdvertisements) {
      return;
    }

    char addr[18];
    formatAddress(desc.addr.val, addr, sizeof(addr));

    char name[32] = {0};
    if (fields.name != nullptr && fields.name_len > 0) {
      const size_t count = fields.name_len < sizeof(name) - 1 ? fields.name_len : sizeof(name) - 1;
      memcpy(name, fields.name, count);
      name[count] = '\0';
    }

    bool hasHid = false;
    for (uint8_t i = 0; i < fields.num_uuids16; ++i) {
      if (fields.uuids16[i].value == kHidServiceUuid) {
        hasHid = true;
        break;
      }
    }

    ESP_LOGI(kTag,
             "BLE adv: %s name=%s appearance=0x%04x rssi=%d type=%s hid=%s candidate=%s",
             addr, name, fields.appearance_is_present ? fields.appearance : 0, desc.rssi,
             addrTypeToString(desc.addr.type), hasHid ? "yes" : "no",
             isCandidate(desc, fields) ? "yes" : "no");
  }

  void openCandidate(const ble_gap_disc_desc& desc, const ble_hs_adv_fields& fields) {
    char addr[18];
    formatAddress(desc.addr.val, addr, sizeof(addr));

    char name[32] = {0};
    if (fields.name != nullptr && fields.name_len > 0) {
      const size_t count = fields.name_len < sizeof(name) - 1 ? fields.name_len : sizeof(name) - 1;
      memcpy(name, fields.name, count);
      name[count] = '\0';
    }

    ESP_LOGI(kTag, "BLE candidate: %s name=%s appearance=0x%04x rssi=%d -> opening HID host",
             addr, name, fields.appearance_is_present ? fields.appearance : 0, desc.rssi);

    memcpy(openingAddr, desc.addr.val, sizeof(openingAddr));
    ble_gap_disc_cancel();
    scanning = false;
    opening = true;

    if (esp_hidh_dev_open(openingAddr, ESP_HID_TRANSPORT_BLE, desc.addr.type) == nullptr) {
      ESP_LOGE(kTag, "esp_hidh_dev_open failed immediately for %s", addr);
      opening = false;
      nextScanAllowedMs = millis() + config::kBleScanRestartMs;
    }
  }

  static int gapEvent(struct ble_gap_event* event, void* arg) {
    auto* self = static_cast<Impl*>(arg);

    switch (event->type) {
      case BLE_GAP_EVENT_DISC: {
        ble_hs_adv_fields fields;
        memset(&fields, 0, sizeof(fields));
        ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);

        self->logAdvertisement(event->disc, fields);
        if (!self->opening && !self->connected && self->isCandidate(event->disc, fields)) {
          self->openCandidate(event->disc, fields);
        }
        return 0;
      }

      case BLE_GAP_EVENT_DISC_COMPLETE:
        self->scanning = false;
        self->nextScanAllowedMs = millis() + config::kBleScanRestartMs;
        ESP_LOGI(kTag, "BLE scan complete, reason=%d", event->disc_complete.reason);
        return 0;

      default:
        return 0;
    }
  }

  static uint32_t millis() {
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
  }
};

BleMouseClient::Impl& BleMouseClient::impl() { return Impl::instance(); }

void BleMouseClient::begin() { impl().begin(); }

void BleMouseClient::poll() { impl().poll(); }

bool BleMouseClient::isConnected() const { return impl().connected; }

bool BleMouseClient::fetchReport(MouseReport& report) { return impl().fetchReport(report); }
