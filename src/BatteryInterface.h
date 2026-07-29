#pragma once

#ifndef BatteryInterface_h
#define BatteryInterface_h

#include <Arduino.h>

#include "configs.h"
#include "Adafruit_MAX1704X.h"
#include "utils.h"
#include "logger.h"

#include <Wire.h>

#define IP5306_ADDR   0x75
#define MAX17048_ADDR 0x36

// Settling time before the gauge's charge estimate is meaningful.
#define MAX17048_SETTLE_MS 1500

class BatteryInterface {
  private:
    uint32_t initTime      = 0;   // poll timer, reset every cycle
    uint32_t setupTime     = 0;   // power-on, fixed
    Adafruit_MAX17048 maxlipo;

  public:
    float  cell_volts    = 0.0f;

  private:

  public:
    int8_t battery_level = -1;   // -1 until the gauge has settled
    int8_t old_level     = 0;
    bool   i2c_supported = false;
    bool   has_max17048  = false;
    bool   has_ip5306    = false;

    BatteryInterface();

    void   RunSetup();
    void   main(uint32_t currentTime);
    int8_t getBatteryLevel();
};

#endif
