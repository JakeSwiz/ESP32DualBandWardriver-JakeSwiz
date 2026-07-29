#include "SurveillanceDetect.h"
#include "GpsInterface.h"
#include "SDInterface.h"
#include "settings.h"

extern GpsInterface gps;
extern SDInterface  sd_obj;
extern Settings     settings;

// The only OUI the IEEE registry assigns to Flock Safety.
static const uint8_t OUI_FLOCK[3]        = {0xB4, 0x1E, 0x52};
// Registered to "ShotSpotter, Inc." (now SoundThinking).
static const uint8_t OUI_SHOTSPOTTER[3]  = {0xD4, 0x11, 0xD6};
// Registered to "Axon Enterprise, Inc.". Held since 2010 but listed as
// PRIVATE until 2025-01-30, so stale oui.txt copies show it unassigned.
static const uint8_t OUI_AXON[3]         = {0x00, 0x25, 0xDF};
// VIEVU, the body-camera maker Axon acquired in 2018.
static const uint8_t OUI_VIEVU[3]        = {0xFC, 0x01, 0x9E};

// BLE manufacturer company IDs.
//   0x034D "TASER International, Inc.", Axon's own SIG registration.
//   0x09C8 "XUNTONG", a battery module vendor, NOT Flock. Only
//          meaningful alongside a Flock-shaped device name.
#define MFG_ID_AXON   0x034D
#define MFG_ID_FLOCK  0x09C8

// Contract-manufacturer prefixes seen carrying Flock hardware. These
// belong to Liteon, Silicon Labs and USI rather than to Flock, so they
// can fire on unrelated gear, but every listed prefix is treated as a
// solid hit by choice: a missed camera costs more than a false alarm.
//
// Espressif, Samsung and Raspberry Pi prefixes from other published
// lists are still excluded. They would fire on this device itself, on
// every Galaxy handset and on every Pi in range.
struct ModuleOui {
  uint8_t oui[3];
  uint8_t weight;
};

// One table, checked on both radios. Do not split it by radio: the
// corpus records where a prefix happened to be seen, not where the
// silicon can be used, and real devices cross over. `weight` records
// corpus share for reference; scoring uses SURV_MODULE_BASE.
static const ModuleOui MODULE_OUIS[] = {
  {{0xD8,0xF3,0xBC}, 46}, {{0x74,0x4C,0xA1}, 46}, {{0x14,0x5A,0xFC}, 35},
  {{0x3C,0x91,0x80}, 34}, {{0xE4,0xAA,0xEA}, 34}, {{0x80,0x30,0x49}, 33},
  {{0x94,0x08,0x53}, 29}, {{0x08,0x3A,0x88}, 29}, {{0x70,0xC9,0x4E}, 28},
  {{0x9C,0x2F,0x9D}, 27}, {{0xF4,0x6A,0xDD}, 26}, {{0xE0,0x0A,0xF6}, 26},
  {{0xF8,0xA2,0xD6}, 26}, {{0x00,0xF4,0x8D}, 26}, {{0xD0,0x39,0x57}, 26},
  {{0xE8,0xD0,0xFC}, 25},
  {{0xEC,0x1B,0xBD}, 50}, {{0x58,0x8E,0x81}, 49}, {{0x90,0x35,0xEA}, 40},
  {{0xCC,0xCC,0xCC}, 34}, {{0xB4,0xE3,0xF9}, 33}, {{0x04,0x0D,0x84}, 32},
  {{0xF0,0x82,0xC0}, 28}, {{0x1C,0x34,0xF1}, 26}, {{0x94,0x34,0x69}, 26},
  {{0x38,0x5B,0x44}, 26},
  // From @NitekryDPaul's research, absent from the WiGLE corpus, so no
  // frequency to weight them by. Floor value.
  {{0xC0,0x35,0x32}, 25}, {{0x24,0xB2,0xB9}, 25}, {{0xE0,0x4F,0x43}, 25},
  {{0xB8,0x1E,0xA4}, 25}, {{0x70,0x08,0x94}, 25}, {{0x58,0x00,0xE3}, 25},
  {{0x5C,0x93,0xA2}, 25}, {{0x64,0x6E,0x69}, 25}
};
#define MODULE_OUI_COUNT (sizeof(MODULE_OUIS) / sizeof(MODULE_OUIS[0]))

// Returns the base score for a listed prefix, 0 when not listed. Every
// listed prefix rates as confirmed; the per-prefix weight is kept in the
// table for reference but no longer downgrades the hit.
static uint8_t moduleWeight(const uint8_t* mac) {
  for (size_t i = 0; i < MODULE_OUI_COUNT; i++)
    if (mac[0] == MODULE_OUIS[i].oui[0] && mac[1] == MODULE_OUIS[i].oui[1] &&
        mac[2] == MODULE_OUIS[i].oui[2])
      return SURV_MODULE_BASE;
  return 0;
}

// Signal strength shifts the rating: a camera you drive past is close.
static uint8_t rateHit(int base, int8_t rssi) {
  int s = base;
  if      (rssi >= -55) s += 12;
  else if (rssi >= -70) s += 6;
  else if (rssi <  -90) s -= 15;
  else if (rssi <  -80) s -= 8;

  if (s < 1)  s = 1;
  if (s > 99) s = 99;
  return (uint8_t)s;
}

static bool ouiIs(const uint8_t* mac, const uint8_t* oui) {
  return mac[0] == oui[0] && mac[1] == oui[1] && mac[2] == oui[2];
}

static bool allDigitsFrom(const String& s, int from) {
  if ((int)s.length() <= from) return false;
  for (int i = from; i < (int)s.length(); i++) {
    char c = s.charAt(i);
    if (c < '0' || c > '9') return false;
  }
  return true;
}

// Walk BLE AD structures [len][type][data...] for a manufacturer record
// with the given company ID. Parsing the structure rather than scanning
// for a byte pattern avoids matching those bytes inside another field.
static bool findMfgData(const uint8_t* p, size_t len, uint16_t id,
                        const uint8_t** data_out, size_t* data_len_out) {
  size_t i = 0;
  while (i + 1 < len) {
    uint8_t fl = p[i];
    if (fl == 0) break;
    if (i + fl >= len) break;            // truncated record

    if (p[i + 1] == 0xFF && fl >= 3) {
      uint16_t cid = (uint16_t)p[i + 2] | ((uint16_t)p[i + 3] << 8);
      if (cid == id) {
        if (data_out)     *data_out     = &p[i + 4];
        if (data_len_out) *data_len_out = (size_t)fl - 3;
        return true;
      }
    }
    i += (size_t)fl + 1;
  }
  return false;
}

static void toHex(const uint8_t* d, size_t len, char* out, size_t out_sz) {
  size_t n = 0;
  for (size_t i = 0; i < len && (n + 3) < out_sz; i++)
    n += snprintf(out + n, out_sz - n, "%02X", d[i]);
  out[n] = '\0';
}

// Great-circle distance in metres.
static float distanceM(float lat1, float lon1, float lat2, float lon2) {
  const float R = 6371000.0f;
  float dphi = (lat2 - lat1) * DEG_TO_RAD;
  float dlam = (lon2 - lon1) * DEG_TO_RAD;
  float a = sinf(dphi / 2.0f) * sinf(dphi / 2.0f) +
            cosf(lat1 * DEG_TO_RAD) * cosf(lat2 * DEG_TO_RAD) *
            sinf(dlam / 2.0f) * sinf(dlam / 2.0f);
  return R * 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
}

void SurveillanceDetect::begin() {
  this->hit_q = xQueueCreate(SURV_QUEUE_LEN, sizeof(SurvHit));
  if (!this->hit_q)
    Logger::log(WARN_MSG, "[SURV] Could not create detection queue");

  memset(this->seen, 0, sizeof(this->seen));
  this->reloadSettings();

  Logger::log(GUD_MSG, "[SURV] Surveillance detection ready");
}

void SurveillanceDetect::reloadSettings() {
  this->enabled          = settings.loadSetting<bool>(SURV_EN_NAME);
  this->weak_oui_enabled = settings.loadSetting<bool>(SURV_OUI_NAME);

  int floor_setting = settings.loadSetting<int>(SURV_RSSI_NAME);
  if (floor_setting >= -100 && floor_setting <= 0)
    this->rssi_floor = (int8_t)floor_setting;
}

const char* SurveillanceDetect::vendorName(SurvVendor v) {
  switch (v) {
    case SURV_FLOCK:       return "FLOCK SAFETY";
    case SURV_AXON:        return "AXON";
    case SURV_SHOTSPOTTER: return "SHOTSPOTTER";
    default:               return "UNKNOWN";
  }
}

const char* SurveillanceDetect::kindSuffix(SurvKind k) {
  switch (k) {
    case SK_BODY:    return " BODY";
    case SK_SURVCAM: return " SURVCAM";
    case SK_VEHICLE: return " FLEET";
    default:         return "";
  }
}

uint32_t SurveillanceDetect::getCount(SurvVendor v) {
  if (v >= SURV_VENDOR_COUNT) return 0;
  portENTER_CRITICAL(&this->mux);
  uint32_t c = this->counts[v];
  portEXIT_CRITICAL(&this->mux);
  return c;
}

// Own dedupe ring rather than the wardrive MAC history, which wraps
// constantly and would re-alert the same camera every few minutes.
// Position delta between sightings decides whether the device moved.
bool SurveillanceDetect::shouldReport(const uint8_t* mac, SurvKind& kind_io) {
  uint32_t now = millis();

  float cur_lat = 0.0f, cur_lon = 0.0f;
  bool  have_pos = gps.getFixStatus();
  if (have_pos) {
    cur_lat = gps.getLat().toFloat();
    cur_lon = gps.getLon().toFloat();
  }

  portENTER_CRITICAL(&this->mux);

  for (uint16_t i = 0; i < SURV_SEEN_SLOTS; i++) {
    if (!this->seen[i].used) continue;
    if (memcmp(this->seen[i].mac, mac, 6) != 0) continue;

    if (now - this->seen[i].last_ms < SURV_REALERT_MS) {
      portEXIT_CRITICAL(&this->mux);
      return false;
    }

    // Re-sighting: how far it moved says what kind of device it is.
    if (have_pos && this->seen[i].lat != 0.0f) {
      float moved = distanceM(this->seen[i].lat, this->seen[i].lon,
                              cur_lat, cur_lon);
      if (moved <= SURV_FIXED_RADIUS_M)
        this->seen[i].kind = SK_SURVCAM;
      else if (moved >= SURV_MOBILE_RADIUS_M)
        this->seen[i].kind = SK_BODY;
    }

    if (this->seen[i].kind != SK_UNKNOWN)
      kind_io = this->seen[i].kind;

    this->seen[i].last_ms = now;
    if (have_pos) {
      this->seen[i].lat = cur_lat;
      this->seen[i].lon = cur_lon;
    }
    portEXIT_CRITICAL(&this->mux);
    return true;
  }

  // First sighting, claim a slot.
  Seen& slot   = this->seen[this->seen_cursor];
  memcpy(slot.mac, mac, 6);
  slot.last_ms = now;
  slot.lat     = cur_lat;
  slot.lon     = cur_lon;
  slot.kind    = kind_io;
  slot.used    = true;
  this->seen_cursor = (this->seen_cursor + 1) % SURV_SEEN_SLOTS;

  portEXIT_CRITICAL(&this->mux);
  return true;
}

void SurveillanceDetect::enqueue(SurvHit& h) {
  portENTER_CRITICAL(&this->mux);
  this->counts[h.vendor]++;
  portEXIT_CRITICAL(&this->mux);

  if (this->hit_q)
    xQueueSend(this->hit_q, &h, 0);   // non-blocking; drop when full
}

void SurveillanceDetect::checkWiFi(const char* ssid, const uint8_t* bssid,
                                   int8_t rssi, uint8_t channel) {
  if (!this->enabled || !bssid) return;
  if (rssi < this->rssi_floor)   return;

  SurvHit h = {};
  h.vendor  = SURV_NONE;
  h.kind    = SK_UNKNOWN;
  int base  = 0;

  if (ouiIs(bssid, OUI_FLOCK)) {
    h.vendor = SURV_FLOCK;
    h.kind   = SK_SURVCAM;
    base     = 95;
    strlcpy(h.model, "Flock", sizeof(h.model));
  }
  else if (ouiIs(bssid, OUI_SHOTSPOTTER)) {
    h.vendor = SURV_SHOTSPOTTER;
    h.kind   = SK_SURVCAM;
    base     = 95;
    strlcpy(h.model, "Acoustic", sizeof(h.model));
  }
  // Axon beaconing on WiFi is unusual: body cameras are stations and
  // docks are wired, so this is likely a fixed or in-vehicle unit.
  else if (ouiIs(bssid, OUI_AXON) || ouiIs(bssid, OUI_VIEVU)) {
    h.vendor = SURV_AXON;
    base     = 95;
    strlcpy(h.model, ouiIs(bssid, OUI_VIEVU) ? "VIEVU" : "Axon",
            sizeof(h.model));
  }
  // "Flock-XXXXXX", where the hex digits are normally the last three
  // octets of the BSSID. Treated as a solid hit without cross-checking
  // them, so unprovisioned units on a default SSID still register.
  else if (ssid && strncasecmp(ssid, "flock-", 6) == 0) {
    h.vendor = SURV_FLOCK;
    h.kind   = SK_SURVCAM;
    base     = 95;
    strlcpy(h.model, "Camera", sizeof(h.model));
  }
  else if (this->weak_oui_enabled) {
    uint8_t w = moduleWeight(bssid);
    if (w) {
      h.vendor = SURV_FLOCK;
      base     = w;
      strlcpy(h.model, "Module", sizeof(h.model));
    }
  }

  if (h.vendor == SURV_NONE) return;
  if (!this->shouldReport(bssid, h.kind)) return;

  memcpy(h.mac, bssid, 6);
  h.rssi    = rssi;
  h.channel = channel;
  h.is_ble  = false;
  h.score   = rateHit(base, rssi);
  h.conf    = (h.score >= 80) ? SURV_CONFIRMED
            : (h.score >= 55) ? SURV_LIKELY : SURV_WEAK;
  strlcpy(h.ident, ssid ? ssid : "", sizeof(h.ident));

  this->enqueue(h);
}

// Flock stations sleep most of their duty cycle, so a transmitter-only
// sniff never sees them. Matching addr1 (the receiver) as well as addr2
// catches them while a nearby AP is addressing them. This is
// @NitekryDPaul's technique and the whole reason the mode exists.
void SurveillanceDetect::checkPromiscAddr(const uint8_t* mac, int8_t rssi,
                                          uint8_t channel, uint8_t slot) {
  if (!this->enabled || !mac) return;
  if (rssi < this->rssi_floor) return;

  if (mac[0] & 0x01) return;   // multicast / broadcast
  if (mac[0] & 0x02) return;   // locally administered => randomised

  SurvHit h = {};
  h.vendor  = SURV_NONE;
  h.kind    = SK_UNKNOWN;
  int base  = 0;

  if (ouiIs(mac, OUI_FLOCK)) {
    h.vendor = SURV_FLOCK;  h.kind = SK_SURVCAM;  base = 95;
    strlcpy(h.model, "Flock", sizeof(h.model));
  }
  else if (ouiIs(mac, OUI_AXON) || ouiIs(mac, OUI_VIEVU)) {
    h.vendor = SURV_AXON;  base = 95;
    strlcpy(h.model, ouiIs(mac, OUI_VIEVU) ? "VIEVU" : "Axon", sizeof(h.model));
  }
  else if (ouiIs(mac, OUI_SHOTSPOTTER)) {
    h.vendor = SURV_SHOTSPOTTER;  h.kind = SK_SURVCAM;  base = 95;
    strlcpy(h.model, "Acoustic", sizeof(h.model));
  }
  else {
    uint8_t w = moduleWeight(mac);
    if (!w) return;
    h.vendor = SURV_FLOCK;
    base = (int)w;
    strlcpy(h.model, slot == 1 ? "Sleeping" : "Module", sizeof(h.model));
  }

  if (h.vendor == SURV_NONE) return;
  if (!this->shouldReport(mac, h.kind)) return;

  memcpy(h.mac, mac, 6);
  h.rssi    = rssi;
  h.channel = channel;
  h.is_ble  = false;
  h.score   = rateHit(base, rssi);
  h.conf    = (h.score >= 80) ? SURV_CONFIRMED
            : (h.score >= 55) ? SURV_LIKELY : SURV_WEAK;
  snprintf(h.ident, sizeof(h.ident), "addr%u ch%u", slot, channel);

  this->enqueue(h);
}

// Runs on the NimBLE host task. Only path that sees current Flock
// hardware, and the only path that can ever see Axon: body cameras are
// WiFi stations and stations do not beacon.
void SurveillanceDetect::checkBLE(const NimBLEAdvertisedDevice* dev,
                                  const uint8_t* mac) {
  if (!this->enabled || !dev || !mac) return;

  int rssi = dev->getRSSI();
  if (rssi < this->rssi_floor) return;

  const std::vector<uint8_t>& pl = dev->getPayload();
  const uint8_t* p   = pl.data();
  size_t         len = pl.size();
  if (!p || len < 4) return;

  String name = dev->haveName() ? String(dev->getName().c_str()) : "";

  SurvHit h = {};
  h.vendor  = SURV_NONE;
  h.kind    = SK_UNKNOWN;
  int base  = 0;

  const uint8_t* mfg     = nullptr;
  size_t         mfg_len = 0;

  // Address type adjusts the rating, it does NOT gate the match. You
  // cannot tell a public address from a random one by its bytes:
  // EC:1B:BD is a registered Silicon Labs prefix whose first octet
  // masks to 0xC0 and trips the "random static" test. Gating on it
  // would discard 4,899 of the 4,907 battery units in the corpus.
  bool public_addr = (dev->getAddressType() == BLE_ADDR_PUBLIC);
  int  addr_penalty = public_addr ? 0 : 20;

  // Pull both records up front so the payload is logged even when the
  // address decides the classification.
  bool has_axon_mfg  = findMfgData(p, len, MFG_ID_AXON,  &mfg, &mfg_len);
  bool has_flock_mfg = false;
  if (!has_axon_mfg)
    has_flock_mfg = findMfgData(p, len, MFG_ID_FLOCK, &mfg, &mfg_len);

  // Registered vendor OUIs must be checked before company IDs. An OUI
  // belongs to whoever built the device; a company ID often identifies a
  // component supplier. A unit carrying ShotSpotter's D4:11:D6 is
  // ShotSpotter hardware even with a XUNTONG module inside it.
  if (ouiIs(mac, OUI_AXON) || ouiIs(mac, OUI_VIEVU)) {
    h.vendor = SURV_AXON;
    base     = 95 - addr_penalty;
    strlcpy(h.model, ouiIs(mac, OUI_VIEVU) ? "VIEVU" : "Axon",
            sizeof(h.model));
  }
  else if (ouiIs(mac, OUI_FLOCK)) {
    h.vendor = SURV_FLOCK;
    h.kind   = SK_SURVCAM;
    base     = 95 - addr_penalty;
    strlcpy(h.model, "Flock", sizeof(h.model));
  }
  else if (ouiIs(mac, OUI_SHOTSPOTTER)) {
    h.vendor = SURV_SHOTSPOTTER;
    h.kind   = SK_SURVCAM;
    base     = 95 - addr_penalty;
    strlcpy(h.model, "Acoustic", sizeof(h.model));
  }
  // 0x034D is Axon's own registration, so it confirms the vendor. It
  // does NOT identify the product: body-worn cameras and fixed ALPR
  // units share it. Kind stays UNKNOWN and is settled by movement.
  else if (has_axon_mfg) {
    h.vendor = SURV_AXON;
    base     = 90;
    strlcpy(h.model, "Axon", sizeof(h.model));
  }
  // 0x09C8 only means Flock alongside a Flock-shaped name. Accepts the
  // pre-2025 "Penguin-" prefix, the bare ten-digit name current firmware
  // uses, and the legacy battery pack.
  else if (has_flock_mfg) {
    bool shaped = (name.length() == 0)
               || (name == "FS Ext Battery")
               || (name.startsWith("Penguin-") && name.length() == 18 &&
                   allDigitsFrom(name, 8))
               || (name.length() == 10 && allDigitsFrom(name, 0));
    if (!shaped) return;

    h.vendor = SURV_FLOCK;
    h.kind   = SK_SURVCAM;
    base     = name.length() ? 85 : 65;
    strlcpy(h.model, name == "FS Ext Battery" ? "Ext Batt" : "Penguin",
            sizeof(h.model));
  }
  // Module prefixes: a hint, weighted by corpus share.
  else if (this->weak_oui_enabled) {
    uint8_t w = moduleWeight(mac);
    if (w) {
      h.vendor = SURV_FLOCK;
      base     = (int)w - addr_penalty;
      strlcpy(h.model, "Module", sizeof(h.model));
    }
  }

  if (h.vendor == SURV_NONE) return;
  if (!this->shouldReport(mac, h.kind)) return;

  memcpy(h.mac, mac, 6);
  h.rssi    = (int8_t)rssi;
  h.channel = 0;
  h.is_ble  = true;
  h.score   = rateHit(base, (int8_t)rssi);
  h.conf    = (h.score >= 80) ? SURV_CONFIRMED
            : (h.score >= 55) ? SURV_LIKELY : SURV_WEAK;
  strlcpy(h.ident, name.c_str(), sizeof(h.ident));
  if (mfg && mfg_len)
    toHex(mfg, mfg_len, h.mfg_hex, sizeof(h.mfg_hex));

  this->enqueue(h);
}

bool SurveillanceDetect::popHit(SurvHit& out) {
  if (!this->hit_q) return false;
  if (xQueueReceive(this->hit_q, &out, 0) != pdTRUE) return false;

  // Announce on serial unconditionally. The CSV needs an SD card and a
  // fix, but without this a card-less setup looks identical to one
  // detecting nothing.
  char mac_str[18];
  snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
           out.mac[0], out.mac[1], out.mac[2],
           out.mac[3], out.mac[4], out.mac[5]);

  Logger::log(GUD_MSG,
    "[SURV] " + String(vendorName(out.vendor)) +
    String(kindSuffix(out.kind)) +
    " " + String(out.model) +
    " " + String(mac_str) +
    " " + String(out.rssi) + "dBm" +
    " score:" + String(out.score) +
    (out.is_ble ? " BLE" : " ch" + String(out.channel)) +
    (strlen(out.ident) ? " \"" + String(out.ident) + "\"" : "") +
    (strlen(out.mfg_hex) ? " mfg:" + String(out.mfg_hex) : ""));

  this->logHit(out);
  return true;
}

// Own file rather than the wardrive log, which is a fixed-format
// WiGLE/WDG upload where these already appear as ordinary networks.
void SurveillanceDetect::logHit(const SurvHit& h) {
  if (!sd_obj.supported) return;
  if (!gps.getFixStatus()) return;   // no position, nothing worth recording

  bool need_header = !SD.exists(SURV_LOG_FILE);

  File f = SD.open(SURV_LOG_FILE, FILE_APPEND);
  if (!f) {
    Logger::log(WARN_MSG, "[SURV] Could not open " + String(SURV_LOG_FILE));
    return;
  }

  if (need_header)
    f.println("utc,vendor,kind,model,conf,score,mac,rssi,chan,band,lat,lon,alt,acc,ident,mfgdata");

  const char* kind_str = (h.kind == SK_BODY)    ? "BODY"    :
                         (h.kind == SK_SURVCAM) ? "SURVCAM" :
                         (h.kind == SK_VEHICLE) ? "FLEET"   : "UNKNOWN";
  const char* conf_str = (h.conf == SURV_CONFIRMED) ? "CONFIRMED" :
                         (h.conf == SURV_LIKELY)    ? "LIKELY"    : "WEAK";

  char mac_str[18];
  snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
           h.mac[0], h.mac[1], h.mac[2], h.mac[3], h.mac[4], h.mac[5]);

  String line = gps.getDatetime();
  line += ","; line += vendorName(h.vendor);
  line += ","; line += kind_str;
  line += ","; line += h.model;
  line += ","; line += conf_str;
  line += ","; line += String(h.score);
  line += ","; line += mac_str;
  line += ","; line += String(h.rssi);
  line += ","; line += String(h.channel);
  line += ","; line += (h.is_ble ? "BLE" : (h.channel > 14 ? "5" : "2.4"));
  line += ","; line += gps.getLat();
  line += ","; line += gps.getLon();
  line += ","; line += String(gps.getAlt());
  line += ","; line += String(gps.getAccuracy());
  line += ","; line += h.ident;
  line += ","; line += h.mfg_hex;

  f.println(line);
  f.close();
}
