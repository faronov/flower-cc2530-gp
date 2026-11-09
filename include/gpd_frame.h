/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
/* Green Power Device frames (Zigbee Green Power Basic, NoSecurity) for the
 * FlowerX GP prototype: an IEEE 802.15.4 data frame to PAN/short FFFF with
 * no source address and no ACK request, then the GPDF: NWK frame control
 * 0x0C (data frame, protocol version 3, Application ID 0, no extension),
 * the 4-byte SrcID, the command ID and its payload. The radio appends the
 * FCS (AUTOCRC). Both builders return the frame length without the FCS.
 * The includer defines MCU_XDATA. */
#ifndef GPD_FRAME_H
#define GPD_FRAME_H

#include <stdint.h>

/* MAC header (7), NWK frame control (1), SrcID (4); the command follows. */
#define GPD_HEADER 12u
#define GPD_COMMISSIONING 0xe0u
#define GPD_MULTI_REPORT 0xa2u
/* Commissioning: IndoorEnvironmentSensor, MAC sequence number capability,
 * application information present, fixed location; unidirectional (no RX
 * after TX), no extended options so no security. Application information:
 * manufacturer ID, model ID and the cluster list (four servers). */
#define GPD_DEVICE_ID 0x33u
#define GPD_OPTIONS 0x45u
#define GPD_APP_INFO 0x0bu
#ifndef GPD_MANUFACTURER
/* Not an assigned Zigbee manufacturer code: a prototype value the
 * matching ZHA quirk keys on, together with the model. */
#define GPD_MANUFACTURER 0x6d6bu
#endif
#ifndef GPD_MODEL
#define GPD_MODEL 0xf100u
#endif
#define GPD_COMMISSIONING_LENGTH (GPD_HEADER + 1u + 16u)
#define GPD_REPORT_LENGTH (GPD_HEADER + 1u + 33u)

typedef struct {
    int16_t temperature;     /* 0x0402/0000, 0.01 C, 0x8000 invalid */
    uint16_t soil;           /* 0x0408/0000, 0.01 %, FFFF invalid */
    uint16_t illuminance;    /* 0x0400/0000, 10000*log10(lux)+1, FFFF invalid */
    uint8_t voltage;         /* 0x0001/0020, 100 mV, FF invalid */
    uint8_t percent;         /* 0x0001/0021, 0.5 %, FF invalid */
} gpd_values_t;

/* SrcID from the low half of the IEEE address (LSB first). 0 and
 * FFFFFFF9..FFFFFFFF are reserved (A.1.4.1.1): 0 becomes 1, the others
 * FFFFFFF1..FFFFFFF7. */
__attribute__((unused)) static void gpd_source(uint8_t MCU_XDATA *src, const uint8_t MCU_XDATA *ieee)
{
    uint8_t i, zero = 1, high = 1;
    for (i = 0; i < 4u; i++) {
        src[i] = ieee[i];
        if (src[i]) zero = 0;
        if (i && src[i] != 0xffu) high = 0;
    }
    if (zero) src[0] = 1;
    else if (high && src[0] >= 0xf9u) src[0] &= 0xf7u;
}

static uint8_t gpd_header(uint8_t MCU_XDATA *f, uint8_t seq, const uint8_t MCU_XDATA *src, uint8_t command)
{
    f[0] = 0x01; f[1] = 0x08; f[2] = seq;
    f[3] = f[4] = f[5] = f[6] = 0xff;
    f[7] = 0x0c;
    f[8] = src[0]; f[9] = src[1]; f[10] = src[2]; f[11] = src[3];
    f[12] = command;
    return GPD_HEADER + 1u;
}

__attribute__((unused)) static uint8_t gpd_commissioning(uint8_t MCU_XDATA *f, uint8_t seq, const uint8_t MCU_XDATA *src)
{
    uint8_t n = gpd_header(f, seq, src, GPD_COMMISSIONING);
    f[n++] = GPD_DEVICE_ID; f[n++] = GPD_OPTIONS; f[n++] = GPD_APP_INFO;
    f[n++] = (uint8_t)GPD_MANUFACTURER; f[n++] = (uint8_t)(GPD_MANUFACTURER >> 8);
    f[n++] = (uint8_t)GPD_MODEL; f[n++] = (uint8_t)(GPD_MODEL >> 8);
    f[n++] = 0x04;
    f[n++] = 0x01; f[n++] = 0x00;   /* Power Configuration */
    f[n++] = 0x00; f[n++] = 0x04;   /* Illuminance Measurement */
    f[n++] = 0x02; f[n++] = 0x04;   /* Temperature Measurement */
    f[n++] = 0x08; f[n++] = 0x04;   /* Soil Moisture */
    return n;
}

/* One Multi-Cluster Reporting record: cluster, attribute, type, value. */
static uint8_t gpd_record(uint8_t MCU_XDATA *f, uint8_t n, uint16_t cluster, uint8_t attribute,
                          uint8_t type, uint16_t value)
{
    f[n++] = (uint8_t)cluster; f[n++] = (uint8_t)(cluster >> 8);
    f[n++] = attribute; f[n++] = 0;
    f[n++] = type;
    f[n++] = (uint8_t)value;
    if (type != 0x20u) f[n++] = (uint8_t)(value >> 8);
    return n;
}

__attribute__((unused)) static uint8_t gpd_report(uint8_t MCU_XDATA *f, uint8_t seq,
                                                  const uint8_t MCU_XDATA *src,
                                                  const gpd_values_t MCU_XDATA *v)
{
    uint8_t n = gpd_header(f, seq, src, GPD_MULTI_REPORT);
    n = gpd_record(f, n, 0x0402u, 0x00u, 0x29u, (uint16_t)v->temperature);
    n = gpd_record(f, n, 0x0408u, 0x00u, 0x21u, v->soil);
    n = gpd_record(f, n, 0x0400u, 0x00u, 0x21u, v->illuminance);
    n = gpd_record(f, n, 0x0001u, 0x20u, 0x20u, v->voltage);
    n = gpd_record(f, n, 0x0001u, 0x21u, 0x20u, v->percent);
    return n;
}

#endif
