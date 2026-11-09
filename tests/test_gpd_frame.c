/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
/* The Green Power frames of the FlowerX GP prototype (include/gpd_frame.h)
 * against hand-assembled bytes (Zigbee Green Power Basic: MAC header, NWK
 * frame control 0x0C, SrcID, command, payload). With an argument, prints the
 * frames as hex for an external parser cross-check. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MCU_XDATA
#include "gpd_frame.h"
#include "bme280_math.h"

#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr, "gpd_frame: line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static unsigned long checks;

static void hex(const char *name, const uint8_t *f, uint8_t n)
{
    uint8_t i;
    printf("%s ", name);
    for (i = 0; i < n; i++) printf("%02x", f[i]);
    printf("\n");
}

/* Negative H4/H5 (sign-extended MSBs). */
static void humidity_negative(bme280_calib_t *c)
{
    static const uint8_t b[7] = {0, 0, 0, 0xff, 0x7f, 0x80, 0};
    bme280_calib_humidity(c, b);
    CHECK(c->h4 == -1 && c->h5 == -2041);
}

int main(int argc, char **argv)
{
    static const uint8_t ieee[8] = {0x15, 0x28, 0xf6, 0x0e, 0x00, 0x4b, 0x12, 0x00};
    static const uint8_t commissioning[] = {
        0x01, 0x08, 0x42, 0xff, 0xff, 0xff, 0xff, 0x0c, 0x15, 0x28, 0xf6, 0x0e, 0xe0,
        0x33, 0x45, 0x0b, 0x6b, 0x6d, 0x00, 0xf1, 0x04,
        0x01, 0x00, 0x00, 0x04, 0x02, 0x04, 0x08, 0x04
    };
    static const uint8_t report[] = {
        0x01, 0x08, 0x43, 0xff, 0xff, 0xff, 0xff, 0x0c, 0x15, 0x28, 0xf6, 0x0e, 0xa2,
        0x02, 0x04, 0x00, 0x00, 0x29, 0x0c, 0xfe,
        0x08, 0x04, 0x00, 0x00, 0x21, 0x10, 0x27,
        0x00, 0x04, 0x00, 0x00, 0x21, 0xff, 0xff,
        0x01, 0x00, 0x20, 0x00, 0x20, 0x1e,
        0x01, 0x00, 0x21, 0x00, 0x20, 0xc8
    };
    static const uint8_t commissioning_all[] = {
        0x01, 0x08, 0x44, 0xff, 0xff, 0xff, 0xff, 0x0c, 0x15, 0x28, 0xf6, 0x0e, 0xe0,
        0x33, 0x45, 0x0b, 0x6b, 0x6d, 0x03, 0xf1, 0x06,
        0x01, 0x00, 0x00, 0x04, 0x02, 0x04, 0x08, 0x04, 0x05, 0x04, 0x03, 0x04
    };
    static const uint8_t extra[] = {
        0x05, 0x04, 0x00, 0x00, 0x21, 0x8f, 0x17,
        0x03, 0x04, 0x00, 0x00, 0x29, 0xf5, 0x03,
        0x02, 0x04, 0x00, 0x50, 0x29, 0xe2, 0x04
    };
    /* Maintenance Channel Requests announcing channel 15 (0b0100 twice). */
    static const uint8_t request_more[] = {0x01, 0x08, 0x45, 0xff, 0xff, 0xff, 0xff, 0x4d, 0xe3, 0x44};
    static const uint8_t request_last[] = {0x01, 0x08, 0x46, 0xff, 0xff, 0xff, 0xff, 0x0d, 0xe3, 0xff};
    static const uint8_t decommissioning[] = {
        0x01, 0x08, 0x47, 0xff, 0xff, 0xff, 0xff, 0x0c, 0x15, 0x28, 0xf6, 0x0e, 0xe1
    };
    /* Channel Configurations (two FCS status bytes): from a proxy with a
     * short source address and PAN ID compression, channel 20 with the
     * Basic bit; without a source address, channel 11. */
    static const uint8_t config_proxy[] = {
        0x41, 0x88, 0x10, 0x62, 0x1a, 0xff, 0xff, 0x34, 0x12, 0x0d, 0xf3, 0x19, 0xd0, 0x80
    };
    static const uint8_t config_bare[] = {0x01, 0x08, 0x11, 0xff, 0xff, 0xff, 0xff, 0x0d, 0xf3, 0x00, 0x00, 0x80};
    /* BMP280 datasheet 3.12 example (T and P calibration, register order). */
    static const uint8_t calibration[26] = {
        0x70, 0x6b, 0x43, 0x67, 0x18, 0xfc, 0x7d, 0x8e, 0x43, 0xd6, 0xd0, 0x0b, 0x27, 0x0b,
        0x8c, 0x00, 0xf9, 0xff, 0x8c, 0x3c, 0xf8, 0xc6, 0x70, 0x17, 0x00, 0x4b
    };
    static const uint8_t humidity[7] = {0x6a, 0x01, 0x00, 0x13, 0x2f, 0x03, 0x1e};
    /* Maxim AN27: ROM 02 1C B8 01 00 00 00, CRC A2. */
    static const uint8_t rom[8] = {0x02, 0x1c, 0xb8, 0x01, 0x00, 0x00, 0x00, 0xa2};
    uint8_t src[4], f[GPD_REPORT_LENGTH + 1u], m[sizeof(config_proxy)], n, z[4] = {0}, high[4] = {0xfe, 0xff, 0xff, 0xff};
    gpd_values_t v = { -500, 10000, 0xffffu, 30, 200, 6031, 1013, 1250 };
    bme280_calib_t c;
    int32_t fine;
    int16_t centi;
    uint32_t pa, rh;
    (void)argv;

    gpd_source(src, ieee);
    CHECK(src[0] == 0x15 && src[1] == 0x28 && src[2] == 0xf6 && src[3] == 0x0e);
    /* Reserved SrcIDs: 0 and FFFFFFF9..FFFFFFFF. */
    gpd_source(src, z);
    CHECK(src[0] == 1 && !src[1] && !src[2] && !src[3]);
    gpd_source(src, high);
    CHECK(src[0] == 0xf6);
    high[0] = 0xff; gpd_source(src, high); CHECK(src[0] == 0xf7);
    high[0] = 0xf9; gpd_source(src, high); CHECK(src[0] == 0xf1);
    high[0] = 0xf8; gpd_source(src, high); CHECK(src[0] == 0xf8);
    high[3] = 0xfe; high[0] = 0xff; gpd_source(src, high); CHECK(src[0] == 0xff);

    gpd_source(src, ieee);
    memset(f, 0xaa, sizeof(f));
    n = gpd_commissioning(f, 0x42, src, 0);
    CHECK(n == sizeof(commissioning));
    CHECK(!memcmp(f, commissioning, n) && f[n] == 0xaa);
    if (argc > 1) hex("commissioning", f, n);

    memset(f, 0xaa, sizeof(f));
    n = gpd_report(f, 0x43, src, &v, 0);
    CHECK(n == sizeof(report));
    CHECK(!memcmp(f, report, n) && f[n] == 0xaa);
    if (argc > 1) hex("report", f, n);

    /* BME280 and DS18B20: model F103, humidity and pressure clusters;
     * humidity, pressure and the soil temperature attribute 0x5000. */
    memset(f, 0xaa, sizeof(f));
    n = gpd_commissioning(f, 0x44, src, GPD_HAS_BME280 | GPD_HAS_DS18B20);
    CHECK(n == GPD_COMMISSIONING_LENGTH && n == sizeof(commissioning_all));
    CHECK(!memcmp(f, commissioning_all, n) && f[n] == 0xaa);
    if (argc > 1) hex("commissioning-all", f, n);
    memset(f, 0xaa, sizeof(f));
    n = gpd_report(f, 0x43, src, &v, GPD_HAS_BME280 | GPD_HAS_DS18B20);
    CHECK(n == GPD_REPORT_LENGTH && n == sizeof(report) + sizeof(extra));
    CHECK(!memcmp(f, report, sizeof(report)) && !memcmp(f + sizeof(report), extra, sizeof(extra)));
    CHECK(f[n] == 0xaa);
    if (argc > 1) hex("report-all", f, n);
    n = gpd_report(f, 0x43, src, &v, GPD_HAS_DS18B20);
    CHECK(n == sizeof(report) + 7u && !memcmp(f + sizeof(report), extra + 14, 7));
    /* PSDU with the FCS within aMaxPHYPacketSize. */
    CHECK(GPD_REPORT_LENGTH + 2u <= 127u);

    CHECK(gpd_channel_request(f, 0x45, 1, 15) == sizeof(request_more));
    CHECK(!memcmp(f, request_more, sizeof(request_more)));
    if (argc > 1) hex("channel-request", f, sizeof(request_more));
    CHECK(gpd_channel_request(f, 0x46, 0, 26) == sizeof(request_last));
    CHECK(!memcmp(f, request_last, sizeof(request_last)));
    CHECK(gpd_decommissioning(f, 0x47, src) == sizeof(decommissioning));
    CHECK(!memcmp(f, decommissioning, sizeof(decommissioning)));

    memcpy(m, config_proxy, sizeof(m));
    CHECK(gpd_channel_configuration(m, sizeof(config_proxy)) == 20u);
    memcpy(m, config_bare, sizeof(config_bare));
    CHECK(gpd_channel_configuration(m, sizeof(config_bare)) == 11u);
    CHECK(!gpd_channel_configuration(m, sizeof(config_bare) - 1u));
    m[7] = GPD_MAINTENANCE_MORE;
    CHECK(!gpd_channel_configuration(m, sizeof(config_bare)));
    m[7] = GPD_MAINTENANCE; m[8] = GPD_CHANNEL_REQUEST;
    CHECK(!gpd_channel_configuration(m, sizeof(config_bare)));
    m[8] = GPD_CHANNEL_CONFIGURATION; m[0] = 0x09;   /* secured */
    CHECK(!gpd_channel_configuration(m, sizeof(config_bare)));
    m[0] = 0x03;                                     /* MAC command */
    CHECK(!gpd_channel_configuration(m, sizeof(config_bare)));
    memcpy(m, config_proxy, sizeof(m));
    m[1] = 0xc8;                                     /* long source: truncated */
    CHECK(!gpd_channel_configuration(m, sizeof(config_proxy)));

    bme280_calib(&c, calibration);
    CHECK(c.t1 == 27504u && c.t2 == 26435 && c.t3 == -1000 && c.p1 == 36477u && c.p2 == -10685);
    CHECK(c.p3 == 3024 && c.p4 == 2855 && c.p5 == 140 && c.p6 == -7 && c.p7 == 15500);
    CHECK(c.p8 == -14600 && c.p9 == 6000 && c.h1 == 0x4b);
    fine = bme280_temperature(&c, 519888L, &centi);
    CHECK(fine == 128422L && centi == 2508);
    pa = bme280_pressure(&c, 415148L, fine);
    CHECK(pa >= 100650u && pa <= 100660u);
    /* H4 = E4<<4 | E5[3:0], H5 = E6<<4 | E5[7:4], signed. */
    bme280_calib_humidity(&c, humidity);
    CHECK(c.h2 == 362 && c.h3 == 0 && c.h4 == 0x13f && c.h5 == 0x32 && c.h6 == 30);
    rh = bme280_humidity(&c, 0x6e00L, fine);
    CHECK(rh <= 102400u);
    CHECK(bme280_humidity(&c, 0, fine) == 0);
    humidity_negative(&c);
    CHECK(onewire_crc8(rom, 7) == 0xa2 && onewire_crc8(rom, 8) == 0);

    if (argc <= 1) printf("gpd_frame: %lu checks passed\n", checks);
    return 0;
}
