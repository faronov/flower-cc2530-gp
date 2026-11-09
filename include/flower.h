/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef FLOWER_H
#define FLOWER_H
/* FlowerX board and measurement (the faronov/flower-cc2530 zcl_flower.c
 * sensor cycle without the ZCL server): battery, CC2530 die temperature,
 * illuminance and soil moisture. */
#include "board.h"
#include "gpd_frame.h"

#define FLOWER_ADC_POLLS 4000u
#define FLOWER_INVALID_U16 0xffffu
#define FLOWER_INVALID_S16 0x8000u
/* CC2530 single-ended 14-bit positive half range; illuminance at or above
 * FLOWER_LIGHT_OPEN is an unconnected input. */
#define FLOWER_ADC_MAX 8191u
#define FLOWER_LIGHT_OPEN 8000u
/* flower battery.c: mV = raw * 0.443 (VDD/3, internal 1.15 V reference). */
#define FLOWER_MILLIVOLTS(raw) ((uint16_t)(((uint32_t)(raw) * 443u) / 1000u))
/* flower zcl_app.h soil calibration against the battery raw ADC:
 * air 0.179*raw + 3926, water 0.146*raw + 2020. */
#define FLOWER_AIR(raw) ((uint16_t)(((uint32_t)(raw) * 179u + 3926000u) / 1000u))
#define FLOWER_WATER(raw) ((uint16_t)(((uint32_t)(raw) * 146u + 2020000u) / 1000u))

/* Die sensor (SWRS081B): 1480 12-bit counts at 25 C and 3 V, 4.5 per C and
 * 1 per 100 mV; as 14-bit averages 5920, 18 per C, 4 per 100 mV.
 * EXPERIMENTAL, undocumented by TI: the big-endian information-page word at
 * FLOWER_FACTORY is assumed to be this chip's 25 C / 3 V reading and
 * replaces the nominal when its 12 bits lie in 1380..1580. */
#define FLOWER_FACTORY 0x781eu
#define FLOWER_DIE_NOMINAL 5920
#define FLOWER_FACTORY_VALID(word) ((uint16_t)(word) >= (uint16_t)(1380u << 4) && \
    (uint16_t)(word) < (uint16_t)(1581u << 4))
#define FLOWER_DIE_REFERENCE(word) ((int16_t)(FLOWER_FACTORY_VALID(word) ? \
    (int16_t)((uint16_t)(word) >> 2) : FLOWER_DIE_NOMINAL))
/* centi-C = 2500 + (die - reference - (mV-3000)/25) * 100/18, 50/9 ~ 1422/256 */
#define FLOWER_DIE_CENTI(die, mv, word) ((int16_t)(2500 + (int16_t)(((int32_t)((int16_t)(die) - \
    FLOWER_DIE_REFERENCE(word) - ((int16_t)(mv)-3000)/25) * 1422 + 128) >> 8)))

/* Interrupts off, LED, sensor rail and excitation off, sensor pins without
 * pulls: the first thing main does. */
void flower_startup(void);
/* Timer 3 as the soil excitation (P1.4 stays a low GPIO until power on). */
void flower_init(void);
/* Sensor rail and excitation on or off. */
void flower_power(uint8_t on);
/* One measurement into v (the rail must have warmed up). */
void flower_measure(gpd_values_t MCU_XDATA *v);

#endif
