#pragma once
#include <Arduino.h>

struct BmeData
{
    float temperature; // °C
    float humidity;    // %RH
    float pressure;    // hPa
};

// Драйвер BME280 по SPI (без сторонніх бібліотек).
// Читання регістра: адреса | 0x80, запис: адреса & 0x7F.
// Компенсація — цілочисельні формули з даташиту Bosch (розділ 4.2.3, 8.2).
namespace bme
{

    constexpr uint8_t CHIP_ID = 0x60; // у BMP280 = 0x58 (без вологості)

    // CS-пін і вже ініціалізований SPI (SPI.begin(...))
    bool init(uint8_t csPin);
    bool read(BmeData &out); // false, якщо чип не відповідає

} // namespace bme
