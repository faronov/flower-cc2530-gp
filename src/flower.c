/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "flower.h"
#include "sensors.h"

/* SWRU191F 7.18 PERCFG, 7.18 P2SEL, 12.2 APCFG/ADCCON1/ADCCON3. */
#define T3_ALT2 0x20u
#define PRI2P1 0x20u
#define TR0 0x624bu
#define ATEST 0x61bdu

void flower_startup(void)
{
    MMIO_WRITE(SOC_IEN0, 0);
    MMIO_WRITE(SOC_IEN1, 0);
    MMIO_WRITE(SOC_IEN2, 0);
    /* Latches first, then direction: LED off, sensor rail off, excitation low. */
    MMIO_CLEAR(SOC_P0, BOARD_FLOWER_LED);
    MMIO_CLEAR(SOC_P0SEL, BOARD_FLOWER_LED);
    MMIO_SET(SOC_P0DIR, BOARD_FLOWER_LED);
    MMIO_CLEAR(SOC_P1, BOARD_FLOWER_RAIL | BOARD_FLOWER_PWM);
    MMIO_CLEAR(SOC_P1SEL, BOARD_FLOWER_RAIL | BOARD_FLOWER_PWM);
    MMIO_SET(SOC_P1DIR, BOARD_FLOWER_RAIL | BOARD_FLOWER_PWM);
    /* Unpowered sensor pins: no internal pull current into the dead rail. */
    MMIO_SET(SOC_P0INP, BOARD_FLOWER_SOIL | BOARD_FLOWER_SCL | BOARD_FLOWER_SDA | BOARD_FLOWER_LIGHT);
    MMIO_SET(SOC_P1INP, BOARD_FLOWER_ONEWIRE);
}

/* Timer 3 channel 1 (alternative 1, P1.4) toggles at T3CC0 in up/down mode. */
void flower_init(void)
{
    MMIO_CLEAR(SOC_PERCFG, T3_ALT2);
    MMIO_SET(SOC_P2SEL, PRI2P1);
    MMIO_WRITE(SOC_T3CTL, 0x07u);
    MMIO_WRITE(SOC_T3CCTL1, 0x14u);
    MMIO_WRITE(SOC_T3CC0, 4u);
}

void flower_excitation_off(void)
{
    MMIO_CLEAR(SOC_T3CTL, 0x10u);
    MMIO_SET(SOC_T3CTL, 0x04u);
    MMIO_CLEAR(SOC_P1, BOARD_FLOWER_PWM);
    MMIO_CLEAR(SOC_P1SEL, BOARD_FLOWER_PWM);
}

void flower_power(uint8_t on)
{
    if (on) {
        MMIO_SET(SOC_P1, BOARD_FLOWER_RAIL);
        sensors_bus(1);
        MMIO_SET(SOC_P1SEL, BOARD_FLOWER_PWM);
        MMIO_SET(SOC_T3CTL, 0x10u);
        return;
    }
    flower_excitation_off();
    sensors_bus(0);
    MMIO_CLEAR(SOC_P1, BOARD_FLOWER_RAIL);
}

/* n single conversions (ADCCON3, SWRU191F 12.2.10) averaged as 14-bit
 * positive codes; FFFF when a conversion misses its poll bound. */
static uint16_t adc(uint8_t command, uint8_t n)
{
    uint32_t sum = 0;
    uint16_t polls, code;
    uint8_t i, low;
    for (i = n; i; i--) {
        (void)MMIO_READ(SOC_ADCH);
        MMIO_WRITE(SOC_ADCCON3, command);
        for (polls = FLOWER_ADC_POLLS; !(MMIO_READ(SOC_ADCCON1) & 0x80u); )
            if (!--polls) return FLOWER_INVALID_U16;
        low = MMIO_READ(SOC_ADCL);
        code = (uint16_t)MMIO_READ(SOC_ADCH) << 8 | low;
        if (!(code & 0x8000u)) sum += code >> 2;
    }
    return (uint16_t)(sum / n);
}

/* flower battery.c Alkaline 2S curve, 0.5 % units. */
static uint8_t percent(uint16_t mv)
{
    uint16_t p;
    if (mv >= 3000u) p = 100;
    else if (mv > 2800u) p = 80u + (mv-2800u)*20u/200u;
    else if (mv > 2500u) p = 40u + (mv-2500u)*40u/300u;
    else if (mv > 2200u) p = 10u + (mv-2200u)*30u/300u;
    else if (mv > 2000u) p = (mv-2000u)*10u/200u;
    else p = 0;
    return (uint8_t)(p*2u);
}

/* flower zclApp_ReadSoilHumidity: air..water maps to 0..100.00 %, within
 * one calibrated span beyond either end; otherwise invalid. */
static uint16_t moisture(uint16_t raw, uint16_t battery)
{
    uint16_t air = FLOWER_AIR(battery), water = FLOWER_WATER(battery), span;
    if (raw > FLOWER_ADC_MAX || air > FLOWER_ADC_MAX || battery > FLOWER_ADC_MAX || air <= water)
        return FLOWER_INVALID_U16;
    span = air-water;
    if (raw < (water > span ? (uint16_t)(water-span) : 0u) ||
        (uint32_t)raw > ((uint32_t)air+span < FLOWER_ADC_MAX ? (uint32_t)air+span : FLOWER_ADC_MAX))
        return FLOWER_INVALID_U16;
    if (raw >= air) return 0;
    if (raw <= water) return 10000u;
    return (uint16_t)((uint32_t)(air-raw)*10000u/span);
}

/* ZCL MeasuredValue 10000*log10(raw)+1 with the raw ADC as uncalibrated lux:
 * log2 by repeated squaring of the Q15 mantissa (12 fraction bits). */
static uint16_t illuminance(uint16_t raw)
{
    uint32_t m;
    uint16_t l, bit;
    uint8_t k = 0;
    if (raw >= FLOWER_LIGHT_OPEN) return FLOWER_INVALID_U16;
    if (!raw) return 0;
    for (l = raw; l > 1u; l >>= 1) k++;
    m = (uint32_t)raw << (15u-k);
    l = (uint16_t)k << 12;
    for (bit = 0x800u; bit; bit >>= 1) {
        m = (m*m) >> 15;
        if (m >= 0x10000UL) { m >>= 1; l |= bit; }
    }
    return (uint16_t)(((uint32_t)l*30103u+40959u)/40960u+1u);
}

/* Die sensor: TR0.ADCTM then ATEST connect it to channel 14 (one discarded
 * conversion, eight averaged); ATEST is restored before the AIN channels. */
static int16_t chip(uint16_t battery)
{
    uint16_t die, factory;
    uint8_t atest = MMIO_XREAD(ATEST);
    MMIO_XWRITE(TR0, 1); MMIO_XWRITE(ATEST, 1);
    (void)adc(0x3eu, 1);
    die = adc(0x3eu, 8);
    MMIO_XWRITE(ATEST, atest); MMIO_XWRITE(TR0, 0);
    factory = (uint16_t)MMIO_XREAD(FLOWER_FACTORY) << 8 | MMIO_XREAD(FLOWER_FACTORY+1u);
    /* A zero (negative) or missed average is a fault: invalid. */
    if ((uint16_t)(die-1u) >= FLOWER_ADC_MAX || (uint16_t)(battery-1u) >= FLOWER_ADC_MAX)
        return (int16_t)FLOWER_INVALID_S16;
    return FLOWER_DIE_CENTI(die, FLOWER_MILLIVOLTS(battery), factory);
}

/* Battery (VDD/3, internal reference), the die sensor, then illuminance and
 * soil (AVDD reference) like flower's APP_PHASE_ADC; the soil curve uses the
 * battery raw ADC. */
void flower_measure(gpd_values_t MCU_XDATA *v)
{
    uint16_t battery, light, soil, mv;
    MMIO_SET(SOC_APCFG, BOARD_FLOWER_SOIL | BOARD_FLOWER_LIGHT);
    battery = adc(0x3fu, 5);
    v->temperature = chip(battery);
    light = adc(0xb7u, 5);
    soil = adc(0xb4u, 3);
    MMIO_CLEAR(SOC_APCFG, BOARD_FLOWER_SOIL | BOARD_FLOWER_LIGHT);
    if (battery > FLOWER_ADC_MAX) {
        v->voltage = v->percent = 0xffu;
    } else {
        mv = FLOWER_MILLIVOLTS(battery);
        v->voltage = (uint8_t)((mv+50u)/100u);
        v->percent = percent(mv);
    }
    v->soil = moisture(soil, battery);
    v->illuminance = illuminance(light);
}
