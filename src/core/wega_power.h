#ifndef WEGA_POWER_H
#define WEGA_POWER_H
#include <M5StickCPlus2.h>

enum PowerMode { MODE_STEALTH, MODE_BOOST, MODE_NORMAL };

class WegaPower {
public:
    void begin() { M5.Power.begin(); setMode(MODE_NORMAL); }
    void setMode(PowerMode mode) {
        if (mode == MODE_STEALTH) { M5.Lcd.setBrightness(0); setCpuFrequencyMhz(80); }
        else if (mode == MODE_BOOST) { M5.Lcd.setBrightness(50); setCpuFrequencyMhz(240); }
        else { M5.Lcd.setBrightness(100); setCpuFrequencyMhz(160); }
    }
};
extern WegaPower PowerUnit;
#endif
