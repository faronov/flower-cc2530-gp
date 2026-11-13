/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
/* Green Power Device frames (Zigbee Green Power) for the FlowerX GP
 * prototype: an IEEE 802.15.4 data frame to PAN/short FFFF
 * with no source address and no ACK request, then the GPDF: NWK frame
 * control 0x0C (data frame, protocol version 3, Application ID 0, no
 * extension), the 4-byte SrcID, the command ID and its payload. The radio
 * appends the FCS (AUTOCRC). The builders return the frame length without
 * the FCS. With GPD_SECURITY (default) gpd_secure turns such a frame into
 * a SecurityLevel 3 one (individual key) and the Commissioning GPDF carries
 * the key. The includer defines MCU_XDATA and, with GPD_SECURITY,
 * gpd_aes. */
#ifndef GPD_FRAME_H
#define GPD_FRAME_H

#include <stdint.h>

/* MAC header (7), NWK frame control (1), SrcID (4); the command follows. */
#define GPD_HEADER 12u
#define GPD_COMMISSIONING 0xe0u
#define GPD_DECOMMISSIONING 0xe1u
#define GPD_CHANNEL_REQUEST 0xe3u
#define GPD_CHANNEL_CONFIGURATION 0xf3u
#define GPD_MULTI_REPORT 0xa2u
/* Maintenance frames (A.1.4.1.2, A.1.4.2.1): protocol version 3, no
 * extended frame control, no SrcID. Auto-Commissioning 1 announces another
 * Channel Request; 0 (the last of a burst) opens the GPD's receive window
 * gpdRxOffset (20 ms) after it. */
#define GPD_MAINTENANCE 0x0du
#define GPD_MAINTENANCE_MORE 0x4du
#define GPD_CHANNEL_REQUEST_LENGTH 10u
#define GPD_DECOMMISSIONING_LENGTH (GPD_HEADER + 1u)
/* Commissioning: IndoorEnvironmentSensor, MAC sequence number capability,
 * application information present, fixed location; unidirectional (no RX
 * after TX). Application information: manufacturer ID, model ID and the
 * server cluster list. */
#define GPD_DEVICE_ID 0x33u
#define GPD_SERVER_CLUSTERS 4u
#ifndef GPD_SECURITY
#define GPD_SECURITY 1
#endif
#if GPD_SECURITY
/* Extended options present; extended options (A.4.2.1.1.3): SecurityLevel
 * capabilities 3, KeyType 4 (individual out-of-the-box GPD key), key
 * present, key encrypted (with its MIC), outgoing counter present. */
#define GPD_OPTIONS 0xc5u
#define GPD_EXTENDED_OPTIONS 0xf3u
#else
#define GPD_OPTIONS 0x45u
#endif
#define GPD_APP_INFO 0x0bu
#ifndef GPD_MANUFACTURER
/* Not an assigned Zigbee manufacturer code: a prototype value the
 * matching ZHA quirk keys on, together with the model. */
#define GPD_MANUFACTURER 0x6d6bu
#endif
#ifndef GPD_MODEL
#define GPD_MODEL 0xf100u
#endif
/* Optional sensors (sensors.h): the model ID carries them in its low bits
 * so the sink's quirk knows which values come. */
#define GPD_HAS_BME280 0x01u
#define GPD_HAS_DS18B20 0x02u
#if GPD_SECURITY
/* Extended options, key, key MIC, outgoing counter; extended NWK frame
 * control, security frame counter, MIC. */
#define GPD_COMMISSIONING_LENGTH (GPD_HEADER + 1u + 20u + 25u)
#define GPD_SECURITY_OVERHEAD 9u
#else
#define GPD_COMMISSIONING_LENGTH (GPD_HEADER + 1u + 20u)
#define GPD_SECURITY_OVERHEAD 0u
#endif
#define GPD_REPORT_LENGTH (GPD_HEADER + 1u + 54u)
#define GPD_DECOMMISSIONING_SECURED (GPD_DECOMMISSIONING_LENGTH + GPD_SECURITY_OVERHEAD)
/* DS18B20 next to a BME280: soil temperature, a manufacturer-specific
 * Temperature Measurement attribute (ZCL r8 2.6.1.3 range 0x5000..0xEFFF),
 * int16 0.01 C. A DS18B20 alone is the air temperature (0x0402/0000). */
#define GPD_HAS_SOIL_TEMPERATURE(sensors) \
    (((sensors) & (GPD_HAS_BME280 | GPD_HAS_DS18B20)) == (GPD_HAS_BME280 | GPD_HAS_DS18B20))
#define GPD_SOIL_TEMPERATURE 0x5000u

typedef struct {
    int16_t temperature;     /* 0x0402/0000, 0.01 C, 0x8000 invalid */
    uint16_t soil;           /* 0x0408/0000, 0.01 %, FFFF invalid */
    uint16_t illuminance;    /* 0x0400/0000, 10000*log10(lux)+1, FFFF invalid */
    uint8_t voltage;         /* 0x0001/0020, 100 mV, FF invalid */
    uint8_t percent;         /* 0x0001/0021, 0.5 %, FF invalid */
    uint16_t humidity;       /* 0x0405/0000, 0.01 %, FFFF invalid (BME280) */
    int16_t pressure;        /* 0x0403/0000, 0.1 kPa, 0x8000 invalid (BME280) */
    int16_t soil_temperature; /* 0x0402/5000, 0.01 C, 0x8000 invalid (DS18B20) */
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

#if GPD_SECURITY
#ifndef MCU_CODE
#define MCU_CODE
#endif
/* block = AES-128(key, block), in place (FIPS 197). */
void gpd_aes(const uint8_t MCU_XDATA *key, uint8_t MCU_XDATA *block);

/* The default Trust Center link key "ZigBeeAlliance09" protects the GPD
 * key in the Commissioning GPDF (A.3.7.1.2.3). */
static const MCU_CODE uint8_t gpd_tclk[16] = {
    0x5a, 0x69, 0x67, 0x42, 0x65, 0x65, 0x41, 0x6c, 0x6c, 0x69, 0x61, 0x6e, 0x63, 0x65, 0x30, 0x39
};
static MCU_XDATA uint8_t gpd_mac[16], gpd_ctr[16], gpd_link[16];

/* flags | nonce | 0 | tail, the nonce (A.1.5.4.1) SrcID | SrcID | fc | 05. */
static void gpd_nonce(uint8_t MCU_XDATA *b, uint8_t flags, const uint8_t MCU_XDATA *src,
                      const uint8_t MCU_XDATA *fc, uint8_t tail)
{
    uint8_t i;
    b[0] = flags;
    for (i = 0; i < 4u; i++) { b[1u+i] = src[i]; b[5u+i] = src[i]; b[9u+i] = fc[i]; }
    b[13] = 0x05; b[14] = 0; b[15] = tail;
}

/* CCM* (IEEE 802.15.4 B.4, A.1.5.4) with a 4-byte MIC and a 2-byte length:
 * authenticates a (alen <= 14) and m, encrypts m (len) in place and
 * appends the MIC. */
static void gpd_ccm(const uint8_t MCU_XDATA *key, const uint8_t MCU_XDATA *src, const uint8_t MCU_XDATA *fc,
                    const uint8_t MCU_XDATA *a, uint8_t alen, uint8_t MCU_XDATA *m, uint8_t len)
{
    uint8_t i;
    gpd_nonce(gpd_mac, 0x49, src, fc, len);
    gpd_aes(key, gpd_mac);
    gpd_mac[1] ^= alen;
    for (i = 0; i < alen; i++) gpd_mac[2u+i] ^= a[i];
    gpd_aes(key, gpd_mac);
    for (i = 0; i < len; i++) {
        gpd_mac[i & 15u] ^= m[i];
        if ((i & 15u) == 15u || i + 1u == len) gpd_aes(key, gpd_mac);
    }
    for (i = 0; i < len; i++) {
        if (!(i & 15u)) {
            gpd_nonce(gpd_ctr, 0x01, src, fc, (uint8_t)((i >> 4) + 1u));
            gpd_aes(key, gpd_ctr);
        }
        m[i] ^= gpd_ctr[i & 15u];
    }
    gpd_nonce(gpd_ctr, 0x01, src, fc, 0);
    gpd_aes(key, gpd_ctr);
    for (i = 0; i < 4u; i++) m[len+i] = (uint8_t)(gpd_mac[i] ^ gpd_ctr[i]);
}

/* A frame of the builders below (n bytes) to SecurityLevel 3 with the
 * individual key (A.1.4.1, A.1.5.4.3.3): NWK frame control with the
 * extension bit, extended frame control 0x38 (Application ID 0, level 3,
 * individual key, GPD to GPP), SrcID, the security frame counter, then the
 * encrypted command and payload and the MIC, authenticated over the NWK
 * header. The new length (n + GPD_SECURITY_OVERHEAD). */
__attribute__((unused)) static uint8_t gpd_secure(uint8_t MCU_XDATA *f, uint8_t n, const uint8_t MCU_XDATA *key,
                                                  uint32_t counter)
{
    uint8_t i;
    for (i = n; i > GPD_HEADER; i--) f[i + 4u] = f[i - 1u];
    for (i = GPD_HEADER; i > 8u; i--) f[i] = f[i - 1u];
    f[7] = 0x8c; f[8] = 0x38;
    for (i = 0; i < 4u; i++) { f[13u+i] = (uint8_t)counter; counter >>= 8; }
    gpd_ccm(key, f + 9, f + 13, f + 7, 10, f + 17, (uint8_t)(n - GPD_HEADER));
    return (uint8_t)(n + GPD_SECURITY_OVERHEAD);
}
#endif

/* With GPD_SECURITY the key (protected with gpd_tclk, nonce SrcID | SrcID |
 * SrcID, the SrcID authenticated) and counter, the security frame counter
 * the next secured GPDF exceeds. */
#if GPD_SECURITY
__attribute__((unused)) static uint8_t gpd_commissioning(uint8_t MCU_XDATA *f, uint8_t seq, const uint8_t MCU_XDATA *src,
                                                         uint8_t sensors, const uint8_t MCU_XDATA *key,
                                                         uint32_t counter)
#else
__attribute__((unused)) static uint8_t gpd_commissioning(uint8_t MCU_XDATA *f, uint8_t seq, const uint8_t MCU_XDATA *src,
                                                         uint8_t sensors)
#endif
{
    uint8_t n = gpd_header(f, seq, src, GPD_COMMISSIONING);
#if GPD_SECURITY
    uint8_t i;
    f[n++] = GPD_DEVICE_ID; f[n++] = GPD_OPTIONS; f[n++] = GPD_EXTENDED_OPTIONS;
    for (i = 0; i < 16u; i++) { f[n+i] = key[i]; gpd_link[i] = gpd_tclk[i]; }
    gpd_ccm(gpd_link, src, src, src, 4, f + n, 16);
    n += 20u;
    for (i = 0; i < 4u; i++) { f[n++] = (uint8_t)counter; counter >>= 8; }
    f[n++] = GPD_APP_INFO;
#else
    f[n++] = GPD_DEVICE_ID; f[n++] = GPD_OPTIONS; f[n++] = GPD_APP_INFO;
#endif
    f[n++] = (uint8_t)GPD_MANUFACTURER; f[n++] = (uint8_t)(GPD_MANUFACTURER >> 8);
    f[n++] = (uint8_t)(GPD_MODEL | sensors); f[n++] = (uint8_t)(GPD_MODEL >> 8);
    f[n++] = (sensors & GPD_HAS_BME280) ? GPD_SERVER_CLUSTERS + 2u : GPD_SERVER_CLUSTERS;
    f[n++] = 0x01; f[n++] = 0x00;   /* Power Configuration */
    f[n++] = 0x00; f[n++] = 0x04;   /* Illuminance Measurement */
    f[n++] = 0x02; f[n++] = 0x04;   /* Temperature Measurement */
    f[n++] = 0x08; f[n++] = 0x04;   /* Soil Moisture */
    if (sensors & GPD_HAS_BME280) {
        f[n++] = 0x05; f[n++] = 0x04;   /* Relative Humidity Measurement */
        f[n++] = 0x03; f[n++] = 0x04;   /* Pressure Measurement */
    }
    return n;
}

/* Channel Request (A.4.2.1.4): the receive channel of the next and the
 * second next attempt, both rx (11..26) here. */
__attribute__((unused)) static uint8_t gpd_channel_request(uint8_t MCU_XDATA *f, uint8_t seq, uint8_t more,
                                                           uint8_t rx)
{
    f[0] = 0x01; f[1] = 0x08; f[2] = seq;
    f[3] = f[4] = f[5] = f[6] = 0xff;
    f[7] = more ? GPD_MAINTENANCE_MORE : GPD_MAINTENANCE;
    f[8] = GPD_CHANNEL_REQUEST;
    f[9] = (uint8_t)((rx - 11u) * 0x11u);
    return GPD_CHANNEL_REQUEST_LENGTH;
}

__attribute__((unused)) static uint8_t gpd_decommissioning(uint8_t MCU_XDATA *f, uint8_t seq,
                                                           const uint8_t MCU_XDATA *src)
{
    return gpd_header(f, seq, src, GPD_DECOMMISSIONING);
}

/* A received MPDU (without the PHR, n bytes with the two FCS/status bytes):
 * the operational channel 11..26 of a Channel Configuration (A.4.2.1.5:
 * unsecured MAC data frame, Maintenance NWK frame control with
 * Auto-Commissioning 0, no SrcID), otherwise 0. */
__attribute__((unused)) static uint8_t gpd_channel_configuration(const uint8_t MCU_XDATA *m, uint8_t n)
{
    uint8_t i = 3, mode;
    if (n < 5u) return 0;
    n -= 2u;
    if ((m[0] & 0x0fu) != 0x01u) return 0;   /* data frame, no security, no pending */
    mode = (uint8_t)(m[1] >> 2 & 3u);
    if (mode == 1u) return 0;
    if (mode) i = (uint8_t)(i + 2u + (mode == 2u ? 2u : 8u));
    mode = (uint8_t)(m[1] >> 6);
    if (mode == 1u) return 0;
    if (mode) i = (uint8_t)(i + ((m[0] & 0x40u) ? 0u : 2u) + (mode == 2u ? 2u : 8u));
    if ((uint8_t)(i + 3u) > n || m[i] != GPD_MAINTENANCE || m[i+1u] != GPD_CHANNEL_CONFIGURATION)
        return 0;
    return (uint8_t)((m[i+2u] & 0x0fu) + 11u);
}

/* One Multi-Cluster Reporting record: cluster, attribute, type, value. */
static uint8_t gpd_record(uint8_t MCU_XDATA *f, uint8_t n, uint16_t cluster, uint16_t attribute,
                          uint8_t type, uint16_t value)
{
    f[n++] = (uint8_t)cluster; f[n++] = (uint8_t)(cluster >> 8);
    f[n++] = (uint8_t)attribute; f[n++] = (uint8_t)(attribute >> 8);
    f[n++] = type;
    f[n++] = (uint8_t)value;
    if (type != 0x20u) f[n++] = (uint8_t)(value >> 8);
    return n;
}

__attribute__((unused)) static uint8_t gpd_report(uint8_t MCU_XDATA *f, uint8_t seq,
                                                  const uint8_t MCU_XDATA *src,
                                                  const gpd_values_t MCU_XDATA *v, uint8_t sensors)
{
    uint8_t n = gpd_header(f, seq, src, GPD_MULTI_REPORT);
    n = gpd_record(f, n, 0x0402u, 0x00u, 0x29u, (uint16_t)v->temperature);
    n = gpd_record(f, n, 0x0408u, 0x00u, 0x21u, v->soil);
    n = gpd_record(f, n, 0x0400u, 0x00u, 0x21u, v->illuminance);
    n = gpd_record(f, n, 0x0001u, 0x20u, 0x20u, v->voltage);
    n = gpd_record(f, n, 0x0001u, 0x21u, 0x20u, v->percent);
    if (sensors & GPD_HAS_BME280) {
        n = gpd_record(f, n, 0x0405u, 0x00u, 0x21u, v->humidity);
        n = gpd_record(f, n, 0x0403u, 0x00u, 0x29u, (uint16_t)v->pressure);
    }
    if (GPD_HAS_SOIL_TEMPERATURE(sensors))
        n = gpd_record(f, n, 0x0402u, GPD_SOIL_TEMPERATURE, 0x29u, (uint16_t)v->soil_temperature);
    return n;
}

#endif
