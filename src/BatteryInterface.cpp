#include "BatteryInterface.h"

BatteryInterface::BatteryInterface() {
}

void BatteryInterface::main(uint32_t currentTime) {
  if (currentTime != 0) {
    if (currentTime - initTime >= 3000) {
      this->initTime = millis();

      int8_t new_level = this->getBatteryLevel();
      if (this->battery_level != new_level) {
        this->battery_level = new_level;
        // Voltage alongside percent: a pack reading 100% and a missing
        // pack where the gauge sees the USB rail look identical
        // otherwise. Under load a real cell sits well below 4.2V.
        Logger::log(STD_MSG, "Battery: " + (String)new_level + "% " +
                             String(this->cell_volts, 2) + "V");
      }
    }
  }
}

void BatteryInterface::RunSetup() {
  byte error;

  #ifdef HAS_BATTERY

    Wire.begin(I2C_SDA, I2C_SCL);

    Logger::log(STD_MSG, "Checking for battery monitors...");

    Wire.beginTransmission(IP5306_ADDR);
    error = Wire.endTransmission();

    if (error == 0) {
      Logger::log(GUD_MSG, "Detected IP5306");
      this->has_ip5306    = true;
      this->i2c_supported = true;
    }

    Wire.beginTransmission(MAX17048_ADDR);
    error = Wire.endTransmission();

    if (error == 0) {
      if (maxlipo.begin()) {
        Logger::log(GUD_MSG, "Detected MAX17048");
        this->has_max17048  = true;
        this->i2c_supported = true;
      }
    }

    this->initTime  = millis();
    this->setupTime = this->initTime;

  #endif
}

int8_t BatteryInterface::getBatteryLevel() {

  if (this->has_ip5306) {
    Wire.beginTransmission(IP5306_ADDR);
    Wire.write(0x78);
    if (Wire.endTransmission(false) == 0 &&
        Wire.requestFrom(IP5306_ADDR, 1)) {
      this->i2c_supported = true;
      switch (Wire.read() & 0xF0) {
        case 0xE0: return 25;
        case 0xC0: return 50;
        case 0x80: return 75;
        case 0x00: return 100;
        default:   return 0;
      }
    }
    this->i2c_supported = false;
    return -1;
  }

  if (this->has_max17048) {
    // The gauge derives charge from cell voltage and needs a moment
    // after power-on before that reading means anything. Sampling it
    // immediately reports 0%.
    if (millis() - this->setupTime < MAX17048_SETTLE_MS)
      return this->battery_level >= 0 ? this->battery_level : -1;

    float volts   = this->maxlipo.cellVoltage();
    float percent = this->maxlipo.cellPercent();

    // A disconnected gauge reads back as NaN or 0V over I2C.
    if (isnan(volts) || volts < 1.0f)
      return -1;

    this->cell_volts = volts;

    if (percent >= 100) return 100;
    if (percent <= 0)   return 0;
    return (int8_t)percent;
  }

  return 0;
}
