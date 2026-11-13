/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "settings.h"
#include "board.h"

/* SWRU191F 6.2 flash controller, 8.6 DMA descriptor. */
#define FCTL 0x6270u
#define FADDRL 0x6271u
#define FADDRH 0x6272u
#define FWDATA 0x6273u
#define FCTL_ERASE 0x01u
#define FCTL_WRITE 0x02u
#define FCTL_BUSY 0x80u
#define WORDS 512u
/* The page through the XDATA bank window (MEMCTR.XBANK). */
#define XBANK (SETTINGS_PAGE / 16u)
#define WINDOW (0x8000u + (SETTINGS_PAGE % 16u) * 2048u)

static MCU_XDATA uint8_t record[4];
/* DMA channel 0: record to FWDATA, single byte transfers on the flash
 * trigger (18), source increment, high priority, no interrupt. */
static MCU_XDATA uint8_t descriptor[8];

static uint8_t word(uint16_t index, uint8_t MCU_XDATA *out)
{
    uint8_t memctr = MMIO_READ(SOC_MEMCTR), i, erased = 0xffu;
    MMIO_WRITE(SOC_MEMCTR, (memctr & 0xf8u) | XBANK);
    for (i = 0; i < 4u; i++) {
        out[i] = MMIO_XREAD(WINDOW + index*4u + i);
        erased &= out[i];
    }
    MMIO_WRITE(SOC_MEMCTR, memctr);
    return erased == 0xffu;
}

static void clear(settings_t *s)
{
    uint8_t i;
    s->channel = s->sensors = s->keyed = 0;
    for (i = 0; i < 16u; i++) s->key[i] = 0;
    s->lease = 0;
}

/* Index of the first erased word (WORDS when full); the last valid records
 * before it into s. */
static uint16_t scan(settings_t *s)
{
    uint16_t i;
    uint8_t k;
    clear(s);
    for (i = 0; i < WORDS; i++) {
        if (word(i, record)) break;
        if (record[3] != SETTINGS_CHECK(record[0], record[1], record[2])) continue;
        k = (uint8_t)(record[0] - SETTINGS_KEY);
        if (record[0] == SETTINGS_MAGIC) {
            if (record[1] == 0 || (record[1] >= 11u && record[1] <= 26u)) {
                s->channel = record[1];
                s->sensors = record[2];
            }
        } else if (k < 8u) {
            s->key[2u*k] = record[1];
            s->key[2u*k + 1u] = record[2];
            s->keyed |= (uint8_t)(1u << k);
        } else if (record[0] == SETTINGS_LEASE) {
            s->lease = (uint16_t)(record[1] | (uint16_t)record[2] << 8);
        }
    }
    return i;
}

static void wait_idle(void)
{
    while (MMIO_XREAD(FCTL) & FCTL_BUSY) {}
}

static void erase(void)
{
    wait_idle();
    MMIO_XWRITE(FADDRL, 0);
    MMIO_XWRITE(FADDRH, SETTINGS_PAGE * 2u);
    MMIO_XWRITE(FCTL, MMIO_XREAD(FCTL) | FCTL_ERASE);
    __asm__ volatile("nop");
    wait_idle();
}

static void write(uint16_t index)
{
    uint16_t source = MMIO_XADDRESS(record), address = SETTINGS_PAGE * 512u + index;
    descriptor[0] = (uint8_t)(source >> 8); descriptor[1] = (uint8_t)source;
    descriptor[2] = (uint8_t)(FWDATA >> 8); descriptor[3] = (uint8_t)FWDATA;
    descriptor[4] = 0; descriptor[5] = 4;
    descriptor[6] = 0x12u; descriptor[7] = 0x42u;
    wait_idle();
    MMIO_WRITE(SOC_DMA0CFGH, (uint8_t)(MMIO_XADDRESS(descriptor) >> 8));
    MMIO_WRITE(SOC_DMA0CFGL, (uint8_t)MMIO_XADDRESS(descriptor));
    MMIO_WRITE(SOC_DMAIRQ, (uint8_t)~1u);
    MMIO_WRITE(SOC_DMAARM, 1);
    MMIO_XWRITE(FADDRL, (uint8_t)address);
    MMIO_XWRITE(FADDRH, (uint8_t)(address >> 8));
    MMIO_XWRITE(FCTL, MMIO_XREAD(FCTL) | FCTL_WRITE);
    wait_idle();
}

void settings_load(settings_t *s)
{
    (void)scan(s);
}

static MCU_XDATA settings_t old;
static MCU_XDATA uint16_t next;

static void put(uint8_t magic, uint8_t b1, uint8_t b2)
{
    record[0] = magic; record[1] = b1; record[2] = b2;
    record[3] = SETTINGS_CHECK(magic, b1, b2);
    write(next++);
}

/* The records whose values differ from old: at most 10. */
void settings_save(const settings_t *s)
{
    uint8_t k;
    next = scan(&old);
    if (next > WORDS - 10u) { erase(); next = 0; clear(&old); }
    if (old.channel != s->channel || old.sensors != s->sensors)
        put(SETTINGS_MAGIC, s->channel, s->sensors);
    for (k = 0; k < 8u; k++)
        if ((s->keyed >> k & 1u) && (!(old.keyed >> k & 1u) || old.key[2u*k] != s->key[2u*k] ||
                                     old.key[2u*k + 1u] != s->key[2u*k + 1u]))
            put((uint8_t)(SETTINGS_KEY + k), s->key[2u*k], s->key[2u*k + 1u]);
    if (old.lease != s->lease)
        put(SETTINGS_LEASE, (uint8_t)s->lease, (uint8_t)(s->lease >> 8));
}

void settings_clear(void)
{
    erase();
}
