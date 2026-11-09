/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
/* BME280 compensation, the 32-bit integer formulas of the Bosch BME280
 * datasheet (BST-BME280-DS002, 4.2.3 and 8.2): temperature in 0.01 C,
 * pressure in Pa, humidity in 1/1024 %RH. Shared by the firmware and the
 * host test. */
#ifndef BME280_MATH_H
#define BME280_MATH_H

#include <stdint.h>

typedef struct {
    uint16_t t1; int16_t t2, t3;
    uint16_t p1; int16_t p2, p3, p4, p5, p6, p7, p8, p9;
    uint8_t h1, h3; int16_t h2, h4, h5; int8_t h6;
} bme280_calib_t;

#define BME280_U16(p, i) ((uint16_t)((uint16_t)(p)[(i)+1] << 8 | (p)[i]))

/* Calibration from registers 0x88..0xA1 (26 bytes). */
__attribute__((unused)) static void bme280_calib(bme280_calib_t *c, const uint8_t *a)
{
    c->t1 = BME280_U16(a, 0); c->t2 = (int16_t)BME280_U16(a, 2); c->t3 = (int16_t)BME280_U16(a, 4);
    c->p1 = BME280_U16(a, 6); c->p2 = (int16_t)BME280_U16(a, 8); c->p3 = (int16_t)BME280_U16(a, 10);
    c->p4 = (int16_t)BME280_U16(a, 12); c->p5 = (int16_t)BME280_U16(a, 14); c->p6 = (int16_t)BME280_U16(a, 16);
    c->p7 = (int16_t)BME280_U16(a, 18); c->p8 = (int16_t)BME280_U16(a, 20); c->p9 = (int16_t)BME280_U16(a, 22);
    c->h1 = a[25];
}

/* Humidity calibration from registers 0xE1..0xE7 (7 bytes). */
__attribute__((unused)) static void bme280_calib_humidity(bme280_calib_t *c, const uint8_t *b)
{
    c->h2 = (int16_t)BME280_U16(b, 0); c->h3 = b[2];
    c->h4 = (int16_t)((int16_t)(int8_t)b[3] * 16 | (b[4] & 0x0f));
    c->h5 = (int16_t)((int16_t)(int8_t)b[5] * 16 | (b[4] >> 4));
    c->h6 = (int8_t)b[6];
}

/* Returns t_fine; *centi gets 0.01 C. */
__attribute__((unused)) static int32_t bme280_temperature(const bme280_calib_t *c, int32_t adc, int16_t *centi)
{
    int32_t v1, v2, d;
    v1 = (((adc >> 3) - (int32_t)c->t1 * 2) * (int32_t)c->t2) >> 11;
    d = (adc >> 4) - (int32_t)c->t1;
    v2 = (((d * d) >> 12) * (int32_t)c->t3) >> 14;
    *centi = (int16_t)(((v1 + v2) * 5 + 128) >> 8);
    return v1 + v2;
}

__attribute__((unused)) static uint32_t bme280_pressure(const bme280_calib_t *c, int32_t adc, int32_t fine)
{
    int32_t v1, v2;
    uint32_t p;
    v1 = (fine >> 1) - 64000L;
    v2 = (((v1 >> 2) * (v1 >> 2)) >> 11) * (int32_t)c->p6;
    v2 = v2 + v1 * (int32_t)c->p5 * 2;
    v2 = (v2 >> 2) + (int32_t)c->p4 * 65536L;
    v1 = ((((int32_t)c->p3 * (((v1 >> 2) * (v1 >> 2)) >> 13)) >> 3) + (((int32_t)c->p2 * v1) >> 1)) >> 18;
    v1 = ((32768L + v1) * (int32_t)c->p1) >> 15;
    if (!v1) return 0;
    p = ((uint32_t)(1048576L - adc) - (uint32_t)(v2 >> 12)) * 3125u;
    if (p < 0x80000000UL) p = (p << 1) / (uint32_t)v1;
    else p = (p / (uint32_t)v1) * 2u;
    v1 = ((int32_t)c->p9 * (int32_t)(((p >> 3) * (p >> 3)) >> 13)) >> 12;
    v2 = ((int32_t)(p >> 2) * (int32_t)c->p8) >> 13;
    return (uint32_t)((int32_t)p + ((v1 + v2 + c->p7) >> 4));
}

__attribute__((unused)) static uint32_t bme280_humidity(const bme280_calib_t *c, int32_t adc, int32_t fine)
{
    int32_t v = fine - 76800L;
    v = (((((adc * 16384L) - (int32_t)c->h4 * 1048576L - ((int32_t)c->h5 * v)) + 16384L) >> 15) *
         (((((((v * (int32_t)c->h6) >> 10) * (((v * (int32_t)c->h3) >> 11) + 32768L)) >> 10) +
            2097152L) * (int32_t)c->h2 + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * (int32_t)c->h1) >> 4);
    if (v < 0) v = 0;
    if (v > 419430400L) v = 419430400L;
    return (uint32_t)(v >> 12);
}

/* Maxim 1-Wire CRC8 (X^8+X^5+X^4+1, reflected 0x8C). */
__attribute__((unused)) static uint8_t onewire_crc8(const uint8_t *d, uint8_t n)
{
    uint8_t crc = 0, i, b;
    while (n--) {
        b = *d++;
        for (i = 0; i < 8u; i++) {
            crc = ((crc ^ b) & 1u) ? (uint8_t)((crc >> 1) ^ 0x8cu) : (uint8_t)(crc >> 1);
            b >>= 1;
        }
    }
    return crc;
}

#endif
