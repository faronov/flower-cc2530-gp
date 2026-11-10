/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef FLOWER_APP_CONFIG_H
#define FLOWER_APP_CONFIG_H
/* Included by the stack's cc2530_mmio.h for CC2530_APP (board
 * BOARD_EXTERNAL): macros only, no includes. */

/* board.h requires an identity; the GPD itself sends GPD_MANUFACTURER and
 * GPD_MODEL (gpd_frame.h). */
#ifdef BOARD_BGT1
#define BOARD_MANUFACTURER "EfektaLab"
#define BOARD_MODEL "EfektaLab_BGT1_GP"
#define BOARD_MODEL_LENGTH 17u

/* EfektaLab BGT1 (BEE_DEN_T1), pin map read from its Z-Stack firmware:
 * LED P0.1 active high, button P2.0 active low (pull-up), DS18B20 supply
 * P0.2 (high while measuring) and 1-Wire P0.3 on the port 0 pulls (up while
 * measuring, down otherwise, P2INP.PDUP0). PA/LNA front end in the
 * CC2590/CC2591 wiring (Z-Stack paLnaChip 2): PA_EN P1.1 and LNA_EN P1.4 from
 * the radio observation outputs, HGM P0.7 high. Two AAA cells. */
#define BOARD_FLOWER_LED 0x02u
#define BOARD_FLOWER_BUTTON 0x01u
#define BOARD_BGT1_ONEWIRE_POWER 0x04u
#define BOARD_FLOWER_ONEWIRE 0x08u
#define BOARD_BGT1_HGM 0x80u
#define BOARD_BGT1_PA_EN 0x02u
#define BOARD_BGT1_LNA_EN 0x10u
#define BOARD_PA_LNA 1
#define BOARD_BME280 0
#define BOARD_ONEWIRE_PORT SOC_P0
#define BOARD_ONEWIRE_DIR SOC_P0DIR
#define BOARD_DS18B20_BITS 11

#define CC2530_APP_REGISTER_LIST(X) \
    X(SOC_T1CNTL, 0xe2) \
    X(SOC_T1CNTH, 0xe3) \
    X(SOC_T1CTL, 0xe4) \
    X(SOC_P2IFG, 0x8b) \
    X(SOC_P2IEN, 0xac) \
    X(SOC_PICTL, 0x8c) \
    X(SOC_IRCON2, 0xe8)

#else
#define BOARD_MANUFACTURER "modkam.ru"
#define BOARD_MODEL "DIYRuZ_FlowerX_GP"
#define BOARD_MODEL_LENGTH 17u

/* faronov/flower v2 (E18-MS1-PCB), HAL_BOARD_FLOWER pin map of the reference
 * firmware: LED P0.1 active high, button P2.0 active low (pull-up), sensor
 * rail P1.1 high=on, soil excitation P1.4 (Timer3 alt1 ch1), soil AIN4 P0.4,
 * light AIN7 P0.7, BME280 I2C P0.5 SCL / P0.6 SDA, DS18B20 1-Wire P1.3. */
#define BOARD_FLOWER_LED 0x02u
#define BOARD_FLOWER_BUTTON 0x01u
#define BOARD_FLOWER_RAIL 0x02u
#define BOARD_FLOWER_PWM 0x10u
#define BOARD_FLOWER_SOIL 0x10u
#define BOARD_FLOWER_LIGHT 0x80u
#define BOARD_FLOWER_SCL 0x20u
#define BOARD_FLOWER_SDA 0x40u
#define BOARD_FLOWER_ONEWIRE 0x08u
#define BOARD_PA_LNA 0
#define BOARD_BME280 1
#define BOARD_ONEWIRE_PORT SOC_P1
#define BOARD_ONEWIRE_DIR SOC_P1DIR
#define BOARD_DS18B20_BITS 10

/* Timer 3 drives the soil excitation. */
#define CC2530_APP_REGISTER_LIST(X) \
    X(SOC_T3CTL, 0xcb) \
    X(SOC_T3CC0, 0xcd) \
    X(SOC_T3CCTL1, 0xce) \
    X(SOC_T1CNTL, 0xe2) \
    X(SOC_T1CNTH, 0xe3) \
    X(SOC_T1CTL, 0xe4) \
    X(SOC_P2IFG, 0x8b) \
    X(SOC_P2IEN, 0xac) \
    X(SOC_PICTL, 0x8c) \
    X(SOC_IRCON2, 0xe8)
#endif

#endif
