#include "bme280.h"
#include <SPI.h>

namespace bme
{

    // ── Регістри ─────────────────────────────────────────────────
    constexpr uint8_t REG_CALIB_TP = 0x88; // 0x88..0xA1: T1..T3, P1..P9, H1
    constexpr uint8_t REG_CHIP_ID = 0xD0;
    constexpr uint8_t REG_RESET = 0xE0;
    constexpr uint8_t REG_CALIB_H = 0xE1; // 0xE1..0xE7: H2..H6
    constexpr uint8_t REG_CTRL_HUM = 0xF2;
    constexpr uint8_t REG_STATUS = 0xF3;
    constexpr uint8_t REG_CTRL_MEAS = 0xF4;
    constexpr uint8_t REG_CONFIG = 0xF5;
    constexpr uint8_t REG_DATA = 0xF7; // 0xF7..0xFE: P[3], T[3], H[2]

    constexpr uint8_t RESET_CMD = 0xB6;
    constexpr uint8_t STATUS_IM_UPDATE = 0x01; // копіювання NVM -> регістри калібрування

    // osrs_h = ×1
    constexpr uint8_t CTRL_HUM = 0x01;
    // t_sb = 125 мс (010), filter = off (000)
    constexpr uint8_t CONFIG = (0b010 << 5) | (0b000 << 2);
    // osrs_t = ×1, osrs_p = ×1, mode = normal (11)
    constexpr uint8_t CTRL_MEAS = (0b001 << 5) | (0b001 << 2) | 0b11;

    // BME280 підтримує SPI mode 0 і 3, до 10 МГц
    static const SPISettings SPI_CFG(1000000, MSBFIRST, SPI_MODE0);

    static uint8_t cs = 0xFF;

    struct Calib
    {
        uint16_t T1;
        int16_t T2, T3;
        uint16_t P1;
        int16_t P2, P3, P4, P5, P6, P7, P8, P9;
        uint8_t H1, H3;
        int16_t H2, H4, H5;
        int8_t H6;
    };
    static Calib cal{};

    static void readRegs(uint8_t reg, uint8_t *buf, size_t len)
    {
        SPI.beginTransaction(SPI_CFG);
        digitalWrite(cs, LOW);
        SPI.transfer(reg | 0x80); // біт 7 = 1 -> читання, далі автоінкремент адреси
        for (size_t i = 0; i < len; i++)
            buf[i] = SPI.transfer(0x00);
        digitalWrite(cs, HIGH);
        SPI.endTransaction();
    }

    static uint8_t readReg(uint8_t reg)
    {
        uint8_t v;
        readRegs(reg, &v, 1);
        return v;
    }

    static void writeReg(uint8_t reg, uint8_t value)
    {
        SPI.beginTransaction(SPI_CFG);
        digitalWrite(cs, LOW);
        SPI.transfer(reg & 0x7F); // біт 7 = 0 -> запис
        SPI.transfer(value);
        digitalWrite(cs, HIGH);
        SPI.endTransaction();
    }

    static uint16_t u16le(const uint8_t *p) { return p[0] | (p[1] << 8); }
    static int16_t s16le(const uint8_t *p) { return (int16_t)u16le(p); }

    static void readCalibration()
    {
        uint8_t tp[26];
        readRegs(REG_CALIB_TP, tp, sizeof(tp));
        cal.T1 = u16le(&tp[0]);
        cal.T2 = s16le(&tp[2]);
        cal.T3 = s16le(&tp[4]);
        cal.P1 = u16le(&tp[6]);
        cal.P2 = s16le(&tp[8]);
        cal.P3 = s16le(&tp[10]);
        cal.P4 = s16le(&tp[12]);
        cal.P5 = s16le(&tp[14]);
        cal.P6 = s16le(&tp[16]);
        cal.P7 = s16le(&tp[18]);
        cal.P8 = s16le(&tp[20]);
        cal.P9 = s16le(&tp[22]);
        cal.H1 = tp[25]; // 0xA1 (0xA0 не використовується)

        uint8_t h[7];
        readRegs(REG_CALIB_H, h, sizeof(h));
        cal.H2 = s16le(&h[0]);
        cal.H3 = h[2];
        cal.H4 = (int16_t)((int8_t)h[3] * 16 | (h[4] & 0x0F)); // 0xE4[7:0] / 0xE5[3:0]
        cal.H5 = (int16_t)((int8_t)h[5] * 16 | (h[4] >> 4));   // 0xE6[7:0] / 0xE5[7:4]
        cal.H6 = (int8_t)h[6];
    }

    bool init(uint8_t csPin)
    {
        cs = csPin;
        pinMode(cs, OUTPUT);
        digitalWrite(cs, HIGH);

        if (readReg(REG_CHIP_ID) != CHIP_ID)
            return false;

        writeReg(REG_RESET, RESET_CMD);
        // Після reset чип ~2 мс копіює калібрування з NVM. Це лише в init(),
        // тому коротке очікування тут не блокує основний цикл.
        uint32_t start = millis();
        do
            delay(2);
        while ((readReg(REG_STATUS) & STATUS_IM_UPDATE) && millis() - start < 50);

        readCalibration();

        // ctrl_hum діє лише після запису в ctrl_meas; config — поки чип у sleep
        writeReg(REG_CTRL_HUM, CTRL_HUM);
        writeReg(REG_CONFIG, CONFIG);
        writeReg(REG_CTRL_MEAS, CTRL_MEAS);
        return readReg(REG_CTRL_MEAS) == CTRL_MEAS;
    }

    // ── Компенсація (даташит BME280, 4.2.3) ─────────────────────
    // t_fine — проміжне значення температури, потрібне для P і H
    static int32_t compensateT(int32_t adc, int32_t &tFine)
    {
        int32_t var1 = ((((adc >> 3) - ((int32_t)cal.T1 << 1))) * cal.T2) >> 11;
        int32_t var2 = (((((adc >> 4) - cal.T1) * ((adc >> 4) - cal.T1)) >> 12) * cal.T3) >> 14;
        tFine = var1 + var2;
        return (tFine * 5 + 128) >> 8; // 0.01 °C
    }

    static uint32_t compensateP(int32_t adc, int32_t tFine)
    {
        int64_t var1 = (int64_t)tFine - 128000;
        int64_t var2 = var1 * var1 * cal.P6;
        var2 += (var1 * cal.P5) << 17;
        var2 += (int64_t)cal.P4 << 35;
        var1 = ((var1 * var1 * cal.P3) >> 8) + ((var1 * cal.P2) << 12);
        var1 = ((((int64_t)1 << 47) + var1) * cal.P1) >> 33;
        if (var1 == 0)
            return 0; // захист від ділення на 0
        int64_t p = 1048576 - adc;
        p = (((p << 31) - var2) * 3125) / var1;
        var1 = ((int64_t)cal.P9 * (p >> 13) * (p >> 13)) >> 25;
        var2 = ((int64_t)cal.P8 * p) >> 19;
        p = ((p + var1 + var2) >> 8) + ((int64_t)cal.P7 << 4);
        return (uint32_t)p; // Па у форматі Q24.8
    }

    static uint32_t compensateH(int32_t adc, int32_t tFine)
    {
        int32_t v = tFine - 76800;
        v = (((((adc << 14) - ((int32_t)cal.H4 << 20) - ((int32_t)cal.H5 * v)) + 16384) >> 15) *
             (((((((v * cal.H6) >> 10) * (((v * (int32_t)cal.H3) >> 11) + 32768)) >> 10) + 2097152) *
                   cal.H2 +
               8192) >>
              14));
        v -= ((((v >> 15) * (v >> 15)) >> 7) * (int32_t)cal.H1) >> 4;
        v = constrain(v, 0, 419430400);
        return (uint32_t)(v >> 12); // %RH у форматі Q22.10
    }

    bool read(BmeData &out)
    {
        if (cs == 0xFF)
            return false;

        // Одне burst-читання 0xF7..0xFE — усі три величини з одного циклу вимірювання
        uint8_t d[8];
        readRegs(REG_DATA, d, sizeof(d));
        int32_t adcP = ((uint32_t)d[0] << 12) | (d[1] << 4) | (d[2] >> 4);
        int32_t adcT = ((uint32_t)d[3] << 12) | (d[4] << 4) | (d[5] >> 4);
        int32_t adcH = (d[6] << 8) | d[7];

        // 0x80000 / 0x8000 — значення "вимірювання пропущене" (або чип відключено)
        if (adcT == 0x80000 || adcP == 0x80000 || adcH == 0x8000)
            return false;
        // Від'єднаний чип: MISO висить у 0 або 1 — перевіряємо ID
        if (readReg(REG_CHIP_ID) != CHIP_ID)
            return false;

        int32_t tFine;
        out.temperature = compensateT(adcT, tFine) / 100.0f;
        out.pressure = compensateP(adcP, tFine) / 256.0f / 100.0f; // Па -> гПа
        out.humidity = compensateH(adcH, tFine) / 1024.0f;
        return true;
    }

} // namespace bme
