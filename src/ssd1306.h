#pragma once
#include <Arduino.h>

// Мінімальний драйвер OLED SSD1306 128×64 по I²C (без сторонніх бібліотек).
// Малювання йде у буфер у RAM, на дисплей він передається викликом flush().
namespace oled {

constexpr uint8_t ADDR   = 0x3C;
constexpr int     WIDTH  = 128;
constexpr int     HEIGHT = 64;

bool init();                       // послідовність команд ініціалізації
void clear();                      // очистити буфер
void drawText(int x, int y, const char *text, uint8_t scale = 1);
int  textWidth(const char *text, uint8_t scale = 1);
bool flush();                      // передати буфер (1024 байти) у GDDRAM

}  // namespace oled
