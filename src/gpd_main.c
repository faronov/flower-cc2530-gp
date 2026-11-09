/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
/* FlowerX Green Power Device. No Zigbee join, no stack runtime (only the
 * cc2530-zigbee clock and Sleep Timer helpers): the radio only transmits
 * GPDFs (include/gpd_frame.h, NoSecurity, unidirectional) on GPD_CHANNEL.
 * After power-on it sends a
 * Commissioning GPDF and a report every GPD_COMMISSION_PERIOD seconds for
 * GPD_COMMISSION_ROUNDS rounds (the sink accepts it only while its
 * commissioning window, ZHA permit join, is open), then one Multi-Cluster
 * Reporting GPDF every GPD_INTERVAL seconds. Each GPDF goes out
 * GPD_REPEAT times with the same MAC sequence number (the sink drops the
 * duplicates), GPD_REPEAT_GAP apart. The device sleeps in PM2 between
 * rounds and between repetitions (so a supply capacitor can recharge), and
 * idles the CPU (PM0 idle; Timer 3 still drives the soil probe) while the
 * probe warms up; any bounded wait that expires restarts it through the
 * watchdog. */
#include "flower.h"
#include "clock.h"
#include "timebase.h"

#ifndef GPD_CHANNEL
#define GPD_CHANNEL 15u
#endif
#ifndef GPD_INTERVAL
#define GPD_INTERVAL 60u
#endif
#ifndef GPD_COMMISSION_PERIOD
#define GPD_COMMISSION_PERIOD 10u
#endif
#ifndef GPD_COMMISSION_ROUNDS
#define GPD_COMMISSION_ROUNDS 12u
#endif
#ifndef GPD_REPEAT
#define GPD_REPEAT 3u
#endif
/* Sleep Timer ticks between repetitions: 655 is 20 ms. */
#ifndef GPD_REPEAT_GAP
#define GPD_REPEAT_GAP 655UL
#endif
/* SWRS081B Table 2: F5 is +4.5 dBm. */
#ifndef GPD_TXPOWER
#define GPD_TXPOWER 0xf5u
#endif

/* SWRU191F 16.3 WDCTL, 4.10 SLEEPSTA, 11.2 STLOAD. */
#define SOC_WDCTL_ADDRESS 0xc9u
#define SOC_SLEEPSTA_ADDRESS 0x9du
#define SOC_STLOAD_ADDRESS 0xadu
#define WDCTL_RESET 0x0bu
/* Factory IEEE address in the information page, LSB first (SWRU191F 2.3.3). */
#define INFO_IEEE 0x780cu
/* SWRU191F 23.15 command strobes, RFIRQF1.TXDONE. */
#define RFST_ISTXON 0xe9u
#define RFST_ISFLUSHTX 0xeeu
#define RFST_ISRFOFF 0xefu
#define TXDONE 0x02u
#define SECOND 32768UL
#define WARMUP 1639UL      /* 50 ms */
#define TX_WAIT 164UL      /* 5 ms per GPDF (about 1.6 ms on air) */
#define LED_ON 655UL       /* 20 ms commissioning flash */
#define SPIN_WAIT 4096UL   /* 125 ms for clock and Sleep Timer handshakes */
#define SLEEP_MIN 8UL
/* SWRU191F 4.1 SLEEPCMD.MODE with OSC_PD (unused 16-MHz RCOSC off):
 * PM2, or PM0 where PCON.IDLE only halts the CPU. */
#define SLEEP_PM2 0x06u
#define SLEEP_IDLE 0x04u

/* Read by a debugger: progress and the last failure. */
typedef struct {
    uint8_t stage, failure, rounds, seq;
    uint16_t sent, lost;
    uint8_t src[4];
} gpd_status_t;
MCU_XDATA gpd_status_t gpd_status;

static MCU_XDATA uint8_t frame[GPD_REPORT_LENGTH], ieee[8];
static MCU_XDATA gpd_values_t values;
static MCU_XDATA clock_diagnostics_t clock_diagnostics;
static MCU_XDATA uint32_t deadline, mark;
#define S gpd_status
#define NOW() timebase_read_awake_ticks24()
#define SINCE(start) ((NOW()-(start))&TIMEBASE_TICKS_MASK)

/* Never taken in a correct sequence: the instruction after the wake clears
 * EA before the vector can run (SWRU191F 4.2). A leaf that only clears STIF. */
__attribute__((interrupt(5))) MCU_COMMON_CODE void gpd_sleep_isr(void)
{
    __asm__ volatile("clr 0xc7");
}

__attribute__((noinline)) static void restart(uint8_t failure)
{
    S.failure = failure;
    MMIO_CLEAR(SOC_IEN0, 0x80);
    MMIO_WRITE(SOC_RFST, RFST_ISRFOFF);
    MMIO_WRITE(SOC_WDCTL, WDCTL_RESET);
    for (;;) {}
}

/* SWRU191F 23.15: TX-relevant part of the cc2530-zigbee radio profile
 * (radio_autoack.c), AUTOCRC without AUTOACK, then channel and power. */
static void radio_setup(void)
{
    MMIO_WRITE(SOC_RFST, RFST_ISRFOFF);
    MMIO_XWRITE(0x6189u, 0x40);   /* FRMCTRL0 */
    MMIO_XWRITE(0x61b2u, 0x15);   /* AGCCTRL1 */
    MMIO_XWRITE(0x61fau, 0x09);   /* TXFILTCFG */
    MMIO_XWRITE(0x61aeu, 0x00);   /* FSCAL1 */
    MMIO_XWRITE(0x6191u, 0x69);   /* TXCTRL */
    MMIO_XWRITE(0x618fu, 11u + 5u*(GPD_CHANNEL-11u));   /* FREQCTRL */
    MMIO_XWRITE(0x6190u, GPD_TXPOWER);
}

/* STIF clear; IEN0 = EA|STIE, then PCON.IDLE whose successor must not start
 * on a 4-byte boundary (SWRU191F 4.2); clr EA runs before any ISR. */
__attribute__((noinline)) MCU_COMMON_CODE static void enter_pm2(void)
{
    __asm__ volatile("clr 0xc7\n\t.p2align 2\n\tmov 0xa8,#0xa0\n\tmov 0x87,#1\n\tclr 0xaf" ::: "memory");
}

#define SPIN(condition, code) do { mark = NOW(); \
    while (condition) if (SINCE(mark) >= SPIN_WAIT) restart(code); } while (0)

/* PM2 (or CPU idle) until the Sleep Timer reaches wake (as cc2530-zigbee
 * join_smoke_sleep: compare ST2, ST1, ST0 after STLOAD; after PM2 the XOSC
 * back, then a fresh 32-kHz edge). A wake already due or too close, or an
 * early wake, ends in a busy wait. */
static void sleep_until(uint32_t wake, uint8_t mode)
{
    uint32_t left = (wake-NOW())&TIMEBASE_TICKS_MASK;
    S.stage = 4;
    if (left >= TIMEBASE_TICKS_MASK/2u) return;
    if (left > SLEEP_MIN*2u) {
        SPIN(!(MMIO_READ(SOC_STLOAD) & 1u), 5);
        MMIO_WRITE(SOC_ST2, (uint8_t)(wake >> 16));
        MMIO_WRITE(SOC_ST1, (uint8_t)(wake >> 8));
        MMIO_WRITE(SOC_ST0, (uint8_t)wake);
        if (((wake-NOW())&TIMEBASE_TICKS_MASK) >= SLEEP_MIN) {
            MMIO_WRITE(SOC_SLEEPCMD, mode);
            enter_pm2();
            MMIO_WRITE(SOC_IEN0, 0);
            MMIO_WRITE(SOC_SLEEPCMD, SLEEP_IDLE);
            if (mode == SLEEP_PM2) {
                S.stage = 5;
                SPIN(MMIO_READ(SOC_CLKCONSTA) != MMIO_READ(SOC_CLKCONCMD), 6);
                SPIN(MMIO_READ(SOC_SLEEPSTA) & 1u, 7);
                SPIN(!(MMIO_READ(SOC_SLEEPSTA) & 1u), 8);
            }
        }
    }
    while (((NOW()-wake)&TIMEBASE_TICKS_MASK) >= TIMEBASE_TICKS_MASK/2u) {}
}

static void nap(uint32_t ticks, uint8_t mode)
{
    sleep_until((NOW()+ticks)&TIMEBASE_TICKS_MASK, mode);
}

static void sample(void)
{
    S.stage = 2;
    flower_power(1);
    nap(WARMUP, SLEEP_IDLE);
    flower_measure(&values);
    flower_power(0);
}

/* One GPDF GPD_REPEAT times: TX FIFO length (with FCS) and body, ISTXON
 * (calibrates, then sends without CCA), TXDONE, radio off; PM2 between
 * repetitions, then the radio profile again. */
static void send(uint8_t length)
{
    uint8_t r, i;
    for (r = 0; r < GPD_REPEAT; r++) {
        if (r) {
            nap(GPD_REPEAT_GAP, SLEEP_PM2);
            radio_setup();
        }
        MMIO_WRITE(SOC_RFST, RFST_ISFLUSHTX);
        MMIO_WRITE(SOC_RFD, length + 2u);
        for (i = 0; i < length; i++) MMIO_WRITE(SOC_RFD, frame[i]);
        MMIO_WRITE(SOC_RFIRQF1, (uint8_t)~TXDONE);
        MMIO_WRITE(SOC_RFST, RFST_ISTXON);
        mark = NOW();
        while (!(MMIO_READ(SOC_RFIRQF1) & TXDONE))
            if (SINCE(mark) >= TX_WAIT) break;
        if (MMIO_READ(SOC_RFIRQF1) & TXDONE) S.sent++;
        else S.lost++;
        MMIO_WRITE(SOC_RFIRQF1, (uint8_t)~TXDONE);
        MMIO_WRITE(SOC_RFST, RFST_ISRFOFF);
    }
    S.seq++;
}

void main(void)
{
    uint8_t i;
    uint16_t period;
    flower_startup();
    S.stage = 1;
    if (clock_select_init(CLOCK_XOSC32, 1024, 4096, &clock_diagnostics) != CLOCK_OK) restart(1);
    flower_init();
    for (i = 0; i < 8u; i++) ieee[i] = MMIO_XREAD(INFO_IEEE + i);
    gpd_source(S.src, ieee);
    S.seq = (uint8_t)(ieee[0] ^ MMIO_READ(SOC_ST0));
    deadline = NOW();
    for (;;) {
        sample();
        S.stage = 3;
        radio_setup();
        if (S.rounds < GPD_COMMISSION_ROUNDS) {
            S.rounds++;
            MMIO_SET(SOC_P0, BOARD_FLOWER_LED);
            send(gpd_commissioning(frame, S.seq, S.src));
            nap(GPD_REPEAT_GAP, SLEEP_PM2);
            radio_setup();
            send(gpd_report(frame, S.seq, S.src, &values));
            nap(LED_ON, SLEEP_PM2);
            MMIO_CLEAR(SOC_P0, BOARD_FLOWER_LED);
            period = GPD_COMMISSION_PERIOD;
        } else {
            send(gpd_report(frame, S.seq, S.src, &values));
            period = GPD_INTERVAL;
        }
        deadline = (deadline + (uint32_t)period*SECOND) & TIMEBASE_TICKS_MASK;
        sleep_until(deadline, SLEEP_PM2);
    }
}
