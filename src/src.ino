#include "configs.h"
#include "BatteryInterface.h"
#include "Buffer.h"
#include "settings.h"
#include "display.h"
#include "GpsInterface.h"
#include "SDInterface.h"
#include "Switches.h"
#include "WiFiOps.h"
#include "utils.h"
#include "ui.h"
#include "logger.h"
#include "SurveillanceDetect.h"

Buffer buffer;
Settings settings;
GpsInterface gps;
BatteryInterface battery;
WiFiOps wifi_ops;
Utils utils;
UI ui_obj;
SurveillanceDetect surveillance;
bool g_force_display_redraw = false;

SPIClass sharedSPI(SPI);
Display display = Display(&sharedSPI, TFT_CS, TFT_DC, TFT_RST);
SDInterface sd_obj = SDInterface(&sharedSPI, SD_CS);

Switches u_btn = Switches(U_BTN, 1000, U_PULL);
Switches d_btn = Switches(D_BTN, 1000, D_PULL);
Switches c_btn = Switches(C_BTN, 1000, C_PULL);

void setup() {
  Serial.begin(115200);

  while (!Serial)
    delay(10);

  // Do SPI stuff first
  sharedSPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);

  // Give SPI some time I guess
  delay(100);

  // Init the display before SD
  display.begin();

  // Give SD some time
  delay(100);

  // Show us IDF information
  Logger::log(STD_MSG, "ESP-IDF version is: " + String(esp_get_idf_version()));

  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);

  // Load settings
  settings.begin();

  if (settings.getSettingType(SETTING_SANITY) == "") {
    Logger::log(WARN_MSG, "Current settings format not supported. Installing new default settings...");
    settings.createDefaultSettings(SPIFFS);
  }
  else {
    Logger::log(GUD_MSG, "Current settings format supported");
  }

  // Init our buffer for writing logs
  buffer = Buffer();

  // Init SD Card
  if(!sd_obj.initSD())
    Logger::log(WARN_MSG, "SD Card NOT Supported");

  // Check for firmware updates now
  Logger::log(STD_MSG, "Checking for firmware updates...");
  sd_obj.runUpdate();

  // Enable SD debug logging if setting is on
  Logger::enableSDLog(settings.loadSetting<bool>(DEBUG_LOG_NAME));

  // Init battery
  battery.RunSetup();

  // Init GPS
  gps.begin();

  // Needs settings loaded
  surveillance.begin();

  ui_obj.begin();

  // Init wifi and bluetooth
  wifi_ops.begin(c_btn.justPressed());

  // Init UI
  ui_obj.begin();

  settings.printJsonSettings(settings.getSettingsString());

  Logger::log(GUD_MSG, "Initialization complete!");
}

void loop() {
  // Take current time of this loop for functions
  uint32_t currentTime = millis();

  // Refresh all functions
  wifi_ops.main(currentTime, ui_obj.stat_display_mode == SD_FILES);
  settings.main(currentTime);
  battery.main(currentTime);
  gps.main();
  sd_obj.main();
  buffer.save();
  ui_obj.main(currentTime);

  // Flock hunt drives its own radio and needs no GPS fix or SD card.
  if (wifi_ops.run_mode == FLOCK_MODE)
    wifi_ops.setCurrentScanMode(WIFI_WARDRIVING);
  // Solo or Core modes
  else if ((gps.getFixStatus()) && (sd_obj.supported) && (ui_obj.stat_display_mode != SD_FILES))
    wifi_ops.setCurrentScanMode(WIFI_WARDRIVING);
  // Nodes
  else if ((wifi_ops.run_mode == NODE_MODE) && (wifi_ops.getNodeReady())) {
    wifi_ops.setCurrentScanMode(WIFI_WARDRIVING);
    digitalWrite(LED_PIN, HIGH);
  }
  else {
    wifi_ops.setCurrentScanMode(WIFI_STANDBY);
    if (wifi_ops.run_mode == NODE_MODE)
      digitalWrite(LED_PIN, LOW);
  }
}
