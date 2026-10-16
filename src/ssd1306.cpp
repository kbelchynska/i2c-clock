#include "ssd1306.h"
#include <Wire.h>
#include "font5x7.h"

namespace oled
{

    // ── I²C control byte ─────────────────────────────────────────
    constexpr uint8_t CTRL_CMD = 0x00;  // далі потік команд
    constexpr uint8_t CTRL_DATA = 0x40; // далі потік даних у GDDRAM

    constexpr uint8_t PAGES = HEIGHT / 8;
    constexpr size_t DATA_CHUNK = 16; // байтів даних в одній I²C-транзакції

    static uint8_t buffer[WIDTH * PAGES];

    static bool sendCommands(const uint8_t *cmds, size_t len)
    {
        Wire.beginTransmission(ADDR);
        Wire.write(CTRL_CMD);
        Wire.write(cmds, len);
        return Wire.endTransmission() == 0;
    }

    bool init()
    {
        static const uint8_t INIT_SEQ[] = {
            0xAE, // Display OFF
            0xD5,
            0x80, // clock divide / oscillator
            0xA8,
            0x3F, // multiplex ratio: 64 рядки
            0xD3,
            0x00, // display offset: 0
            0x40, // start line: 0
            0x8D,
            0x14, // charge pump: увімкнено
            0x20,
            0x00, // addressing mode: горизонтальний
            0xA1, // segment remap: колонка 127 -> SEG0
            0xC8, // COM scan direction: COM[N-1] -> COM0
            0xDA,
            0x12, // COM pins config для 128×64
            0x81,
            0xCF, // contrast
            0xD9,
            0xF1, // pre-charge (внутрішній charge pump)
            0xDB,
            0x40, // VCOMH deselect level
            0xA4, // показувати вміст RAM
            0xA6, // звичайний (не інвертований) режим
            0x2E, // вимкнути scroll
            0xAF, // Display ON
        };
        if (!sendCommands(INIT_SEQ, sizeof(INIT_SEQ)))
            return false;
        delay(100);
        clear();
        return flush();
    }

    void clear()
    {
        memset(buffer, 0, sizeof(buffer));
    }

    static void setPixel(int x, int y)
    {
        if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
            return;
        buffer[(y / 8) * WIDTH + x] |= 1 << (y % 8);
    }

    static void drawChar(int x, int y, char c, uint8_t scale)
    {
        const uint8_t *cols = font::glyph(c);
        for (int col = 0; col < font::CHAR_W; col++)
        {
            for (int row = 0; row < font::CHAR_H; row++)
            {
                if (!(cols[col] & (1 << row)))
                    continue;
                for (int dx = 0; dx < scale; dx++)
                    for (int dy = 0; dy < scale; dy++)
                        setPixel(x + col * scale + dx, y + row * scale + dy);
            }
        }
    }

    void drawText(int x, int y, const char *text, uint8_t scale)
    {
        for (; *text; text++, x += (font::CHAR_W + 1) * scale)
            drawChar(x, y, *text, scale);
    }

    int textWidth(const char *text, uint8_t scale)
    {
        size_t n = strlen(text);
        return n ? (n * (font::CHAR_W + 1) - 1) * scale : 0;
    }

    bool flush()
    {
        static const uint8_t WINDOW[] = {
            0x21,
            0,
            WIDTH - 1, // column address: 0..127
            0x22,
            0,
            PAGES - 1, // page address: 0..7
        };
        if (!sendCommands(WINDOW, sizeof(WINDOW)))
            return false;

        // Адреса в GDDRAM інкрементується сама (горизонтальний режим),
        // тому буфер шлемо частинами, кожну з control byte 0x40.
        for (size_t i = 0; i < sizeof(buffer); i += DATA_CHUNK)
        {
            Wire.beginTransmission(ADDR);
            Wire.write(CTRL_DATA);
            Wire.write(buffer + i, DATA_CHUNK);
            if (Wire.endTransmission() != 0)
                return false;
        }
        return true;
    }

} // namespace oled
