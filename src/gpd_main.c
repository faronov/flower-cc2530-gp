/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
/* FlowerX Green Power Device. No Zigbee join, no stack runtime (only the
 * cc2530-zigbee clock and Sleep Timer helpers): GPDFs (include/gpd_frame.h,
 * NoSecurity, unidirectional commissioning) on one channel.
 *
 * Channel: with no saved channel (first start), a changed sensor set or a
 * 3-s button press the device commissions: each of up to
 * GPD_COMMISSION_ROUNDS rounds (GPD_COMMISSION_PERIOD apart) first searches
 * the channel with GP Channel Requests (A.3.9.1: Auto-Commissioning 1 on
 * every channel, then 0 on the hint channel and a receive window there; a
 * GP proxy of the network, acting as TempMaster while the sink's
 * commissioning window, ZHA permit join, is open, answers with a Channel
 * Configuration on the hint channel). An answer ends the rounds: one
 * Commissioning GPDF and a report on that channel, which is saved in flash
 * (settings.h). Without an answer (no proxy, or permit join still closed)
 * the round sends them on the hint channel (the saved one or GPD_CHANNEL)
 * instead, as the earlier fixed-channel firmware did.
 *
 * Then one Multi-Cluster Reporting GPDF every GPD_INTERVAL seconds. Each
 * GPDF goes out GPD_REPEAT times with the same MAC sequence number (the sink
 * drops the duplicates), GPD_REPEAT_GAP apart. The device sleeps in PM2
 * between rounds and repetitions and idles the CPU while the soil probe
 * warms up; any bounded wait that expires restarts it through the watchdog.
 *
 * Button (P2.0, wakes PM2): short press reports now; held 3 s (LED on)
 * commissions; held 10 s (LED blinks) sends a Decommissioning GPDF, clears
 * the settings and stops reporting until the next commissioning. LED: a
 * flash per channel search attempt, 1 s on when a channel was found, three
 * blinks when not. */
#include "flower.h"
#include "sensors.h"
#include "settings.h"
#include "clock.h"
#include "timebase.h"

/* The hint: the channel searched first and used when no proxy answers. */
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
/* Channel search attempts per round (the TempMaster needs one attempt to
 * be told by the sink, then waits up to 5 s on the hint channel) and the
 * Sleep Timer ticks between them. */
#ifndef GPD_SEARCH_ATTEMPTS
#define GPD_SEARCH_ATTEMPTS 6u
#endif
#ifndef GPD_SEARCH_GAP
#define GPD_SEARCH_GAP 13107UL     /* 400 ms */
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
/* SWRU191F 23.15 command strobes, RFIRQF0.RXPKTDONE, RFIRQF1.TXDONE,
 * FRMFILT0 (frame filtering off), RXFIFOCNT. */
#define RFST_ISRXON 0xe3u
#define RFST_ISTXON 0xe9u
#define RFST_ISFLUSHRX 0xedu
#define RFST_ISFLUSHTX 0xeeu
#define RFST_ISRFOFF 0xefu
#define RXPKTDONE 0x40u
#define TXDONE 0x02u
#define FRMFILT0 0x6180u
#define RXFIFOCNT 0x619bu
/* SWRU191F 7.10 port 2 interrupt: PICTL.P2ICONL falling edge, IEN2.P2IE. */
#define P2ICONL 0x08u
#define P2IE 0x02u
#define SECOND 32768UL
#define WARMUP 1639UL      /* 50 ms */
#define TX_WAIT 164UL      /* 5 ms per GPDF (about 1.6 ms on air) */
#define LED_ON 655UL       /* 20 ms flash */
#define BLINK 4915UL       /* 150 ms */
#define SPIN_WAIT 4096UL   /* 125 ms for clock and Sleep Timer handshakes */
/* Receive window after the start of the last Channel Request: the
 * TempMaster answers 20..25 ms after it (gpTxOffset, gpMaxTxOffsetVariation)
 * within gpTxDuration (10 ms). */
#define RX_OPEN 590UL      /* 18 ms */
#define RX_CLOSE 1311UL    /* 40 ms */
#define RX_FRAME 164UL     /* 5 ms for a frame being received to complete */
#define BUTTON_DEBOUNCE 655UL           /* 20 ms */
#define BUTTON_POLL 1638UL              /* 50 ms */
#define BUTTON_COMMISSION (3UL*SECOND)
#define BUTTON_RESET (10UL*SECOND)
#define BUTTON_STUCK (20UL*SECOND)
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
    uint8_t channel, sensors, found, presses;
} gpd_status_t;
MCU_XDATA gpd_status_t gpd_status;

static MCU_XDATA uint8_t frame[GPD_REPORT_LENGTH], ieee[8], rx[128];
static MCU_XDATA uint8_t paired, armed;
static MCU_XDATA gpd_values_t values;
static MCU_XDATA settings_t settings;
static MCU_XDATA clock_diagnostics_t clock_diagnostics;
static MCU_XDATA uint32_t deadline, mark;
static volatile MCU_XDATA uint16_t polls;
#define S gpd_status
#define NOW() timebase_read_awake_ticks24()
#define SINCE(start) ((NOW()-(start))&TIMEBASE_TICKS_MASK)
#define LATER(ticks) ((NOW()+(ticks))&TIMEBASE_TICKS_MASK)
#define BUTTON_DOWN() (!(MMIO_READ(SOC_P2) & BOARD_FLOWER_BUTTON))

/* Never taken in a correct sequence: the instruction after the wake clears
 * EA before the vector can run (SWRU191F 4.2). Leaves that only clear the
 * Sleep Timer flag (STIF) and the button flags (P2IFG.0, then P2IF). */
__attribute__((interrupt(5))) MCU_COMMON_CODE void gpd_sleep_isr(void)
{
    __asm__ volatile("clr 0xc7");
}

__attribute__((interrupt(6))) MCU_COMMON_CODE void gpd_button_isr(void)
{
    __asm__ volatile("anl 0x8b,#0xfe\n\tclr 0xe8");
}

__attribute__((noinline)) static void restart(uint8_t failure)
{
    S.failure = failure;
    MMIO_CLEAR(SOC_IEN0, 0x80);
    MMIO_WRITE(SOC_RFST, RFST_ISRFOFF);
    MMIO_WRITE(SOC_WDCTL, WDCTL_RESET);
    for (;;) {}
}

/* A press counts once the button was seen released (a stuck or still held
 * button is not a new press). */
static uint8_t pressed(void)
{
    if (!BUTTON_DOWN()) {
        armed = 1;
        return 0;
    }
    return armed;
}

static void led(uint8_t on)
{
    if (on) MMIO_SET(SOC_P0, BOARD_FLOWER_LED);
    else MMIO_CLEAR(SOC_P0, BOARD_FLOWER_LED);
}

/* SWRU191F 23.15: the cc2530-zigbee radio profile (radio_autoack.c),
 * AUTOCRC without AUTOACK, no frame filtering, then channel and power. */
static void radio_setup(uint8_t channel)
{
    MMIO_WRITE(SOC_RFST, RFST_ISRFOFF);
    MMIO_XWRITE(FRMFILT0, 0x0c);
    MMIO_XWRITE(0x6189u, 0x40);   /* FRMCTRL0 */
    MMIO_XWRITE(0x61b2u, 0x15);   /* AGCCTRL1 */
    MMIO_XWRITE(0x61fau, 0x09);   /* TXFILTCFG */
    MMIO_XWRITE(0x61aeu, 0x00);   /* FSCAL1 */
    MMIO_XWRITE(0x6191u, 0x69);   /* TXCTRL */
    MMIO_XWRITE(0x618fu, 11u + 5u*(channel-11u));   /* FREQCTRL */
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

/* Until a positive 32-kHz edge after a PM2 wake the Sleep Timer still reads
 * its value from before the sleep (SWRU191F 11.4), so a SPIN deadline there
 * expired at once after a 10 s or 60 s sleep and restarted every report.
 * These waits are bounded by a poll count instead (as join_smoke). */
#define POLL(condition, code) do { polls = 0; \
    while (condition) if (!++polls) restart(code); } while (0)

/* PM2 (or CPU idle) until the Sleep Timer reaches wake (as cc2530-zigbee
 * join_smoke_sleep: compare ST2, ST1, ST0 after STLOAD; after PM2 the XOSC
 * back, then a fresh 32-kHz edge). A wake too close ends in a busy wait; a
 * button wake sleeps again, or with on_button returns 1 for a press. */
static uint8_t sleep_until(uint32_t wake, uint8_t mode, uint8_t on_button)
{
    uint32_t left;
    for (;;) {
        S.stage = 4;
        left = (wake-NOW())&TIMEBASE_TICKS_MASK;
        if (left >= TIMEBASE_TICKS_MASK/2u) return 0;
        if (on_button && pressed()) return 1;
        if (left <= SLEEP_MIN*2u) continue;
        SPIN(!(MMIO_READ(SOC_STLOAD) & 1u), 5);
        MMIO_WRITE(SOC_ST2, (uint8_t)(wake >> 16));
        MMIO_WRITE(SOC_ST1, (uint8_t)(wake >> 8));
        MMIO_WRITE(SOC_ST0, (uint8_t)wake);
        if (((wake-NOW())&TIMEBASE_TICKS_MASK) < SLEEP_MIN) continue;
        MMIO_CLEAR(SOC_P2IFG, BOARD_FLOWER_BUTTON);
        MMIO_CLEAR(SOC_IRCON2, 0x01u);
        if (on_button && pressed()) return 1;
        MMIO_WRITE(SOC_SLEEPCMD, mode);
        enter_pm2();
        MMIO_WRITE(SOC_IEN0, 0);
        MMIO_WRITE(SOC_SLEEPCMD, SLEEP_IDLE);
        if (mode == SLEEP_PM2) {
            S.stage = 5;
            POLL(MMIO_READ(SOC_CLKCONSTA) != MMIO_READ(SOC_CLKCONCMD), 6);
            POLL(MMIO_READ(SOC_SLEEPSTA) & 1u, 7);
            POLL(!(MMIO_READ(SOC_SLEEPSTA) & 1u), 8);
        }
    }
}

static void nap(uint32_t ticks, uint8_t mode)
{
    (void)sleep_until(LATER(ticks), mode, 0);
}

static void blink(uint8_t n)
{
    while (n--) {
        led(1); nap(BLINK, SLEEP_PM2);
        led(0); nap(BLINK, SLEEP_PM2);
    }
}

/* Rail on and warmed up (CPU idle: Timer 3 drives the soil probe), sensor
 * conversions started, ADC, BME280 result, excitation off, PM2 until the
 * DS18B20 conversion is done, rail off. A valid BME280 temperature, else a
 * valid DS18B20 one, replaces the die's; next to a BME280 the DS18B20 is
 * the soil temperature. With detect, sensors that answer join S.sensors. */
static void sample(uint8_t detect)
{
    uint8_t bme, ds;
    int16_t die;
    uint32_t started;
    S.stage = 2;
    flower_power(1);
    nap(WARMUP, SLEEP_IDLE);
    if (detect) S.sensors |= sensors_detect();
    bme = (S.sensors & GPD_HAS_BME280) && bme280_start();
    ds = (S.sensors & GPD_HAS_DS18B20) && ds18b20_start();
    started = NOW();
    flower_measure(&values);
    values.humidity = FLOWER_INVALID_U16;
    values.pressure = values.soil_temperature = (int16_t)FLOWER_INVALID_S16;
    if (bme) {
        die = values.temperature;
        bme280_read(&values);
        if (values.temperature == (int16_t)FLOWER_INVALID_S16) values.temperature = die;
    }
    flower_excitation_off();
    if (ds) {
        (void)sleep_until((started + DS18B20_CONVERSION) & TIMEBASE_TICKS_MASK, SLEEP_PM2, 0);
        ds18b20_read(&values);
        if (!GPD_HAS_SOIL_TEMPERATURE(S.sensors)) {
            if (values.soil_temperature != (int16_t)FLOWER_INVALID_S16)
                values.temperature = values.soil_temperature;
            values.soil_temperature = (int16_t)FLOWER_INVALID_S16;
        }
    }
    flower_power(0);
}

/* One GPDF: TX FIFO length (with FCS) and body, ISTXON (calibrates, then
 * sends without CCA), TXDONE, radio off. */
static void transmit(uint8_t length)
{
    uint8_t i;
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

/* One GPDF GPD_REPEAT times; PM2 between repetitions, then the radio
 * profile again. */
static void send(uint8_t length)
{
    uint8_t r;
    for (r = 0; r < GPD_REPEAT; r++) {
        if (r) {
            nap(GPD_REPEAT_GAP, SLEEP_PM2);
            radio_setup(S.channel);
        }
        transmit(length);
    }
    S.seq++;
}

/* Receive from RX_OPEN to RX_CLOSE after start: the operational channel of
 * a Channel Configuration with a good FCS, or 0. */
static uint8_t listen(uint32_t start)
{
    uint8_t found = 0, n, i;
    nap((RX_OPEN - SINCE(start)) & TIMEBASE_TICKS_MASK, SLEEP_IDLE);
    MMIO_WRITE(SOC_RFST, RFST_ISFLUSHRX);
    MMIO_WRITE(SOC_RFST, RFST_ISFLUSHRX);
    MMIO_WRITE(SOC_RFIRQF0, (uint8_t)~RXPKTDONE);
    MMIO_WRITE(SOC_RFST, RFST_ISRXON);
    while (!found && SINCE(start) < RX_CLOSE) {
        if (!(MMIO_READ(SOC_RFIRQF0) & RXPKTDONE)) continue;
        MMIO_WRITE(SOC_RFIRQF0, (uint8_t)~RXPKTDONE);
        while (!found && MMIO_XREAD(RXFIFOCNT)) {
            n = MMIO_READ(SOC_RFD) & 0x7fu;
            mark = NOW();
            while (MMIO_XREAD(RXFIFOCNT) < n)
                if (SINCE(mark) >= RX_FRAME) break;
            if (!n || MMIO_XREAD(RXFIFOCNT) < n) {
                MMIO_WRITE(SOC_RFST, RFST_ISFLUSHRX);
                MMIO_WRITE(SOC_RFST, RFST_ISFLUSHRX);
                break;
            }
            for (i = 0; i < n; i++) rx[i] = MMIO_READ(SOC_RFD);
            if (rx[n-1u] & 0x80u) found = gpd_channel_configuration(rx, n);
        }
    }
    MMIO_WRITE(SOC_RFST, RFST_ISRFOFF);
    MMIO_WRITE(SOC_RFST, RFST_ISFLUSHRX);
    MMIO_WRITE(SOC_RFST, RFST_ISFLUSHRX);
    return found;
}

/* GPD_SEARCH_ATTEMPTS Channel Request bursts announcing hint as the receive
 * channel: Auto-Commissioning 1 on every other channel, 0 on hint, then the
 * receive window there. The found channel, or 0. */
static uint8_t search(uint8_t hint)
{
    uint8_t attempt, channel, found;
    uint32_t start;
    for (attempt = 0; attempt < GPD_SEARCH_ATTEMPTS; attempt++) {
        if (attempt) nap(GPD_SEARCH_GAP, SLEEP_PM2);
        led(1);
        for (channel = 11u; channel <= 26u; channel++) {
            if (channel == hint) continue;
            radio_setup(channel);
            transmit(gpd_channel_request(frame, S.seq++, 1, hint));
        }
        radio_setup(hint);
        start = NOW();
        transmit(gpd_channel_request(frame, S.seq++, 0, hint));
        found = listen(start);
        led(0);
        if (found) return found;
    }
    return 0;
}

static void report(void)
{
    radio_setup(S.channel);
    send(gpd_report(frame, S.seq, S.src, &values, S.sensors));
}

/* Rounds of search, then commissioning and a report; the channel found is
 * saved with the sensor set. A button press ends the rounds. */
static void commission(void)
{
    uint8_t hint = settings.channel ? settings.channel : GPD_CHANNEL, found = 0;
    sample(1);
    for (S.rounds = 0; S.rounds < GPD_COMMISSION_ROUNDS && !found; ) {
        S.rounds++;
        deadline = LATER((uint32_t)GPD_COMMISSION_PERIOD*SECOND);
        found = search(hint);
        S.channel = found ? found : hint;
        radio_setup(S.channel);
        led(1);
        send(gpd_commissioning(frame, S.seq, S.src, S.sensors));
        nap(GPD_REPEAT_GAP, SLEEP_PM2);
        report();
        led(0);
        if (!found && S.rounds < GPD_COMMISSION_ROUNDS && sleep_until(deadline, SLEEP_PM2, 1)) break;
    }
    if (found) {
        S.found++;
        settings.channel = found;
        led(1); nap(SECOND, SLEEP_PM2); led(0);
    } else {
        blink(3);
    }
    settings.sensors = S.sensors;
    settings_save(&settings);
    paired = 1;
    deadline = LATER((uint32_t)GPD_INTERVAL*SECOND);
}

static void decommission(void)
{
    radio_setup(S.channel);
    send(gpd_decommissioning(frame, S.seq, S.src));
    settings_clear();
    settings.channel = settings.sensors = 0;
    S.sensors = 0;
    paired = 0;
    blink(5);
}

/* After debouncing: LED on from 3 s, blinking from 10 s; the action on
 * release. A press held over 20 s is ignored. */
static void button(void)
{
    uint32_t down;
    uint8_t level = 0;
    armed = 0;
    nap(BUTTON_DEBOUNCE, SLEEP_IDLE);
    if (!BUTTON_DOWN()) return;
    S.presses++;
    down = NOW();
    while (BUTTON_DOWN()) {
        if (SINCE(down) >= BUTTON_STUCK) { led(0); return; }
        if (SINCE(down) >= BUTTON_RESET) { level = 2; led(!(MMIO_READ(SOC_P0) & BOARD_FLOWER_LED)); }
        else if (SINCE(down) >= BUTTON_COMMISSION) { level = 1; led(1); }
        nap(BUTTON_POLL, SLEEP_PM2);
    }
    led(0);
    armed = 1;
    if (level == 2) {
        decommission();
    } else if (level == 1) {
        commission();
    } else if (paired) {
        sample(0);
        report();
        led(1); nap(LED_ON, SLEEP_PM2); led(0);
    }
}

void main(void)
{
    uint8_t i;
    flower_startup();
    S.stage = 1;
    if (clock_select_init(CLOCK_XOSC32, 1024, 4096, &clock_diagnostics) != CLOCK_OK) restart(1);
    flower_init();
    MMIO_SET(SOC_PICTL, P2ICONL);
    MMIO_SET(SOC_P2IEN, BOARD_FLOWER_BUTTON);
    MMIO_SET(SOC_IEN2, P2IE);
    for (i = 0; i < 8u; i++) ieee[i] = MMIO_XREAD(INFO_IEEE + i);
    gpd_source(S.src, ieee);
    S.seq = (uint8_t)(ieee[0] ^ MMIO_READ(SOC_ST0));
    settings_load(&settings);
    S.sensors = settings.sensors;
    S.channel = settings.channel ? settings.channel : GPD_CHANNEL;
    sample(1);
    if (!settings.channel || S.sensors != settings.sensors) {
        commission();
    } else {
        paired = 1;
        report();
        deadline = LATER((uint32_t)GPD_INTERVAL*SECOND);
    }
    for (;;) {
        if (sleep_until(deadline, SLEEP_PM2, 1)) {
            button();
            continue;
        }
        if (paired) {
            sample(0);
            report();
        }
        deadline = (deadline + (uint32_t)GPD_INTERVAL*SECOND) & TIMEBASE_TICKS_MASK;
    }
}
