#include <M5StickCPlus2.h>

// Определение уровней энергопотребления
enum PowerMode {
    MODE_STEALTH, // Минимум потребления, экран выключен
    MODE_BOOST,   // Максимум питания на внешние модули (CC1101/NFC)
    MODE_NORMAL   // Стандартный режим
};

void setPowerProfile(PowerMode mode) {
    switch (mode) {
        case MODE_STEALTH:
            M5.Lcd.setBrightness(0);
            setCpuFrequencyMhz(80); // Снижаем частоту
            M5.Axp.setALDO1Voltage(0, false); // Выключаем лишнюю периферию
            break;

        case MODE_BOOST:
            M5.Lcd.setBrightness(50); 
            setCpuFrequencyMhz(240); // Максимальная мощь
            // Усиление питания на шине 3.3V для стабильной работы радио
            M5.Axp.setALDO3Voltage(3300, true); 
            M5.Axp.setDCDC1Voltage(3300, true);
            break;

        case MODE_NORMAL:
            M5.Lcd.setBrightness(100);
            setCpuFrequencyMhz(160);
            break;
    }
}
