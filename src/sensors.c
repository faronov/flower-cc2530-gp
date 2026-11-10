/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "sensors.h"
#include "bme280_math.h"

#define T1_DIV8_FREE 0x05u
#define I2C_HALF 5u          /* us, about 100 kHz */
#define I2C_STRETCH 2000u    /* us an SCL release may take */
#define BME280_ID 0x60u
#define BME280_REG_ID 0xd0u
#define BME280_REG_STATUS 0xf3u
#define BME280_REG_DATA 0xf7u
#define OW_SKIP_ROM 0xccu
#define OW_CONVERT 0x44u
#define OW_READ 0xbeu
#define OW_WRITE 0x4eu
/* Volatile alarm bytes: a DS18B20 that lost power returns its EEPROM
 * values instead, so stale (85 C power-on) data is rejected. */
#define DS_TH 0x00u
#define DS_TL 0x64u
/* SWRU191F 7.18 P2INP.PDUP0: port 0 pull-down. */
#define PDUP0 0x20u
/* One expansion level so BOARD_ONEWIRE_PORT becomes the register name
 * before MMIO_READ pastes it. */
#define PORT_READ(reg) MMIO_READ(reg)
#define OW_LINE() (PORT_READ(BOARD_ONEWIRE_PORT) & BOARD_FLOWER_ONEWIRE)

#if BOARD_BME280
static MCU_XDATA uint8_t bme_addr, bme_valid, buf[26];
static MCU_XDATA bme280_calib_t calib;
#else
static MCU_XDATA uint8_t buf[9];
#endif

static uint16_t ticks(void)
{
    uint8_t low = MMIO_READ(SOC_T1CNTL);
    return (uint16_t)MMIO_READ(SOC_T1CNTH) << 8 | low;
}

/* Busy until us microseconds after start (Timer 1 at 2 MHz). */
static void until(uint16_t start, uint16_t us)
{
    while ((uint16_t)(ticks() - start) < (uint16_t)(us << 1)) {}
}

static void wait_us(uint16_t us)
{
    until(ticks(), us);
}

#if BOARD_BME280
void sensors_bus(uint8_t on)
{
    MMIO_CLEAR(SOC_P0DIR, BOARD_FLOWER_SCL | BOARD_FLOWER_SDA);
    MMIO_CLEAR(SOC_P1DIR, BOARD_FLOWER_ONEWIRE);
    if (on) {
        MMIO_CLEAR(SOC_P0INP, BOARD_FLOWER_SCL | BOARD_FLOWER_SDA);
        MMIO_CLEAR(SOC_P1INP, BOARD_FLOWER_ONEWIRE);
        MMIO_WRITE(SOC_T1CTL, T1_DIV8_FREE);
    } else {
        MMIO_WRITE(SOC_T1CTL, 0);
        MMIO_SET(SOC_P0INP, BOARD_FLOWER_SCL | BOARD_FLOWER_SDA);
        MMIO_SET(SOC_P1INP, BOARD_FLOWER_ONEWIRE);
    }
}
#else
/* The 1-Wire line on the port 0 pulls: up while measuring, down otherwise
 * (the vendor firmware's sleep state of port 0). */
void sensors_bus(uint8_t on)
{
    MMIO_CLEAR(SOC_P0DIR, BOARD_FLOWER_ONEWIRE);
    if (on) {
        MMIO_CLEAR(SOC_P2INP, PDUP0);
        MMIO_WRITE(SOC_T1CTL, T1_DIV8_FREE);
    } else {
        MMIO_WRITE(SOC_T1CTL, 0);
        MMIO_SET(SOC_P2INP, PDUP0);
    }
}
#endif

#if BOARD_BME280
/* Open drain: low drives the cleared latch, high releases to the pull-up.
 * The latch is cleared before every drive (a read-modify-write of another
 * pin may have copied a released pin's high level into it). */
static void scl(uint8_t high)
{
    uint16_t start;
    if (!high) {
        MMIO_CLEAR(SOC_P0, BOARD_FLOWER_SCL);
        MMIO_SET(SOC_P0DIR, BOARD_FLOWER_SCL);
        return;
    }
    MMIO_CLEAR(SOC_P0DIR, BOARD_FLOWER_SCL);
    start = ticks();
    while (!(MMIO_READ(SOC_P0) & BOARD_FLOWER_SCL))
        if ((uint16_t)(ticks() - start) >= (uint16_t)(I2C_STRETCH << 1)) break;
}

static void sda(uint8_t high)
{
    if (high) {
        MMIO_CLEAR(SOC_P0DIR, BOARD_FLOWER_SDA);
    } else {
        MMIO_CLEAR(SOC_P0, BOARD_FLOWER_SDA);
        MMIO_SET(SOC_P0DIR, BOARD_FLOWER_SDA);
    }
}

static void i2c_start(void)
{
    sda(1); scl(1); wait_us(I2C_HALF);
    sda(0); wait_us(I2C_HALF);
    scl(0); wait_us(I2C_HALF);
}

static void i2c_stop(void)
{
    sda(0); wait_us(I2C_HALF);
    scl(1); wait_us(I2C_HALF);
    sda(1); wait_us(I2C_HALF);
}

/* One byte out; 1 when the device acknowledged. */
static uint8_t i2c_write(uint8_t b)
{
    uint8_t i, ack;
    for (i = 0; i < 8u; i++) {
        sda(b & 0x80u);
        b <<= 1;
        wait_us(I2C_HALF);
        scl(1); wait_us(I2C_HALF);
        scl(0);
    }
    sda(1); wait_us(I2C_HALF);
    scl(1); wait_us(I2C_HALF);
    ack = !(MMIO_READ(SOC_P0) & BOARD_FLOWER_SDA);
    scl(0);
    return ack;
}

static uint8_t i2c_read(uint8_t ack)
{
    uint8_t i, b = 0;
    sda(1);
    for (i = 0; i < 8u; i++) {
        wait_us(I2C_HALF);
        scl(1); wait_us(I2C_HALF);
        b = (uint8_t)(b << 1 | ((MMIO_READ(SOC_P0) & BOARD_FLOWER_SDA) ? 1u : 0u));
        scl(0);
    }
    sda(!ack); wait_us(I2C_HALF);
    scl(1); wait_us(I2C_HALF);
    scl(0); sda(1);
    return b;
}

static uint8_t bme_read(uint8_t addr, uint8_t reg, uint8_t MCU_XDATA *d, uint8_t n)
{
    uint8_t ok;
    i2c_start();
    ok = i2c_write((uint8_t)(addr << 1)) && i2c_write(reg);
    if (ok) {
        i2c_start();
        ok = i2c_write((uint8_t)(addr << 1 | 1u));
        while (ok && n) {
            n--;
            *d++ = i2c_read(n != 0);
        }
    }
    i2c_stop();
    return ok;
}

static uint8_t bme_write(uint8_t reg, uint8_t value)
{
    uint8_t ok;
    i2c_start();
    ok = i2c_write((uint8_t)(bme_addr << 1)) && i2c_write(reg) && i2c_write(value);
    i2c_stop();
    return ok;
}
#endif

/* 1-Wire at standard speed; each slot is timed from its falling edge. */
static void ow_low(void)
{
    MMIO_CLEAR(BOARD_ONEWIRE_PORT, BOARD_FLOWER_ONEWIRE);
    MMIO_SET(BOARD_ONEWIRE_DIR, BOARD_FLOWER_ONEWIRE);
}

static void ow_release(void)
{
    MMIO_CLEAR(BOARD_ONEWIRE_DIR, BOARD_FLOWER_ONEWIRE);
}

/* Reset; 1 when a device answered with a presence pulse. */
static uint8_t ow_reset(void)
{
    uint16_t start;
    uint8_t present;
    ow_low();
    wait_us(480);
    start = ticks();
    ow_release();
    until(start, 70);
    present = !OW_LINE();
    until(start, 480);
    return present && OW_LINE();
}

static uint8_t ow_bit(uint8_t b)
{
    uint16_t start = ticks();
    ow_low();
    until(start, b ? 2u : 62u);
    ow_release();
    if (b) {
        until(start, 12);
        b = OW_LINE() ? 1u : 0u;
    }
    until(start, 70);
    return b;
}

static uint8_t ow_byte(uint8_t b)
{
    uint8_t i, r = 0;
    for (i = 0; i < 8u; i++) {
        r = (uint8_t)(r >> 1 | (ow_bit(b & 1u) ? 0x80u : 0u));
        b >>= 1;
    }
    return r;
}

uint8_t sensors_detect(void)
{
    uint8_t found = 0;
#if BOARD_BME280
    for (bme_addr = 0x76u; bme_addr <= 0x77u; bme_addr++)
        if (bme_read(bme_addr, BME280_REG_ID, buf, 1) && buf[0] == BME280_ID) {
            found |= SENSOR_BME280;
            break;
        }
#endif
    if (ow_reset()) found |= SENSOR_DS18B20;
    ow_release();
    return found;
}

#if BOARD_BME280
/* Forced mode, 1x oversampling of humidity, temperature and pressure,
 * filter off (datasheet 3.5.1 weather monitoring): at most 9.3 ms. */
uint8_t bme280_start(void)
{
    uint8_t a;
    if (!bme_valid) {
        for (a = 0x76u; a <= 0x77u; a++)
            if (bme_read(a, BME280_REG_ID, buf, 1) && buf[0] == BME280_ID) break;
        if (a > 0x77u) return 0;
        bme_addr = a;
        if (!bme_read(bme_addr, 0x88u, buf, 26)) return 0;
        bme280_calib(&calib, buf);
        if (!bme_read(bme_addr, 0xe1u, buf, 7)) return 0;
        bme280_calib_humidity(&calib, buf);
        bme_valid = 1;
    }
    if (bme_write(0xf2u, 0x01u) && bme_write(0xf4u, 0x25u)) return 1;
    bme_valid = 0;
    return 0;
}

void bme280_read(gpd_values_t MCU_XDATA *v)
{
    int32_t fine, t, p;
    uint32_t pa, rh;
    int16_t centi;
    uint8_t polls;
    v->humidity = FLOWER_INVALID_U16;
    v->pressure = (int16_t)FLOWER_INVALID_S16;
    v->temperature = (int16_t)FLOWER_INVALID_S16;
    for (polls = 0; polls < 20u; polls++) {
        if (!bme_read(bme_addr, BME280_REG_STATUS, buf, 1)) { bme_valid = 0; return; }
        if (!(buf[0] & 0x09u)) break;
        wait_us(1000);
    }
    if (polls == 20u || !bme_read(bme_addr, BME280_REG_DATA, buf, 8)) { bme_valid = 0; return; }
    p = (int32_t)((uint32_t)buf[0] << 12 | (uint16_t)buf[1] << 4 | buf[2] >> 4);
    t = (int32_t)((uint32_t)buf[3] << 12 | (uint16_t)buf[4] << 4 | buf[5] >> 4);
    /* 0x80000 / 0x8000: a skipped (reset) measurement. */
    if (t == 0x80000L) { bme_valid = 0; return; }
    fine = bme280_temperature(&calib, t, &centi);
    v->temperature = centi;
    if (p != 0x80000L) {
        pa = bme280_pressure(&calib, p, fine);
        if (pa) v->pressure = (int16_t)((pa + 50u) / 100u);
    }
    if (buf[6] != 0x80u || buf[7]) {
        rh = bme280_humidity(&calib, (int32_t)((uint16_t)buf[6] << 8 | buf[7]), fine);
        v->humidity = (uint16_t)((rh * 100u + 512u) >> 10);
    }
}
#endif

uint8_t ds18b20_start(void)
{
    uint8_t busy;
    if (!ow_reset()) return 0;
    ow_byte(OW_SKIP_ROM); ow_byte(OW_WRITE);
    ow_byte(DS_TH); ow_byte(DS_TL); ow_byte(DS18B20_CONFIG);
    if (!ow_reset()) return 0;
    ow_byte(OW_SKIP_ROM); ow_byte(OW_CONVERT);
    /* An externally powered DS18B20 reads 0 while converting. */
    busy = !ow_bit(1);
    ow_release();
    return busy;
}

void ds18b20_read(gpd_values_t MCU_XDATA *v)
{
    uint8_t i;
    int16_t raw;
    v->soil_temperature = (int16_t)FLOWER_INVALID_S16;
    if (!ow_reset()) return;
    ow_byte(OW_SKIP_ROM); ow_byte(OW_READ);
    for (i = 0; i < 9u; i++) buf[i] = ow_byte(0xffu);
    ow_release();
    if (onewire_crc8(buf, 8) != buf[8] || buf[2] != DS_TH || buf[3] != DS_TL ||
        buf[4] != DS18B20_CONFIG)
        return;
    raw = (int16_t)((uint16_t)buf[1] << 8 | (buf[0] & DS18B20_MASK));
    if (raw < -55*16 || raw > 125*16) return;
    v->soil_temperature = (int16_t)(((int32_t)raw * 25) / 4);
}
