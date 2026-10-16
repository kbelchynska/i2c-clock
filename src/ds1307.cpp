#include "ds1307.h"
#include <Wire.h>

namespace rtc
{

    //  Регістри DS1307
    constexpr uint8_t REG_SECONDS = 0x00; // біт 7 = CH (clock halt)
    constexpr uint8_t TIME_REGS = 7;      // seconds, minutes, hours, weekday, date, month, year
    constexpr uint8_t CH_BIT = 0x80;
    constexpr uint8_t HOUR_12H = 0x40; // біт 6 регістра годин: 12-годинний режим
    constexpr uint8_t HOUR_PM = 0x20;

    static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
    static uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

    static bool readRegs(uint8_t reg, uint8_t *out, uint8_t len)
    {
        Wire.beginTransmission(ADDR);
        Wire.write(reg); // встановити вказівник регістра
        if (Wire.endTransmission() != 0)
            return false;
        if (Wire.requestFrom(ADDR, len) != len)
            return false;
        for (uint8_t i = 0; i < len; i++)
            out[i] = Wire.read();
        return true;
    }

    bool isRunning()
    {
        uint8_t sec;
        return readRegs(REG_SECONDS, &sec, 1) && !(sec & CH_BIT);
    }

    bool read(DateTime &dt)
    {
        uint8_t r[TIME_REGS];
        if (!readRegs(REG_SECONDS, r, TIME_REGS))
            return false;

        dt.second = bcd2bin(r[0] & 0x7F);
        dt.minute = bcd2bin(r[1] & 0x7F);
        if (r[2] & HOUR_12H)
        {
            uint8_t h = bcd2bin(r[2] & 0x1F) % 12;
            dt.hour = (r[2] & HOUR_PM) ? h + 12 : h;
        }
        else
        {
            dt.hour = bcd2bin(r[2] & 0x3F);
        }
        dt.weekday = r[3] & 0x07;
        dt.day = bcd2bin(r[4] & 0x3F);
        dt.month = bcd2bin(r[5] & 0x1F);
        dt.year = 2000 + bcd2bin(r[6]);
        return true;
    }

    bool write(const DateTime &dt)
    {
        Wire.beginTransmission(ADDR);
        Wire.write(REG_SECONDS);
        Wire.write(bin2bcd(dt.second)); // CH = 0 -> осцилятор запущено
        Wire.write(bin2bcd(dt.minute));
        Wire.write(bin2bcd(dt.hour)); // 24-годинний режим
        Wire.write(weekdayOf(dt.year, dt.month, dt.day));
        Wire.write(bin2bcd(dt.day));
        Wire.write(bin2bcd(dt.month));
        Wire.write(bin2bcd(dt.year % 100));
        return Wire.endTransmission() == 0;
    }

    // Алгоритм Сакамото -> 0 = Sun .. 6 = Sat, переводимо у 1 = Mon .. 7 = Sun
    uint8_t weekdayOf(uint16_t year, uint8_t month, uint8_t day)
    {
        static const uint8_t T[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
        if (month < 3)
            year--;
        uint8_t w = (year + year / 4 - year / 100 + year / 400 + T[month - 1] + day) % 7;
        return w == 0 ? 7 : w;
    }

} // namespace rtc
