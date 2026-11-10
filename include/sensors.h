/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef FLOWER_SENSORS_H
#define FLOWER_SENSORS_H
/* Optional sensors on the switched rail: BME280 on a bit-banged I2C bus
 * (SCL P0.5, SDA P0.6, open drain through the direction register) and a
 * DS18B20 on 1-Wire (P1.3); on BGT1 only the DS18B20 (P0.3, supplied from
 * P0.2). Timer 1 counts 2 MHz (16-MHz tick / 8) for the bus timing while a
 * sensor call runs; interrupts are off. */
#include "flower.h"

#define SENSOR_BME280 GPD_HAS_BME280
#define SENSOR_DS18B20 GPD_HAS_DS18B20
/* DS18B20 resolution: configuration register, undefined low bits of the
 * result, and conversion time (datasheet tCONV: 187.5 ms at 10 bits,
 * 375 ms at 11). */
#if BOARD_DS18B20_BITS == 11
#define DS18B20_CONFIG 0x5fu
#define DS18B20_MASK 0xfeu
#define DS18B20_CONVERSION 13107UL  /* Sleep Timer ticks, 400 ms */
#elif BOARD_DS18B20_BITS == 10
#define DS18B20_CONFIG 0x3fu
#define DS18B20_MASK 0xfcu
#define DS18B20_CONVERSION 6554UL   /* Sleep Timer ticks, 200 ms */
#else
#error "BOARD_DS18B20_BITS must be 10 or 11"
#endif

/* Rail on and warmed up: which sensors answer. */
uint8_t sensors_detect(void);
#if BOARD_BME280
/* Starts a BME280 forced measurement (and reads its calibration once). */
uint8_t bme280_start(void);
/* Reads the finished BME280 measurement into v; invalid values on error. */
void bme280_read(gpd_values_t MCU_XDATA *v);
#endif
/* Configures BOARD_DS18B20_BITS and starts a DS18B20 conversion; 0 on error. */
uint8_t ds18b20_start(void);
/* Reads the conversion into v->soil_temperature (invalid on error). */
void ds18b20_read(gpd_values_t MCU_XDATA *v);
/* Sensor bus pins: pull-ups with the rail on, no pulls with it off. */
void sensors_bus(uint8_t on);

#endif
