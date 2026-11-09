/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef FLOWER_SETTINGS_H
#define FLOWER_SETTINGS_H
/* Persistent settings in one 2-KiB flash page (CC2530F256 page
 * SETTINGS_PAGE, below Z-Stack's NV and the lock-bit page): a log of 4-byte
 * records {magic, channel, sensors, check} appended in the first erased
 * word, the last valid one wins; the page is erased when full or on
 * settings_clear. A chip erase (cc-tool -e) is a first start. */
#include <stdint.h>

#ifndef SETTINGS_PAGE
#define SETTINGS_PAGE 125u
#endif
#define SETTINGS_MAGIC 0xa5u
#define SETTINGS_CHECK(channel, sensors) ((uint8_t)~(SETTINGS_MAGIC ^ (channel) ^ (sensors)))

typedef struct {
    uint8_t channel;   /* 11..26, 0 unknown */
    uint8_t sensors;   /* GPD_HAS_* seen so far */
} settings_t;

/* Interrupts off (CPU stalls during flash operations, SWRU191F 6.2). */
void settings_load(settings_t *s);
void settings_save(const settings_t *s);
void settings_clear(void);

#endif
