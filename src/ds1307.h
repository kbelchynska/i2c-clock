#pragma once
#include <Arduino.h>

struct DateTime {
    uint16_t year;     // 2000..2099
    uint8_t  month;    // 1..12
    uint8_t  day;      // 1..31
    uint8_t  weekday;  // 1 = Mon .. 7 = Sun
    uint8_t  hour;     // 0..23
    uint8_t  minute;
    uint8_t  second;
};

// Драйвер RTC DS1307 по I²C (регістри 0x00..0x06, формат BCD).
namespace rtc {

constexpr uint8_t ADDR = 0x68;

bool    isRunning();                    // false, якщо осцилятор зупинений (біт CH)
bool    read(DateTime &dt);
bool    write(const DateTime &dt);      // день тижня рахується автоматично
uint8_t weekdayOf(uint16_t year, uint8_t month, uint8_t day);

}  // namespace rtc
