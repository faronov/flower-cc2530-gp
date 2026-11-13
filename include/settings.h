/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef FLOWER_SETTINGS_H
#define FLOWER_SETTINGS_H
/* Persistent settings in one 2-KiB flash page (CC2530F256 page
 * SETTINGS_PAGE, below Z-Stack's NV and the lock-bit page): a log of 4-byte
 * records {magic, b1, b2, ~(magic ^ b1 ^ b2)} appended in the first erased
 * word, the last valid one of each magic wins: SETTINGS_MAGIC {channel,
 * sensors}, SETTINGS_KEY + i {key[2i], key[2i+1]} (i 0..7), SETTINGS_LEASE
 * {lease, LSB first}. A full page is erased and the current values written
 * again; settings_clear erases it. A chip erase (cc-tool -e) is a first
 * start. */
#include <stdint.h>

#ifndef SETTINGS_PAGE
#define SETTINGS_PAGE 125u
#endif
#define SETTINGS_MAGIC 0xa5u
#define SETTINGS_KEY 0xb0u
#define SETTINGS_LEASE 0xc5u
#define SETTINGS_CHECK(magic, b1, b2) ((uint8_t)~((magic) ^ (b1) ^ (b2)))

typedef struct {
    uint8_t channel;   /* 11..26, 0 unknown */
    uint8_t sensors;   /* GPD_HAS_* seen so far */
    uint8_t keyed;     /* bit i: key[2i..2i+1] stored, FF a whole key */
    uint8_t key[16];   /* GP security key */
    uint16_t lease;    /* security frame counters below lease * 1024 may be used */
} settings_t;

/* Interrupts off (CPU stalls during flash operations, SWRU191F 6.2). */
void settings_load(settings_t *s);
void settings_save(const settings_t *s);
void settings_clear(void);

#endif
