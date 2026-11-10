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
/* DS18B20 10-bit conversion: at most 187.5 ms (datasheet tCONV). */
#define DS18B20_CONVERSION 6554UL   /* Sleep Timer ticks, 200 ms */

/* Rail on and warmed up: which sensors answer. */
uint8_t sensors_detect(void);
#if BOARD_BME280
/* Starts a BME280 forced measurement (and reads its calibration once). */
uint8_t bme280_start(void);
/* Reads the finished BME280 measurement into v; invalid values on error. */
void bme280_read(gpd_values_t MCU_XDATA *v);
#endif
/* Configures 10 bits and starts a DS18B20 conversion; 0 on error. */
uint8_t ds18b20_start(void);
/* Reads the conversion into v->soil_temperature (invalid on error). */
void ds18b20_read(gpd_values_t MCU_XDATA *v);
/* Sensor bus pins: pull-ups with the rail on, no pulls with it off. */
void sensors_bus(uint8_t on);

#endif
