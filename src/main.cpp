#include <Arduino.h>
#include <Wire.h>
#include "ds1307.h"
#include "ssd1306.h"

// ── Pins / I²C ───────────────────────────────────────────────
constexpr uint8_t  I2C_SDA  = 8;
constexpr uint8_t  I2C_SCL  = 9;
constexpr uint32_t I2C_FREQ = 100000;

constexpr uint32_t RTC_POLL_MS = 200;  // опитуємо RTC 5 разів на секунду

static const char *const WEEKDAYS[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

// ── State ────────────────────────────────────────────────────
DateTime now{};
bool     rtcOk       = false;
int      lastSecond  = -1;
uint32_t lastPollAt  = 0;

// Час компіляції ("Oct 12 2026", "15:33:59") -> DateTime
static DateTime compileTime() {
    static const char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char     mon[4];
    int      d, y, h, m, s;
    DateTime dt{};
    sscanf(__DATE__, "%3s %d %d", mon, &d, &y);
    sscanf(__TIME__, "%d:%d:%d", &h, &m, &s);
    dt.month  = (strstr(MONTHS, mon) - MONTHS) / 3 + 1;
    dt.day    = d;
    dt.year   = y;
    dt.hour   = h;
    dt.minute = m;
    dt.second = s;
    return dt;
}

// ── Ініціалізація ────────────────────────────────────────────
static void initI2C() {
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);
}

static void initDisplay() {
    if (!oled::init()) Serial.println("SSD1306 не відповідає на 0x3C");
}

static void initRtc() {
    if (!rtc::isRunning()) {
        Serial.println("DS1307 зупинений або не знайдений -> встановлюю час компіляції");
        rtc::write(compileTime());
    }
}

// ── Зчитування часу ──────────────────────────────────────────
static bool readTime(DateTime &dt) {
    return rtc::read(dt);
}

// Встановлення часу з Serial Monitor: "2026-02-14 15:33:59"
static void handleSerial() {
    if (!Serial.available()) return;
    String line = Serial.readStringUntil('\n');
    int y, mo, d, h, mi, s;
    if (sscanf(line.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6) {
        DateTime dt{(uint16_t)y, (uint8_t)mo, (uint8_t)d, 0, (uint8_t)h, (uint8_t)mi, (uint8_t)s};
        Serial.println(rtc::write(dt) ? "Час встановлено" : "Помилка запису в DS1307");
        lastSecond = -1;  // перемалювати одразу
    } else {
        Serial.println("Формат: YYYY-MM-DD HH:MM:SS");
    }
}

// ── Оновлення дисплея ────────────────────────────────────────
static void drawCentered(int y, const char *text, uint8_t scale) {
    oled::drawText((oled::WIDTH - oled::textWidth(text, scale)) / 2, y, text, scale);
}

static void updateDisplay(const DateTime &dt, bool valid) {
    char timeStr[9];   // HH:MM:SS
    char dateStr[15];  // EEE DD.MM.YYYY
    if (valid) {
        snprintf(timeStr, sizeof(timeStr), "%02u:%02u:%02u", dt.hour, dt.minute, dt.second);
        const char *wd = (dt.weekday >= 1 && dt.weekday <= 7) ? WEEKDAYS[dt.weekday - 1] : "---";
        snprintf(dateStr, sizeof(dateStr), "%s %02u.%02u.%04u", wd, dt.day, dt.month, dt.year);
    } else {
        strcpy(timeStr, "--:--:--");
        strcpy(dateStr, "--- --.--.----");
    }

    oled::clear();
    drawCentered(14, timeStr, 2);  // великий час: 16 px заввишки
    drawCentered(42, dateStr, 1);
    oled::flush();
}

void setup() {
    Serial.begin(115200);
    initI2C();
    initDisplay();
    initRtc();
}

void loop() {
    handleSerial();

    if (millis() - lastPollAt < RTC_POLL_MS) return;
    lastPollAt = millis();

    bool ok = readTime(now);
    if (ok == rtcOk && (!ok || now.second == lastSecond)) return;  // нічого не змінилось

    rtcOk      = ok;
    lastSecond = ok ? now.second : -1;
    updateDisplay(now, ok);
    if (ok) Serial.printf("%02u:%02u:%02u\n", now.hour, now.minute, now.second);
}
