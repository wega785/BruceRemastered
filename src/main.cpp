#include <M5StickCPlus2.h>
#include "core/main_menu.h"
#include <globals.h>
#include "core/powerSave.h"
#include "core/serial_commands/cli.h"
#include "core/utils.h"
#include "esp32-hal-psram.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include <functional>
#include <string>
#include <vector>
#include <Wire.h>

// Библиотеки модулей
#include "core/display.h"
#include "core/led_control.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/serialcmds.h"
#include "core/settings.h"
#include "core/wifi/webInterface.h"
#include "core/wifi/wifi_common.h"
#include "modules/bjs_interpreter/interpreter.h" 
#include "modules/others/audio.h"
#include "modules/rf/rf_utils.h"

// --- [WEGA+ POWER MODULE START] ---
enum PowerMode {
    MODE_STEALTH, 
    MODE_BOOST,   
    MODE_NORMAL   
};

void setPowerProfile(PowerMode mode) {
    switch (mode) {
        case MODE_STEALTH:
            M5.Lcd.setBrightness(0);
            M5.Lcd.sleep();
            setCpuFrequencyMhz(80);
            break;
        case MODE_BOOST:
            M5.Lcd.wakeup();
            M5.Lcd.setBrightness(40);
            setCpuFrequencyMhz(240);
            M5.Power.setVBusPowerMode(1); 
            break;
        case MODE_NORMAL:
            M5.Lcd.wakeup();
            M5.Lcd.setBrightness(100);
            setCpuFrequencyMhz(160);
            break;
    }
}
// --- [WEGA+ POWER MODULE END] ---

// Глобальные объекты Bruce
io_expander ioExpander;
BruceConfig bruceConfig;
BruceConfigPins bruceConfigPins;
SerialCli serialCli;
USBSerial USBserial;
SerialDevice *serialDevice = &USBserial;
StartupApp startupApp;
MainMenu mainMenu;
SPIClass sdcardSPI;

#ifdef USE_HSPI_PORT
SPIClass CC_NRF_SPI(VSPI);
#else
SPIClass CC_NRF_SPI(HSPI);
#endif

// Переменные навигации
volatile bool NextPress = false, PrevPress = false, UpPress = false, DownPress = false;
volatile bool SelPress = false, EscPress = false, AnyKeyPress = false;
volatile bool NextPagePress = false, PrevPagePress = false, LongPress = false;
volatile bool SerialCmdPress = false;
volatile int forceMenuOption = -1;
volatile uint8_t menuOptionType = 0;
String menuOptionLabel = "";
TouchPoint touchPoint;
keyStroke KeyStroke;
TaskHandle_t xHandle;

// Состояния системы
unsigned long previousMillis = millis();
int prog_handler; 
String cachedPassword = "";
bool interpreter_start = false;
bool sdcardMounted = false;
bool gpsConnected = false;
bool wifiConnected = false;
bool isWebUIActive = false;
String wifiIP;
bool BLEConnected = false;
bool returnToMenu;
bool isSleeping = false, isScreenOff = false, dimmer = false;
char timeStr[10];
time_t localTime;
struct tm *timeInfo;

#if defined(HAS_RTC)
cplus_RTC _rtc;
RTC_TimeTypeDef _time;
RTC_DateTypeDef _date;
bool clock_set = true;
#else
ESP32Time rtc;
bool clock_set = false;
#endif

std::vector<Option> options;

#if defined(HAS_SCREEN)
tft_logger tft = tft_logger(); 
TFT_eSprite sprite = TFT_eSprite(&tft);
TFT_eSprite draw = TFT_eSprite(&tft);
volatile int tftWidth = TFT_HEIGHT;
#ifdef HAS_TOUCH
volatile int tftHeight = TFT_WIDTH - 20;
#else
volatile int tftHeight = TFT_WIDTH;
#endif
#else
tft_logger tft;
SerialDisplayClass &sprite = tft;
SerialDisplayClass &draw = tft;
volatile int tftWidth = VECTOR_DISPLAY_DEFAULT_HEIGHT;
volatile int tftHeight = VECTOR_DISPLAY_DEFAULT_WIDTH;
#endif

// Функции Bruce
void begin_storage() {
    if (!LittleFS.begin(true)) { LittleFS.format(), LittleFS.begin(); }
    bool checkFS = setupSdCard();
    bruceConfig.fromFile(checkFS);
    bruceConfigPins.fromFile(checkFS);
}

void setup_gpio() {
    ioExpander.init(IO_EXPANDER_ADDRESS, &Wire);
#if TFT_MOSI > 0
    if (bruceConfigPins.CC1101_bus.mosi == (gpio_num_t)TFT_MOSI) initCC1101once(&tft.getSPIinstance());
    else
#endif
    if (bruceConfigPins.CC1101_bus.mosi == bruceConfigPins.SDCARD_bus.mosi) initCC1101once(&sdcardSPI);
    else initCC1101once(NULL);
}

void begin_tft() {
    tft.setRotation(bruceConfigPins.rotation);
    tft.invertDisplay(bruceConfig.colorInverted);
    tftWidth = tft.width();
    tftHeight = tft.height();
#ifdef HAS_TOUCH
    tftHeight -= 20;
#endif
    resetTftDisplay();
    setBrightness(bruceConfig.bright, false);
}

void taskInputHandler(void *parameter) {
    auto timer = millis();
    while (true) {
        checkPowerSaveTime();
        if (!AnyKeyPress || millis() - timer > 75) {
            NextPress = PrevPress = UpPress = DownPress = SelPress = EscPress = AnyKeyPress = false;
            SerialCmdPress = NextPagePress = PrevPagePress = false;
            touchPoint.pressed = false;
            touchPoint.Clear();
#ifndef USE_TFT_eSPI_TOUCH
            InputHandler();
#endif
            timer = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ГЛАВНЫЙ SETUP
void setup() {
    // 1. Инициализация WEGA+ Power
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Power.begin();
    setPowerProfile(MODE_NORMAL);

    // 2. Serial & PSRAM
    Serial.begin(115200);
    if (psramInit()) log_d("PSRAM Started");

    // 3. Bruce Init
    prog_handler = 0;
    sdcardMounted = false;
    wifiConnected = false;
    bruceConfig.bright = 100;
    bruceConfigPins.rotation = 1;

    setup_gpio();
    
#if defined(HAS_SCREEN)
    tft.init();
    tft.setRotation(bruceConfigPins.rotation);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_PURPLE, TFT_BLACK);
    tft.drawCentreString("Booting Bruce + WEGA+", tft.width() / 2, tft.height() / 2, 1);
#endif

    begin_storage();
    begin_tft();

    // WiFi Power
    wifi_country_t country = {.cc = "US", .schan = 1, .nchan = 14, .max_tx_power = 80, .policy = WIFI_COUNTRY_POLICY_MANUAL};
    esp_wifi_set_max_tx_power(80);
    esp_wifi_set_country(&country);

    // Ввод
    xTaskCreate(taskInputHandler, "InputHandler", 4096, NULL, 2, &xHandle);

    startSerialCommandsHandlerTask();
    wakeUpScreen();
}

void loop() {
#if defined(HAS_SCREEN)
    tft.fillScreen(bruceConfig.bgColor);
    mainMenu.begin();
    delay(1);
#else
    vTaskDelay(10 / portTICK_PERIOD_MS);
#endif
}
