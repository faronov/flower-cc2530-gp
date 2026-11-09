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
    uint8_t src[4], f[64], n, z[4] = {0}, high[4] = {0xfe, 0xff, 0xff, 0xff};
    gpd_values_t v = { -500, 10000, 0xffffu, 30, 200 };
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
    n = gpd_commissioning(f, 0x42, src);
    CHECK(n == GPD_COMMISSIONING_LENGTH && n == sizeof(commissioning));
    CHECK(!memcmp(f, commissioning, n) && f[n] == 0xaa);
    if (argc > 1) hex("commissioning", f, n);

    memset(f, 0xaa, sizeof(f));
    n = gpd_report(f, 0x43, src, &v);
    CHECK(n == GPD_REPORT_LENGTH && n == sizeof(report));
    CHECK(!memcmp(f, report, n) && f[n] == 0xaa);
    /* PSDU with the FCS within aMaxPHYPacketSize. */
    CHECK(GPD_REPORT_LENGTH + 2u <= 127u);
    if (argc > 1) hex("report", f, n);

    if (argc <= 1) printf("gpd_frame: %lu checks passed\n", checks);
    return 0;
}
