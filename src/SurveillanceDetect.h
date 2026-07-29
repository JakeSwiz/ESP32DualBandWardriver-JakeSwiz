#pragma once

#ifndef SurveillanceDetect_h
#define SurveillanceDetect_h

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "configs.h"
#include "logger.h"

enum SurvVendor : uint8_t {
  SURV_NONE = 0,
  SURV_FLOCK,
  SURV_AXON,
  SURV_SHOTSPOTTER,
  SURV_AXIS,
  SURV_VENDOR_COUNT
};

enum SurvKind : uint8_t {
  SK_UNKNOWN = 0,
  SK_BODY,
  SK_SURVCAM,
  SK_VEHICLE
};

enum SurvConf : uint8_t {
  SURV_WEAK = 0,
  SURV_LIKELY,
  SURV_CONFIRMED
};

struct SurvHit {
  uint8_t    mac[6];
  int8_t     rssi;
  uint8_t    channel;      // 0 for BLE
  SurvVendor vendor;
  SurvKind   kind;
  SurvConf   conf;
  uint8_t    score;        // 1-100, signature strength adjusted for RSSI
  bool       is_ble;
  char       model[16];
  char       ident[24];    // matched SSID or BLE name
  char       mfg_hex[36];  // raw manufacturer payload
  char       oui[9];       // matched vendor prefix, empty when not OUI-derived
  bool       is_new;       // false when already counted and logged
};

class SurveillanceDetect {
  public:
    void begin();
    void reloadSettings();

    // Safe from any task: classify, dedupe, count, enqueue. Never draws.
    void checkWiFi(const char* ssid, const uint8_t* bssid,
                   int8_t rssi, uint8_t channel);
    void checkBLE(const NimBLEAdvertisedDevice* dev, const uint8_t* mac);

    void checkPromiscAddr(const uint8_t* mac, int8_t rssi,
                          uint8_t channel, uint8_t slot);

    // Main loop only. Also writes the SD log.
    bool popHit(SurvHit& out);

    uint32_t getCount(SurvVendor v);
    uint32_t getFlockCount() { return getCount(SURV_FLOCK); }
    uint32_t getAxonCount()  { return getCount(SURV_AXON); }
    uint32_t getAxisCount()  { return getCount(SURV_AXIS); }

    bool isEnabled() { return this->enabled; }

    static const char* vendorName(SurvVendor v);
    static const char* kindSuffix(SurvKind k);

  private:
    QueueHandle_t hit_q = nullptr;
    portMUX_TYPE  mux   = portMUX_INITIALIZER_UNLOCKED;

    uint32_t counts[SURV_VENDOR_COUNT] = {0};

    bool   enabled          = true;
    bool   weak_oui_enabled = false;
    int8_t rssi_floor       = -90;

    struct Seen {
      uint8_t  mac[6];
      uint32_t last_ms;      // last counted and logged
      uint32_t last_notify;  // last alerted
      float    lat;
      float    lon;
      SurvKind kind;
      bool     used;
    };
    Seen     seen[SURV_SEEN_SLOTS];
    uint16_t seen_cursor = 0;

    bool     shouldReport(const uint8_t* mac, SurvKind& kind_io, bool& is_new);
    void     enqueue(SurvHit& h);
    void     logHit(const SurvHit& h);
};

#endif
