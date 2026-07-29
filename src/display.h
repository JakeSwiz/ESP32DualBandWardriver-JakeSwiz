#ifndef display_h
#define display_h

#include <FS.h>
#include <LinkedList.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <functional>

#include "configs.h"
#include "assets.h"

#include "BatteryInterface.h"

extern BatteryInterface battery;

// The JCMK host board panel is wired BGR: red and blue arrive swapped,
// so ST77XX_RED paints blue. These carry the swap so the names mean what
// they say. Green, white, black, grey and magenta need no correction,
// their red and blue fields are equal.
#ifdef JCMK_HOST_BOARD
  #define CYAN 0xFFE0
  #define UI_RED     0x001F
  #define UI_YELLOW  0x07FF
  #define UI_CYAN    0xFFE0
#else
  #define CYAN ST77XX_CYAN
  #define UI_RED     ST77XX_RED
  #define UI_YELLOW  ST77XX_YELLOW
  #define UI_CYAN    ST77XX_CYAN
#endif

class Display {
  public:
    int _cs, _dc, _rst;
    Display(SPIClass* spi, int cs, int dc, int rst);
    Adafruit_ST7735* tft;

    void begin();
    void main(uint32_t currentTime);
    void clearScreen();
    void ctrlBacklight(bool on = true);
    void drawCenteredText(String text, bool centerVertically = false);

  private:
    SPIClass* _spi;

    void drawMonochromeImage160x80(const uint8_t* imageData, int width, int height);

};

#endif