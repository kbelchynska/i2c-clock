#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "bme280.h"
#include "ds1307.h"
#include "ssd1306.h"

static const char *TAG = "clock";

//  Pins / I²C
constexpr uint8_t I2C_SDA = 8;
constexpr uint8_t I2C_SCL = 9;
constexpr uint32_t I2C_FREQ = 100000;

//  Pins / SPI (FSPI за замовчуванням на ESP32-S3)
constexpr uint8_t SPI_SCK = 12;
constexpr uint8_t SPI_MISO = 13;
constexpr uint8_t SPI_MOSI = 11;
constexpr uint8_t BME_CS = 10;

constexpr uint32_t RTC_POLL_MS = 200; // опитуємо RTC 5 разів на секунду
constexpr uint32_t BME_POLL_MS = 500; // BME280 — 2 рази на секунду (вимога: не рідше 1/с)

static const char *const WEEKDAYS[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

// Вміст екрана: перемальовуємо і логуємо лише коли він змінився
struct Screen
{
    char time[9];     // HH:MM:SS
    char date[15];    // EEE DD.MM.YYYY
    char climate[22]; // T: 23.4°C RH: 45%
    char pressure[22]; // P: 1013 hPa

    bool operator==(const Screen &o) const { return memcmp(this, &o, sizeof(Screen)) == 0; }
};

// State
DateTime now{};
bool rtcOk = false;
uint32_t lastRtcPollAt = 0;

BmeData climate{};
bool bmeOk = false;
uint32_t lastBmePollAt = 0;

Screen shown{};
bool forceRedraw = true;

// Час компіляції ("Oct 12 2026", "15:33:59") -> DateTime
static DateTime compileTime()
{
    static const char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4];
    int d, y, h, m, s;
    DateTime dt{};
    sscanf(__DATE__, "%3s %d %d", mon, &d, &y);
    sscanf(__TIME__, "%d:%d:%d", &h, &m, &s);
    dt.month = (strstr(MONTHS, mon) - MONTHS) / 3 + 1;
    dt.day = d;
    dt.year = y;
    dt.hour = h;
    dt.minute = m;
    dt.second = s;
    return dt;
}

//  Ініціалізація
static void initI2C()
{
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);
}

static void initDisplay()
{
    if (!oled::init())
        ESP_LOGE(TAG, "SSD1306 не відповідає на 0x3C");
}

static void initBme()
{
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, BME_CS);
    bmeOk = bme::init(BME_CS);
    if (!bmeOk)
        ESP_LOGE(TAG, "BME280 не відповідає по SPI (CS = GPIO%u)", BME_CS);
}

static void initRtc()
{
    if (!rtc::isRunning())
    {
        ESP_LOGW(TAG, "DS1307 зупинений або не знайдений -> встановлюю час компіляції");
        rtc::write(compileTime());
    }
}

//  Зчитування даних
static bool readTime(DateTime &dt)
{
    return rtc::read(dt);
}

static bool readClimate(BmeData &data)
{
    // Якщо датчик відвалився (або не було на старті) — пробуємо ініціалізувати знову
    if (!bmeOk && !bme::init(BME_CS))
        return false;
    return bme::read(data);
}

// Встановлення часу з Serial Monitor: "2026-02-14 15:33:59"
static void handleSerial()
{
    if (!Serial.available())
        return;
    String line = Serial.readStringUntil('\n');
    int y, mo, d, h, mi, s;
    if (sscanf(line.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6)
    {
        DateTime dt{(uint16_t)y, (uint8_t)mo, (uint8_t)d, 0, (uint8_t)h, (uint8_t)mi, (uint8_t)s};
        if (rtc::write(dt))
            ESP_LOGI(TAG, "Час встановлено");
        else
            ESP_LOGE(TAG, "Помилка запису в DS1307");
        lastRtcPollAt = millis() - RTC_POLL_MS; // перечитати RTC одразу
    }
    else
    {
        ESP_LOGW(TAG, "Формат: YYYY-MM-DD HH:MM:SS");
    }
}

//  Оновлення дисплея
static void drawCentered(int y, const char *text, uint8_t scale)
{
    oled::drawText((oled::WIDTH - oled::textWidth(text, scale)) / 2, y, text, scale);
}

// degree — "\xB0" для шрифту дисплея, "°" (UTF-8) для логу
static void formatClimate(char *buf, size_t size, const char *degree)
{
    if (bmeOk)
        snprintf(buf, size, "T: %.1f%sC RH: %.0f%%", climate.temperature, degree, climate.humidity);
    else
        snprintf(buf, size, "T: --.-%sC RH: --%%", degree);
}

static void composeScreen(Screen &s)
{
    memset(&s, 0, sizeof(s)); // щоб memcmp не залежав від сміття після '\0'
    if (rtcOk)
    {
        snprintf(s.time, sizeof(s.time), "%02u:%02u:%02u", now.hour, now.minute, now.second);
        const char *wd = (now.weekday >= 1 && now.weekday <= 7) ? WEEKDAYS[now.weekday - 1] : "---";
        snprintf(s.date, sizeof(s.date), "%s %02u.%02u.%04u", wd, now.day, now.month, now.year);
    }
    else
    {
        strcpy(s.time, "--:--:--");
        strcpy(s.date, "--- --.--.----");
    }

    formatClimate(s.climate, sizeof(s.climate), "\xB0");
    if (bmeOk)
        snprintf(s.pressure, sizeof(s.pressure), "P: %.0f hPa", climate.pressure);
    else
        strcpy(s.pressure, "P: ---- hPa");
}

static void updateDisplay(const Screen &s)
{
    oled::clear();
    drawCentered(0, s.time, 2); // великий час: 16 px заввишки
    drawCentered(20, s.date, 1);
    drawCentered(36, s.climate, 1);
    drawCentered(50, s.pressure, 1);
    oled::flush();
}

// Дублюємо в лог усе, що зараз на екрані
static void logScreen(const Screen &s)
{
    char climateUtf8[24];
    formatClimate(climateUtf8, sizeof(climateUtf8), "°");
    ESP_LOGI(TAG, "%s %s | %s | %s", s.time, s.date, climateUtf8, s.pressure);
}

void setup()
{
    Serial.begin(115200);
    initI2C();
    initDisplay();
    initRtc();
    initBme();
}

void loop()
{
    handleSerial();

    uint32_t t = millis();
    bool polled = false;

    if (t - lastRtcPollAt >= RTC_POLL_MS)
    {
        lastRtcPollAt = t;
        rtcOk = readTime(now);
        polled = true;
    }

    if (t - lastBmePollAt >= BME_POLL_MS)
    {
        lastBmePollAt = t;
        bool ok = readClimate(climate);
        if (ok != bmeOk)
        {
            if (ok)
                ESP_LOGI(TAG, "BME280 на зв'язку");
            else
                ESP_LOGE(TAG, "BME280: помилка читання");
        }
        bmeOk = ok;
        polled = true;
    }

    if (!polled)
        return;

    Screen s;
    composeScreen(s);
    if (!forceRedraw && s == shown)
        return; // на екрані нічого не змінилось

    forceRedraw = false;
    shown = s;
    updateDisplay(s);
    logScreen(s);
}
