#include "ui.h"

void UI::begin() {
  sd_file_menu.list = new LinkedList<MenuNode>();
  action_menu.list  = new LinkedList<MenuNode>();
  mode_menu.list    = new LinkedList<MenuNode>();
  upload_menu.list  = new LinkedList<MenuNode>();
  delete_all_menu.list = new LinkedList<MenuNode>();
  upload_all_menu.list = new LinkedList<MenuNode>();
  mark_geofence_menu.list = new LinkedList<MenuNode>();

  mode_menu.name   = "Mode";
  action_menu.name = "Action";
  upload_menu.name = "Upload";
  delete_all_menu.name = "Delete All?";
  upload_all_menu.name = "Upload All?";
  mark_geofence_menu.name = "Mark Geofence Center?";

  this->buildSDFileMenu();

  action_menu.parentMenu = &sd_file_menu;
  mode_menu.parentMenu   = &sd_file_menu;
  upload_menu.parentMenu = &action_menu;  // Upload is a submenu of Action
  delete_all_menu.parentMenu = &sd_file_menu;
  upload_all_menu.parentMenu = &sd_file_menu;
  mark_geofence_menu.parentMenu = &sd_file_menu;

  // Geofence menu
  this->addNodes(&mark_geofence_menu, "No", ST77XX_WHITE, NULL, 0, [this]() {
    this->current_menu = mark_geofence_menu.parentMenu;
  });
  this->addNodes(&mark_geofence_menu, "Yes", ST77XX_WHITE, NULL, 0, [this]() {
    display.clearScreen();

    // Check to make sure we have GPS
    if (gps.getFixStatus() && gps.getGpsModuleStatus()) {
      bool save_available = false;

      // Look for next available geofence
      for (int i = 0; i < MAX_GEOFENCES; i++) {
        String geoStr = settings.loadSetting<String>("geo_" + String(i));

        // Parse stored JSON geo string
        DynamicJsonDocument geoDoc(256);
        if (!geoStr.isEmpty() && deserializeJson(geoDoc, geoStr) == DeserializationError::Ok) {
          float gLat = geoDoc["lat"];
          float gLon = geoDoc["lon"];
          int gRad = geoDoc["rad"];
          String old_label = geoDoc["label"];
          if (gLat != 0.0 && gLon != 0.0 && old_label != "") {
            Logger::log(STD_MSG, "geo_" + String(i) + " lat: " + String(gLat) + ", label: " + old_label + " exists. Skipping...");
            continue;
          }
          else {
            save_available = true;
            Logger::log(STD_MSG, "geo_" + String(i) + " available. Creating...");
            int rad = (int)(0.10 * 1609.34);     // convert to meters for storage
            String label = "Live Geofence " + String(i);

            DynamicJsonDocument geoDoc(256);
            geoDoc["lat"]   = gps.getLat().toFloat();
            geoDoc["lon"]   = gps.getLon().toFloat();
            geoDoc["rad"]   = rad;
            geoDoc["label"] = label;
            String geoStr;
            serializeJson(geoDoc, geoStr);

            settings.saveSetting<bool>("geo_" + String(i), geoStr);

            wifi_ops.reloadGeofenceCache();

            Logger::log(GUD_MSG, "Saved geofence center \"" + label + "\" as geo_" + String(i));

            display.drawCenteredText("New Geofence Saved", true);

            break;

          }
        }
      }
      if (!save_available) {
        display.drawCenteredText("No available save slots", true);
      }
    }
    else {
      display.drawCenteredText("Need GPS Fix", true);
    }

    delay(2000);

    this->current_menu = &sd_file_menu;
  });

  // Delete all Menu
  this->addNodes(&delete_all_menu, "No", ST77XX_WHITE, NULL, 0, [this]() {
    this->current_menu = delete_all_menu.parentMenu;
  });
  this->addNodes(&delete_all_menu, "Yes", ST77XX_WHITE, NULL, 0, [this]() {
    display.clearScreen();

    display.drawCenteredText("Deleting Logs...");

    buffer.setFileName("");

    for (int i = 0; i < sd_obj.sd_files->size(); i++) {
      if (sd_obj.sd_files->get(i).startsWith("wardrive_") || sd_obj.sd_files->get(i).startsWith("wigle-")) {
        if (sd_obj.removeFile("/" + sd_obj.sd_files->get(i))) {
          Logger::log(STD_MSG, "Removed file: " + sd_obj.sd_files->get(i));
          sd_obj.removeFile("/" + sd_obj.sd_files->get(i) + ".wdg");
          sd_obj.removeFile("/" + sd_obj.sd_files->get(i) + ".wigle");
        }
        else {
          Logger::log(WARN_MSG, "Could not remove file: " + sd_obj.sd_files->get(i));
        }
      }
    }
    display.clearScreen();

    display.drawCenteredText("Logs removed");

    delay(2000);

    this->buildSDFileMenu();

    this->current_menu = &sd_file_menu;
  });

  // Upload all Menu
  this->addNodes(&upload_all_menu, "Back", ST77XX_WHITE, NULL, 0, [this]() {
    this->current_menu = upload_all_menu.parentMenu;
  });
  this->addNodes(&upload_all_menu, "WiGLE", ST77XX_WHITE, NULL, 0, [this]() {
    if (wifi_ops.tryConnectToWiFi()) {
      delay(1000);
      for (int i = 0; i < sd_obj.sd_files->size(); i++) {
        if (sd_obj.sd_files->get(i).startsWith("wardrive_") || sd_obj.sd_files->get(i).startsWith("wigle-")) {
          Logger::log(STD_MSG, "Uploading " + sd_obj.sd_files->get(i) + "...");
          if (wifi_ops.uploadFile("/" + sd_obj.sd_files->get(i), true, WIGLE_UPLOAD)) {
            display.clearScreen();
            display.drawCenteredText("WiGLE OK", true);
          } else {
            display.clearScreen();
            display.drawCenteredText("WiGLE failed", true);
          }
        }
      }
    }
    wifi_ops.deinitWiFi();
    delay(10);
    wifi_ops.initWiFi();
    delay(2000);
    this->current_menu = upload_all_menu.parentMenu;
  });
  this->addNodes(&upload_all_menu, "WDGWars", ST77XX_WHITE, NULL, 0, [this]() {
    if (wifi_ops.tryConnectToWiFi()) {
      delay(1000);
      for (int i = 0; i < sd_obj.sd_files->size(); i++) {
        if (sd_obj.sd_files->get(i).startsWith("wardrive_") || sd_obj.sd_files->get(i).startsWith("wigle-")) {
          Logger::log(STD_MSG, "Uploading " + sd_obj.sd_files->get(i) + "...");
          if (wifi_ops.uploadFile("/" + sd_obj.sd_files->get(i), true, WDG_UPLOAD)) {
            display.clearScreen();
            display.drawCenteredText("WDG OK", true);
          } else {
            display.clearScreen();
            display.drawCenteredText("WDG failed", true);
          }
        }
      }
    }
    wifi_ops.deinitWiFi();
    delay(10);
    wifi_ops.initWiFi();
    delay(2000);
    this->current_menu = upload_all_menu.parentMenu;
  });
  this->addNodes(&upload_all_menu, "Both", ST77XX_WHITE, NULL, 0, [this]() {
    if (wifi_ops.tryConnectToWiFi()) {
      delay(1000);
      for (int i = 0; i < sd_obj.sd_files->size(); i++) {
        if (sd_obj.sd_files->get(i).startsWith("wardrive_") || sd_obj.sd_files->get(i).startsWith("wigle-")) {
          Logger::log(STD_MSG, "Uploading " + sd_obj.sd_files->get(i) + "...");
          if (wifi_ops.uploadFile("/" + sd_obj.sd_files->get(i), true, BOTH_UPLOAD)) {
            display.clearScreen();
            display.drawCenteredText("Upload OK", true);
          } else {
            display.clearScreen();
            display.drawCenteredText("Upload failed", true);
          }
        }
      }
    }
    wifi_ops.deinitWiFi();
    delay(10);
    wifi_ops.initWiFi();
    delay(2000);
    this->current_menu = upload_all_menu.parentMenu;
  });

  this->addNodes(&action_menu, "Back", ST77XX_WHITE, NULL, 0, [this]() {
    this->current_menu = action_menu.parentMenu;
  });

  // Upload opens submenu
  this->addNodes(&action_menu, "Upload", ST77XX_WHITE, NULL, 0, [this]() {
    this->current_menu = &upload_menu;
  });

  this->addNodes(&action_menu, "Delete", ST77XX_WHITE, NULL, 0, [this]() {
    if ("/" + sd_obj.selected_file_name == buffer.getFileName())
      buffer.setFileName("");

    if (sd_obj.removeFile("/" + sd_obj.selected_file_name)) {
      Logger::log(STD_MSG, "Removed file: " + sd_obj.selected_file_name);
      display.clearScreen();
      display.drawCenteredText("File removed", true);
    } else {
      Logger::log(STD_MSG, "Could not remove file");
      display.clearScreen();
      display.drawCenteredText("Could not remove file", true);
    }
    delay(2000);
    this->buildSDFileMenu();
    this->current_menu = &sd_file_menu;
  });

  // Upload Menu
  this->addNodes(&upload_menu, "Back", ST77XX_WHITE, NULL, 0, [this]() {
    this->current_menu = upload_menu.parentMenu;
  });
  this->addNodes(&upload_menu, "WiGLE", ST77XX_WHITE, NULL, 0, [this]() {
    if (wifi_ops.tryConnectToWiFi()) {
      delay(1000);
      if (wifi_ops.uploadFile("/" + sd_obj.selected_file_name, true, WIGLE_UPLOAD)) {
        display.clearScreen();
        display.drawCenteredText("WiGLE OK", true);
      } else {
        display.clearScreen();
        display.drawCenteredText("WiGLE failed", true);
      }
    }
    wifi_ops.deinitWiFi();
    delay(10);
    wifi_ops.initWiFi();
    delay(2000);
    this->current_menu = upload_menu.parentMenu;
  });
  this->addNodes(&upload_menu, "WDGWars", ST77XX_WHITE, NULL, 0, [this]() {
    if (wifi_ops.tryConnectToWiFi()) {
      delay(1000);
      if (wifi_ops.uploadFile("/" + sd_obj.selected_file_name, true, WDG_UPLOAD)) {
        display.clearScreen();
        display.drawCenteredText("WDG OK", true);
      } else {
        display.clearScreen();
        display.drawCenteredText("WDG failed", true);
      }
    }
    wifi_ops.deinitWiFi();
    delay(10);
    wifi_ops.initWiFi();
    delay(2000);
    this->current_menu = upload_menu.parentMenu;
  });
  this->addNodes(&upload_menu, "Both", ST77XX_WHITE, NULL, 0, [this]() {
    if (wifi_ops.tryConnectToWiFi()) {
      delay(1000);
      if (wifi_ops.uploadFile("/" + sd_obj.selected_file_name, true, BOTH_UPLOAD)) {
        display.clearScreen();
        display.drawCenteredText("Upload OK", true);
      } else {
        display.clearScreen();
        display.drawCenteredText("Upload failed", true);
      }
    }
    wifi_ops.deinitWiFi();
    delay(10);
    wifi_ops.initWiFi();
    delay(2000);
    this->current_menu = upload_menu.parentMenu;
  });

  // Mode Menu
  this->addNodes(&mode_menu, "Back", ST77XX_WHITE, NULL, 0, [this]() {
    this->current_menu = mode_menu.parentMenu;
  });
  this->addNodes(&mode_menu, "Solo", ST77XX_WHITE, NULL, 0, [this]() {
    wifi_ops.run_mode = SOLO_MODE;
    // begin() reloads run_mode from setting "m", so write it back.
    settings.saveSetting<bool>("m", SOLO_MODE);
    this->current_menu = mode_menu.parentMenu;
    display.clearScreen();
    display.drawCenteredText("Mode set", true);
    delay(2000);
  });
  this->addNodes(&mode_menu, "Core", ST77XX_WHITE, NULL, 0, [this]() {
    wifi_ops.run_mode = CORE_MODE;
    // begin() reloads run_mode from setting "m", so write it back.
    settings.saveSetting<bool>("m", CORE_MODE);
    this->current_menu = mode_menu.parentMenu;
    display.clearScreen();
    display.drawCenteredText("Mode set", true);
    wifi_ops.startESPNow();
    delay(2000);
  });
  this->addNodes(&mode_menu, "Node", ST77XX_WHITE, NULL, 0, [this]() {
    wifi_ops.run_mode = NODE_MODE;
    // begin() reloads run_mode from setting "m", so write it back.
    settings.saveSetting<bool>("m", NODE_MODE);
    this->current_menu = mode_menu.parentMenu;
    display.clearScreen();
    display.drawCenteredText("Mode set", true);
    wifi_ops.startESPNow();
    delay(2000);
  });
  this->addNodes(&mode_menu, "Flock Hunt", UI_RED, NULL, 0, [this]() {
    wifi_ops.run_mode = FLOCK_MODE;
    // begin() reloads run_mode from setting "m", so write it back.
    settings.saveSetting<bool>("m", FLOCK_MODE);
    this->current_menu = mode_menu.parentMenu;
    display.clearScreen();
    display.drawCenteredText("Promiscuous hunt", true);
    delay(2000);
  });


  this->current_menu = &sd_file_menu;
  this->init_time    = millis();
}

void UI::printFirmwareVersion() {
  display.tft->setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  display.tft->setCursor(0, 0);
  display.tft->print(FIRMWARE_VERSION);
}

void UI::printBatteryLevel(int8_t batteryLevel) {
  display.tft->setRotation(3);
  display.tft->setTextSize(1);
  display.tft->setTextColor(ST77XX_WHITE, ST77XX_BLACK);

  // Fixed width: start x derives from string length, so a value
  // shrinking from 100% to 99% would leave its old leading digit.
  char buf[12];
  if (batteryLevel < 0)
    snprintf(buf, sizeof(buf), "Bat: --%%");
  else
    snprintf(buf, sizeof(buf), "Bat:%3d%%", batteryLevel);

  uint8_t  charWidth = 6;
  uint16_t textWidth = (strlen(buf) + 5) * charWidth;
  uint16_t x         = TFT_WIDTH - textWidth - 2;

  display.tft->setCursor(x, 0);
  if (sd_obj.supported)
    display.tft->setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  else
    display.tft->setTextColor(UI_RED, ST77XX_BLACK);
  display.tft->print("SD");
  display.tft->setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  if (battery.i2c_supported) {
    display.tft->print(" | ");
    display.tft->print(buf);
  }
}

// ============================================================
// setDisplayMode — clean mode transition helper
// Resets incognito state, restores backlight, forces redraw
// ============================================================
void UI::setDisplayMode(uint8_t new_mode) {
  if (this->incognito_counting) {
    this->incognito_counting = false;
    display.ctrlBacklight(true);
  }
  this->stat_display_mode      = new_mode;
  this->last_stat_display_mode = 255;
  this->last_mode_change_ms    = millis();
  this->lastUpdateTime         = 0;
  this->full_repaint           = true;
  if (new_mode != SD_FILES && new_mode != INCOGNITO)
    display.tft->fillScreen(ST77XX_BLACK);
}

// Writes a value with an opaque background padded to a fixed cell
// width, so a shorter value erases the longer one it replaces.
static void drawField(int16_t x, int16_t y, uint8_t size, uint16_t colour,
                      const String& value, uint8_t cells) {
  String out = value;
  while (out.length() < cells) out += " ";
  if (out.length() > cells) out = out.substring(0, cells);

  display.tft->setTextSize(size);
  display.tft->setTextColor(colour, ST77XX_BLACK);
  display.tft->setCursor(x, y);
  display.tft->print(out);
}

// Main loop only. Detection callbacks enqueue, this drains the queue
// and arms the banner. A hit arriving while a banner is up stays
// queued rather than stomping the one on screen.
void UI::serviceAlerts(uint32_t currentTime) {
  if (this->banner_active &&
      (currentTime - this->banner_start >= SURV_BANNER_MS)) {
    this->banner_active = false;
    this->banner_phase  = -1;
    // Clear only what the banner covered; the next pass repaints it.
    display.tft->fillRect(0, 18, TFT_WIDTH, 28, ST77XX_BLACK);
  }

  if (this->banner_active) return;

  SurvHit hit;
  if (!surveillance.popHit(hit)) return;   // also writes the SD log

  // Only the primary stats screen reserves a region for the banner.
  // Elsewhere the hit is still counted and logged, just not drawn.
  if (this->stat_display_mode != STATS_NEW || wifi_ops.isDocked()) return;

  if (!settings.loadSetting<bool>(SURV_BNR_NAME)) return;

  // A weak signature at range still counts and logs, but should not
  // interrupt the screen.
  if (hit.score < SURV_BANNER_MIN_SCORE) return;

  this->banner_hit    = hit;
  this->banner_start  = currentTime;
  this->banner_phase  = -1;
  this->banner_active = true;
}

void UI::drawBanner(uint32_t currentTime) {
  uint32_t elapsed = currentTime - this->banner_start;

  // Blink briefly to catch the eye, then hold solid to stay readable.
  int8_t phase = 1;
  if (elapsed < SURV_BANNER_BLINK_FOR)
    phase = ((elapsed / SURV_BANNER_BLINK_MS) % 2) ? 0 : 1;

  if (phase == this->banner_phase) return;
  this->banner_phase = phase;

  // Colour encodes certainty first. A contract-manufacturer prefix is
  // Flock-associated silicon, not a Flock registration, so it reads
  // orange rather than taking the vendor's own colour.
  uint16_t accent;
  if (this->banner_hit.conf != SURV_CONFIRMED) {
    accent = UI_ORANGE;
  }
  else switch (this->banner_hit.vendor) {
    case SURV_FLOCK:       accent = UI_RED;    break;
    case SURV_AXON:        accent = UI_YELLOW; break;
    case SURV_SHOTSPOTTER: accent = 0xF81F;    break;  // magenta: R/B symmetric
    case SURV_AXIS:        accent = UI_CYAN;   break;
    default:               accent = UI_YELLOW; break;
  }

  uint16_t bg = phase ? accent : ST77XX_BLACK;
  uint16_t fg = phase ? ST77XX_BLACK : accent;

  display.tft->fillRect(0, 18, TFT_WIDTH, 28, bg);
  display.tft->setTextSize(1);
  display.tft->setTextColor(fg, bg);

  // Line 1: vendor, plus the deployment kind once known.
  String title = String(SurveillanceDetect::vendorName(this->banner_hit.vendor));
  title += SurveillanceDetect::kindSuffix(this->banner_hit.kind);
  if (this->banner_hit.conf != SURV_CONFIRMED) title += "?";
  title = "*" + title + "*";
  if (title.length() > 26) title = title.substring(0, 26);

  int16_t x1 = (TFT_WIDTH - (int16_t)title.length() * 6) / 2;
  display.tft->setCursor(x1 < 0 ? 0 : x1, 23);
  display.tft->print(title);

  // Line 2: identity, proximity and rating. The score matters most for
  // weak signatures, which can never rise above the low 60s.
  String detail = strlen(this->banner_hit.ident) ? String(this->banner_hit.ident)
                                                 : String(this->banner_hit.model);
  if (detail.length() > 11) detail = detail.substring(0, 11);
  detail += " " + String(this->banner_hit.rssi) + "dB";
  detail += " " + String(this->banner_hit.score) + "%";
  if (detail.length() > 26) detail = detail.substring(0, 26);

  int16_t x2 = (TFT_WIDTH - (int16_t)detail.length() * 6) / 2;
  display.tft->setCursor(x2 < 0 ? 0 : x2, 34);
  display.tft->print(detail);
}

// ============================================================
// Screen 1 — new large-format stats display
// Layout for 160x80px:
//   y=0  : GPS status + battery % + scan status  (size 1)
//   y=19 : divider
//   y=21 : 2.4GHz / 5GHz / BLE labels            (size 1)
//   y=30 : big counts                             (size 2, 16px tall)
//   y=47 : divider
//   y=50 : NET / BLE totals                       (size 2)
//   y=71 : geofence label (only when inside zone) (size 1)
// ============================================================
void UI::drawStatsNew(uint32_t currentTime, uint32_t count2g4, uint32_t count5g,
                      uint32_t bleCount, int gpsSats, int8_t batteryLevel, bool do_now) {

  if ((currentTime - lastUpdateTime < UI_UPDATE_TIME) && (!do_now)) return;
  lastUpdateTime = currentTime;

  display.tft->setRotation(3);
  display.tft->setTextWrap(false);

  // Chrome only needs painting when the screen was cleared.
  if (this->full_repaint) {
    display.tft->fillScreen(ST77XX_BLACK);
    display.tft->drawFastHLine(0, 46, TFT_WIDTH, 0x4208);
    this->full_repaint = false;
    this->banner_phase = -1;
  }

  bool has_fix = gps.getFixStatus();

  drawField(0, 0, 1, has_fix ? ST77XX_GREEN : UI_RED,
            has_fix ? (String(gpsSats) + " sats") : "No GPS fix", 11);

  bool hunting  = (wifi_ops.run_mode == FLOCK_MODE);
  bool scanning = (wifi_ops.getCurrentScanMode() == WIFI_WARDRIVING);
  drawField(104, 0, 1,
            hunting  ? UI_RED :
            scanning ? ST77XX_GREEN : UI_YELLOW,
            hunting  ? "HUNT" : scanning ? "SCAN" : "STBY", 4);

  String batStr = (batteryLevel < 0) ? "--" : String(batteryLevel) + "%";
  while (batStr.length() < 4) batStr = " " + batStr;
  drawField(136, 0, 1,
            (batteryLevel > 50) ? ST77XX_GREEN :
            (batteryLevel > 20) ? UI_YELLOW : UI_RED,
            batStr, 4);

  // Five decimals is about a metre. The GPS strings carry seven, past
  // what a float represents, so reformat rather than truncate.
  if (has_fix) {
    char pos[28];
    snprintf(pos, sizeof(pos), "%.5f,%.5f",
             gps.getLat().toFloat(), gps.getLon().toFloat());
    drawField(0, 9, 1, UI_CYAN, String(pos), 26);
  } else {
    drawField(0, 9, 1, UI_RED, "NO POSITION FIX", 26);
  }

  // The banner covers the per-cycle counters and nothing else: they
  // reset every scan anyway, so position and totals stay readable.
  if (this->banner_active) {
    this->drawBanner(currentTime);
  }
  else {
    uint16_t col_w = TFT_WIDTH / 3;

    display.tft->drawFastHLine(0, 18, TFT_WIDTH, 0x4208);

    drawField(col_w * 0 + (col_w - 6 * 6) / 2, 20, 1, 0x7BEF, "2.4GHz", 6);
    drawField(col_w * 1 + (col_w - 6 * 4) / 2, 20, 1, 0x7BEF, "5GHz",   4);
    drawField(col_w * 2 + (col_w - 6 * 3) / 2, 20, 1, 0x7BEF, "BLE",    3);

    drawField(col_w * 0 + 2, 29, 2, UI_CYAN, String(count2g4), 4);
    drawField(col_w * 1 + 2, 29, 2, UI_CYAN, String(count5g),  4);
    drawField(col_w * 2 + 2, 29, 2, 0xF81F,      String(bleCount), 4);
  }

  uint32_t nets = wifi_ops.getTotalNetCount();
  uint32_t bles = wifi_ops.getTotalBLECount();

  // Past 9999 the value drops to size 1. Glyphs are 16px tall at size
  // 2 and 8px at size 1, so wipe the row or the taller digits linger.
  bool small = (nets > 9999) || (bles > 9999);
  if (small != this->totals_small) {
    display.tft->fillRect(0, 48, TFT_WIDTH, 16, ST77XX_BLACK);
    this->totals_small = small;
  }
  int16_t val_y = small ? 52 : 48;

  drawField(0,   48,    2,             0x7BEF,       "W:",         2);
  drawField(24,  val_y, small ? 1 : 2, ST77XX_GREEN, String(nets), small ? 8 : 4);
  drawField(80,  48,    2,             0x7BEF,       "B:",         2);
  drawField(104, val_y, small ? 1 : 2, 0xF81F,       String(bles), small ? 8 : 4);

  // Under W:/B: because they are the same kind of number: cumulative
  // for the session, not per scan cycle.
  uint32_t flock = surveillance.getFlockCount();
  uint32_t axon  = surveillance.getAxonCount();
  uint32_t axis  = surveillance.getAxisCount();

  drawField(0,  70, 1, flock ? UI_RED    : 0x7BEF, "F" + String(flock), 4);
  drawField(30, 70, 1, axon  ? UI_YELLOW : 0x7BEF, "A" + String(axon),  4);
  drawField(60, 70, 1, axis  ? UI_CYAN   : 0x7BEF, "X" + String(axis),  4);

  // In hunt mode the channel and frame count show it is hearing traffic.
  if (wifi_ops.run_mode == FLOCK_MODE) {
    uint32_t fr = wifi_ops.getFlockFrames();
    String frames = (fr >= 10000) ? String(fr / 1000) + "k" : String(fr);
    drawField(90, 70, 1, UI_RED,
              "c" + String(wifi_ops.getFlockChannel()) + " f" + frames, 11);
  }
  else if (!sd_obj.supported) {
    drawField(90, 70, 1, UI_RED, "NO SD CARD", 11);
  }
  else if (wifi_ops.in_geofence && wifi_ops.current_geo_label.length() > 0) {
    char dist[16] = {0};
    String geo = "GEO:" + wifi_ops.current_geo_label;
    if (wifi_ops.checkGeofences(dist, sizeof(dist)))
      geo += " " + String(dist);
    drawField(90, 70, 1, UI_YELLOW, geo, 11);
  }
  else if (wifi_ops.run_mode == CORE_MODE) {
    drawField(90, 70, 1, ST77XX_WHITE,
              "Nodes:" + String(wifi_ops.getNodeCount()), 11);
  }
  else {
    drawField(90, 70, 1, ST77XX_BLACK, "", 11);
  }
}

// ============================================================
// Screen 2 — original stats display (unchanged)
// ============================================================
void UI::updateStats(uint32_t currentTime, uint32_t wifiCount, uint32_t count2g4,
                     uint32_t count5g, uint32_t bleCount, int gpsSats,
                     int8_t batteryLevel, bool do_now) {

  if ((currentTime - lastUpdateTime < UI_UPDATE_TIME) && (!do_now)) return;
  lastUpdateTime = currentTime;

  display.tft->setRotation(3);
  display.tft->setTextWrap(false);

  if (this->full_repaint) {
    display.tft->fillScreen(ST77XX_BLACK);
    this->full_repaint = false;
  }

  display.tft->setTextSize(1);
  this->printFirmwareVersion();
  this->printBatteryLevel(batteryLevel);

  bool has_fix = gps.getFixStatus();

  if (has_fix) {
    char pos[28];
    snprintf(pos, sizeof(pos), "%.5f,%.5f",
             gps.getLat().toFloat(), gps.getLon().toFloat());
    drawField(0, 9, 1, UI_CYAN, String(pos), 26);
  } else {
    drawField(0, 9, 1, UI_RED, "NO POSITION FIX", 26);
  }

  drawField(0, 18, 1, ST77XX_WHITE,
            (wifi_ops.getCurrentScanMode() == WIFI_WARDRIVING)
              ? "Status: SCANNING" : "Status: STANDBY", 26);

  if (sd_obj.supported)
    drawField(0, 27, 1, ST77XX_WHITE, "File: " + buffer.getFileName(), 26);
  else
    drawField(0, 27, 1, UI_RED, "No SD card", 26);

  drawField(0, 36, 1, ST77XX_WHITE,
            "2.4G:" + String(count2g4) + " 5G:" + String(count5g), 26);
  drawField(0, 45, 1, ST77XX_WHITE,
            "BLE:" + String(bleCount) +
            " Sats:" + (gpsSats > 0 ? String(gpsSats) : String("0")), 26);

  drawField(0,  54, 1, ST77XX_GREEN, "Total Nets:", 12);
  drawField(72, 54, 1, ST77XX_WHITE, String(wifi_ops.getTotalNetCount()), 14);
  drawField(0,  63, 1, 0xF81F,       "Total BLE:", 12);
  drawField(72, 63, 1, ST77XX_WHITE, String(wifi_ops.getTotalBLECount()), 14);

  uint32_t flock = surveillance.getFlockCount();
  uint32_t axon  = surveillance.getAxonCount();
  drawField(0,  72, 1, flock ? UI_RED : 0x7BEF, "FLOCK:" + String(flock), 13);
  drawField(78, 72, 1, axon  ? UI_YELLOW     : 0x7BEF, "AXON:"  + String(axon),  13);
}

void UI::setupSDFileList() {
  sd_obj.sd_files->clear();
  delete sd_obj.sd_files;
  sd_obj.sd_files = new LinkedList<String>();
  sd_obj.listDirToLinkedList(sd_obj.sd_files, "/", ".log");
}

void UI::buildSDFileMenu() {
  // Mode entries are always built. Only log-file entries need the card,
  // and Flock hunt needs neither card nor GPS.
  if (sd_obj.supported)
    this->setupSDFileList();

  sd_file_menu.list->clear();
  delete sd_file_menu.list;
  sd_file_menu.list = new LinkedList<MenuNode>();
  sd_file_menu.name = sd_obj.supported ? "Logs" : "Menu";

  this->addNodes(&sd_file_menu, "Back", ST77XX_WHITE, NULL, 0, [this]() {
    this->setDisplayMode(STATS_NEW);
    if (sd_obj.supported && buffer.getFileName() == "") {
      Logger::log(STD_MSG, "Active log file was deleted. Creating new one...");
      wifi_ops.startLog(LOG_FILE_NAME);
      Logger::log(STD_MSG, "New log file: " + buffer.getFileName());
    }
    this->hard_refresh = true;
  });

  this->addNodes(&sd_file_menu, "Mode", ST77XX_WHITE, NULL, 0, [this]() {
    this->current_menu = &mode_menu;
  });

  if (sd_obj.supported) {
    this->addNodes(&sd_file_menu, "Delete Wardrive Logs", ST77XX_WHITE, NULL, 0, [this]() {
      this->current_menu = &delete_all_menu;
    });

    this->addNodes(&sd_file_menu, "Upload All", ST77XX_WHITE, NULL, 0, [this]() {
      this->current_menu = &upload_all_menu;
    });

    this->addNodes(&sd_file_menu, "Mark New Geofence", ST77XX_WHITE, NULL, 0, [this]() {
      this->current_menu = &mark_geofence_menu;
    });

    for (int i = 0; i < sd_obj.sd_files->size(); i++) {
      File current_file = sd_obj.getFile("/" + sd_obj.sd_files->get(i));
      if (sd_obj.sd_files->get(i).startsWith("wardrive_") || sd_obj.sd_files->get(i).startsWith("wigle-")) {
        this->addNodes(&sd_file_menu, sd_obj.sd_files->get(i), ST77XX_WHITE, NULL, 0, [this, i]() {
          sd_obj.selected_file_name = sd_obj.sd_files->get(i);
          Logger::log(STD_MSG, sd_obj.sd_files->get(i) + " selected");
          this->current_menu = &action_menu;
        }, current_file.size());
      }
    }

    Logger::log(STD_MSG, "Built menu with " + (String)sd_obj.sd_files->size() + " log files");
  } else {
    Logger::log(WARN_MSG, "No SD card, built reduced menu (Mode only)");
  }
}

void UI::addNodes(Menu * menu, String name, uint16_t color, Menu * child, int place,
                  std::function<void()> callable, uint32_t size, bool selected, String command) {
  menu->list->add(MenuNode{name, false, color, place, selected, callable, size});
}

void UI::drawCurrentMenu() {
  if (!current_menu || current_menu->list->size() == 0) return;

  const uint8_t max_visible_items = 7;
  const uint8_t header_height     = 8;

  display.tft->setRotation(3);
  display.tft->fillScreen(ST77XX_BLACK);
  display.tft->setTextSize(1);
  display.tft->setTextWrap(false);

  display.tft->setTextColor(ST77XX_WHITE);
  display.tft->setCursor(0, 0);
  display.tft->println(current_menu->name);

  if (current_menu->selected < current_menu->scroll_offset)
    current_menu->scroll_offset = current_menu->selected;
  else if (current_menu->selected >= current_menu->scroll_offset + max_visible_items)
    current_menu->scroll_offset = current_menu->selected - max_visible_items + 1;

  for (int i = 0; i < max_visible_items; i++) {
    int item_index = current_menu->scroll_offset + i;
    if (item_index >= current_menu->list->size()) break;

    MenuNode node = current_menu->list->get(item_index);
    int y = header_height + i * 8;

    if (item_index == current_menu->selected) {
      display.tft->setTextColor(ST77XX_BLACK, ST77XX_WHITE);
      display.tft->setCursor(0, y);
      display.tft->print("> ");
    } else {
      display.tft->setTextColor(node.color, ST77XX_BLACK);
      display.tft->setCursor(0, y);
      display.tft->print("  ");
    }
    display.tft->print(node.name);

    String sizeStr = "";
    if (node.fileSize > 0) {
      sizeStr  = String((node.fileSize + 1023) / 1024);
      sizeStr += " KB";
    }
    int xRightAlign = TFT_WIDTH - sizeStr.length() * 6;
    display.tft->setCursor(xRightAlign, y);
    display.tft->print(sizeStr);
  }

  if (current_menu->scroll_offset > 0) {
    display.tft->setCursor(TFT_WIDTH - 10, header_height);
    display.tft->setTextColor(ST77XX_WHITE);
    display.tft->print("^");
  }
  if (current_menu->scroll_offset + max_visible_items < current_menu->list->size()) {
    display.tft->setCursor(TFT_WIDTH - 10, header_height + (max_visible_items - 1) * 8);
    display.tft->setTextColor(ST77XX_WHITE);
    display.tft->print("v");
  }
}

void UI::handleMenuNavigation() {
  if (!current_menu || current_menu->list->size() == 0) return;

  int list_size = current_menu->list->size();

  if (u_btn.justPressed()) {
    if (current_menu->selected == 0)
      current_menu->selected = list_size - 1;
    else
      current_menu->selected--;
    drawCurrentMenu();
  }

  if (d_btn.justPressed()) {
    current_menu->selected = (current_menu->selected + 1) % list_size;
    drawCurrentMenu();
  }

  if (c_btn.justPressed()) {
    MenuNode node = current_menu->list->get(current_menu->selected);
    if (node.callable) node.callable();
    drawCurrentMenu();
  }
}

void UI::doHardRefresh() {
  if (this->hard_refresh) {
    Logger::log(STD_MSG, "Hard-refreshing display...");
    display.clearScreen();
    this->hard_refresh = false;
    this->full_repaint = true;
  }
}

void UI::main(uint32_t currentTime) {

  // Handle dock departure display reset
  extern bool g_force_display_redraw;
  if (g_force_display_redraw) {
    this->last_stat_display_mode = 255;
    this->lastUpdateTime         = 0;
    g_force_display_redraw       = false;
    this->full_repaint           = true;
    display.tft->fillScreen(ST77XX_BLACK);

    // The menu is otherwise only repainted on a button press, so a
    // forced blank while it is up leaves a black screen until the user
    // presses something.
    if (this->stat_display_mode == SD_FILES)
      this->drawCurrentMenu();
  }

  // Drain first, whatever is on screen: this writes the SD log, so it
  // must not depend on the current screen or on being docked.
  this->serviceAlerts(currentTime);

  // Don't draw stats while docked, dock mode manages its own display
  if (wifi_ops.isDocked())
    return;

  bool in_stats = (this->stat_display_mode != SD_FILES);

  if (in_stats) {

    // ---- Screen 3: Incognito ----
    if (this->stat_display_mode == INCOGNITO) {

      if (!this->incognito_counting) {
        this->incognito_counting = true;
        this->incognito_start_ms = currentTime;
        this->incognito_last_sec = -1;
        display.tft->fillScreen(ST77XX_BLACK);
        this->last_stat_display_mode = INCOGNITO;
      }

      uint32_t elapsed  = currentTime - this->incognito_start_ms;
      int      secs_rem = (elapsed < 5000) ? (int)(5 - elapsed / 1000) : 0;

      if (elapsed < 5000) {
        if (secs_rem != this->incognito_last_sec) {
          this->incognito_last_sec = secs_rem;
          display.tft->fillScreen(ST77XX_BLACK);
          display.tft->setTextSize(1);
          display.tft->setTextColor(UI_YELLOW, ST77XX_BLACK);
          uint16_t lblX = (TFT_WIDTH - 14 * 6) / 2;
          display.tft->setCursor(lblX > 0 ? lblX : 0, 26);
          display.tft->print("INCOGNITO MODE");
          display.tft->setTextSize(3);
          display.tft->setTextColor(UI_YELLOW, ST77XX_BLACK);
          char buf[3];
          snprintf(buf, sizeof(buf), "%d", secs_rem);
          display.tft->setCursor((TFT_WIDTH - 18) / 2, 44);
          display.tft->print(buf);
        }
      } else {
        if (this->incognito_counting) {
          display.ctrlBacklight(false);
          display.tft->fillScreen(ST77XX_BLACK);
        }
      }

      // Any button press exits incognito — debounced
      if ((u_btn.justPressed() || d_btn.justPressed()) &&
          (currentTime - this->last_mode_change_ms >= 300)) {
        this->setDisplayMode(STATS_NEW);
        display.tft->fillScreen(ST77XX_BLACK);
      }
      return;
    }

    // Leaving incognito — restore backlight
    if (this->incognito_counting) {
      this->incognito_counting = false;
      display.ctrlBacklight(true);
    }

    this->doHardRefresh();

    // ---- Screen 1: new large-format stats ----
    if (this->stat_display_mode == STATS_NEW) {
      this->drawStatsNew(
        currentTime,
        wifi_ops.getCurrent2g4Count(),
        wifi_ops.getCurrent5gCount(),
        wifi_ops.getCurrentBLECount(),
        gps.getNumSats(),
        battery.battery_level,
        false
      );
    }
    // ---- Screen 2: original stats ----
    else if (this->stat_display_mode == FULL_STATS) {
      this->updateStats(
        currentTime,
        wifi_ops.getCurrentNetCount(),
        wifi_ops.getCurrent2g4Count(),
        wifi_ops.getCurrent5gCount(),
        wifi_ops.getCurrentBLECount(),
        gps.getNumSats(),
        battery.battery_level
      );
    }

    // ---- Button handling — debounced at 300ms ----
    bool mode_change_ok = (currentTime - this->last_mode_change_ms >= 300);

    if (u_btn.justPressed() && mode_change_ok) {
      uint8_t next = (this->stat_display_mode >= MAX_DISPLAY_MODES - 1)
                       ? 0 : this->stat_display_mode + 1;
      this->setDisplayMode(next);
      if (next == SD_FILES)
        this->drawCurrentMenu();
      else if (next == STATS_NEW)
        this->drawStatsNew(currentTime,
          wifi_ops.getCurrent2g4Count(), wifi_ops.getCurrent5gCount(),
          wifi_ops.getCurrentBLECount(), gps.getNumSats(),
          battery.battery_level, true);
      else if (next == FULL_STATS)
        this->updateStats(currentTime,
          wifi_ops.getCurrentNetCount(), wifi_ops.getCurrent2g4Count(),
          wifi_ops.getCurrent5gCount(), wifi_ops.getCurrentBLECount(),
          gps.getNumSats(), battery.battery_level, true);
    }

    if (d_btn.justPressed() && mode_change_ok) {
      uint8_t next = (this->stat_display_mode == 0)
                       ? MAX_DISPLAY_MODES - 1 : this->stat_display_mode - 1;
      this->setDisplayMode(next);
      if (next == SD_FILES)
        this->drawCurrentMenu();
      else if (next == STATS_NEW)
        this->drawStatsNew(currentTime,
          wifi_ops.getCurrent2g4Count(), wifi_ops.getCurrent5gCount(),
          wifi_ops.getCurrentBLECount(), gps.getNumSats(),
          battery.battery_level, true);
      else if (next == FULL_STATS)
        this->updateStats(currentTime,
          wifi_ops.getCurrentNetCount(), wifi_ops.getCurrent2g4Count(),
          wifi_ops.getCurrent5gCount(), wifi_ops.getCurrentBLECount(),
          gps.getNumSats(), battery.battery_level, true);
    }

    if (c_btn.justPressed())
      Logger::log(STD_MSG, "C_BTN Pressed: " + (String)millis());

  } else if (this->stat_display_mode == SD_FILES) {
    this->handleMenuNavigation();
  }
}
