# FlowerX Green Power firmware (CC2530)

Firmware for the DIYRuZ FlowerX soil sensor (CC2530) that works as a
**Zigbee Green Power Device (GPD)**. It never joins a network. Every 60 s it
wakes from PM2, measures, broadcasts one GPDF and goes back to sleep. A Green
Power proxy or sink, such as ZHA with the bundled quirk, receives it.

- Image: about 20 KB, non-banked (the regular Zigbee end-device image is
  about 240 KB).
- Toolchain: LLVM MCS-51 ([faronov/llvm-project](https://github.com/faronov/llvm-project)),
  pinned through the [cc2530-zigbee](https://github.com/faronov/cc2530-zigbee)
  submodule. Only the stack's clock and Sleep Timer helpers are linked.
- Measurement: the same as [flower-cc2530](https://github.com/faronov/flower-cc2530):
  soil probe (Timer 3, 4 MHz), photo resistor, chip temperature, and battery
  voltage. The battery (CR2032 with a 220 µF capacitor) is read at the end
  of the report's transmissions and reported in the next one; the percentage
  is an approximate curve for that TX load, with 0 % at 2.0 V. The first report after a start uses the
  idle voltage.
- Optional sensors, detected at every start: a BME280 (I²C, SCL P0.5,
  SDA P0.6) for air temperature, humidity and pressure, and a DS18B20
  (1-Wire, P1.3, 10 bits, 0.25 °C steps): the air temperature alone, the
  soil temperature next to a BME280.

The same image is also built for the EfektaLab BGT1 board (`BOARD=bgt1`,
`bgt1-gp.hex`), see [EfektaLab BGT1](#efektalab-bgt1).

## Radio protocol

- Frame type: Green Power Device Frames, ApplicationID 0 (SrcID),
  unidirectional, security level 3 (see Security).
- SrcID: the low 32 bits of the chip IEEE address, avoiding the reserved
  values.
- Commissioning frame (0xE0): DeviceID 0x33, manufacturer 0x6D6B, model
  `0xF100 | sensors` (bit 0 BME280, bit 1 DS18B20). Server clusters are
  0x0001, 0x0400, 0x0402 and 0x0408, plus 0x0405 and 0x0403 with a BME280.
- Reports: a Multi-Cluster Reporting frame (0xA2) every 60 s
  (`GPD_INTERVAL`). It carries the following values, all in their ZCL
  encodings:
  - temperature (0x0402), from the BME280 if present, else the DS18B20,
    else the chip sensor (uncalibrated, ±10 °C per SWRS081B);
  - soil moisture (0x0408);
  - illuminance (0x0400);
  - battery voltage (0x0001/0x0020) and battery remaining (0x0001/0x0021);
  - with a BME280: humidity (0x0405) and pressure (0x0403, hPa);
  - with a BME280 and a DS18B20: soil temperature (0x0402, attribute 0x5000).
- Repeats and sleep: each GPDF is sent 3 times, 20 ms apart, with PM2
  between repetitions and between rounds. The CPU idles instead of sleeping
  during the 50-ms probe warm-up, because PM2 would stop Timer 3, which
  drives the soil probe.

### Channel search and commissioning

Commissioning runs on the first start, when the detected sensor set
changes, and after a 3-s button press. It does up to 12 rounds, 10 s apart.
Each round works as follows:

1. **Channel search (GP Basic A.3.9.1).** Up to 6 attempts, 400 ms apart.
   Each attempt sends a Maintenance *Channel Request* (0xE3) on every
   channel, with Auto-Commissioning set. The *hint* channel (the saved one,
   or `GPD_CHANNEL` = 15) goes last, with Auto-Commissioning cleared. The
   device then listens there for 18–40 ms.
2. **Answer.** A GP proxy of the network (the TempMaster) can answer with a
   *Channel Configuration* (0xF3) carrying the operational channel. It does
   so only while the sink is in commissioning mode, which in ZHA means
   permit join is open.
3. **Commissioning.** An answer ends the rounds: one Commissioning frame
   and a report go out on that channel, and the channel is saved. Without an
   answer, the round sends them on the hint channel instead, as the old
   fixed-channel firmware did.

The ZHA coordinator itself cannot answer: bellows/EZSP never sends GP
frames to a GPD. The answer therefore needs a GP proxy, for example an IKEA
TRÅDFRI router, within radio range. Without one, keep the network on
channel 15 or rebuild with another hint:
`make firmware GPD_DEFINES=-DGPD_CHANNEL=20`.

### Button and LED

| Button (P2.0)  | Action |
|----------------|--------|
| short press    | measure and report now (LED flash) |
| hold 3 s       | LED on; on release, search the channel and commission |
| hold 10 s      | LED blinks; on release, send Decommissioning (0xE1), clear the settings and stop reporting |

The button wakes the device from PM2 through the port 2 interrupt.

LED (P0.1) signals:
- one flash per channel-search attempt;
- 1 s on: the channel was found;
- 3 blinks: the channel was not found (the hint channel is used);
- 5 blinks: decommissioned.

### Security

Reports and Decommissioning are sent at GP security level 3 (encrypted,
4-byte MIC, AES-CCM* on the CC2530 AES coprocessor) with an individual
out-of-the-box key (KeyType 4). Channel Requests are unprotected, as the
specification requires.

- Key: 16 random bytes, made on the first commissioning from the radio's
  random bits (RFRND, receiver on) and whitened with AES. It survives
  recommissioning and goes on Decommissioning (a 10-s press) or a chip erase.
  The sink then sees a new key and the device must be paired again.
- Commissioning frame: the key is sent encrypted with the ZigBee
  Alliance default TC link key (`ZigBeeAlliance09`) with its MIC, plus the
  outgoing frame counter, as Zigbee2MQTT (zigbee-herdsman) and ZHA (zigpy)
  expect.
- Frame counter: 32 bits, never reused for a given key. Flash holds a lease of
  1024 counter values; each reboot starts at the next lease.
- Firmware that starts without a key (for example after an upgrade without a
  chip erase) commissions again.
- `GPD_DEFINES=-DGPD_SECURITY=0` builds the unsecured variant.

### Settings

The channel, the sensor set, the key and the frame-counter lease are kept in
flash page 125 (0x3E800), as a log of 4-byte records written by DMA. A chip
erase (`cc-tool -e`) therefore means a first start and a new key.

## EfektaLab BGT1

A CC2530 module with a PA/LNA front end, a DS18B20 and two AAA cells. The
pin map was read from its original Z-Stack firmware:

| Function | Pin |
|---|---|
| LED (active high) | P0.1 |
| Button (active low, pull-up) | P2.0 |
| DS18B20 supply (high while measuring) | P0.2 |
| DS18B20 1-Wire (port 0 internal pulls) | P0.3 |
| PA_EN (radio observation output 0, inverted pa_pd) | P1.1 |
| LNA_EN (radio observation output 1, inverted lna_pd) | P1.4 |
| HGM (high-gain mode, always high) | P0.7 |

- Front end: Z-Stack's CC2590/CC2591 wiring (`paLnaChip` 2). The original
  firmware uses Z-Stack's CC2590 TX power table; this build keeps
  `GPD_TXPOWER` 0xF5, that table's highest setting (about +11 dBm with a
  CC2590, more with a CC2591). Which of the two is fitted is not known.
- Commissioning: DeviceID 0x30 (TemperatureSensor), manufacturer 0x6D6B,
  model `0xF200 | sensors` (bit 1 DS18B20), server clusters 0x0001 and
  0x0402. Reports carry the DS18B20 temperature (11 bits, 0.125 °C steps,
  375 ms conversion; invalid without it, no chip-sensor fallback), battery
  voltage and battery remaining.
- Battery: 0 % at 2.0 V, 100 % from 2.9 V, a rough piecewise curve for two
  alkaline AAA cells; approximate.
- Reports: the sensor wakes every 60 s but transmits only when the
  temperature moved by 0.25 °C or more from the last report, became valid or
  invalid, or after 30 minutes without a report (heartbeat). The button and
  (re)commissioning always report. The battery is sent with these reports.
- Button, LED, channel search and settings work as on the FlowerX.
- ZHA: its own quirk, `quirks/zha_quirk_efektalab_bgt1_gp.py` ("EfektaLab
  BGT1 GP").
- Zigbee2MQTT: external converter `z2m/efektalab_bgt1_gp.mjs`. Copy it to
  `<z2m data>/external_converters/` and restart Zigbee2MQTT. zigbee-herdsman
  names the device `GreenPower_48` after its DeviceID, so the converter
  matches any Green Power TemperatureSensor. Tested against
  zigbee-herdsman's parsing and decryption of this firmware's frames, not
  with a live Zigbee2MQTT.

## Pairing with ZHA

1. Copy `quirks/zha_quirk_diyruz_flowerx_gp.py` (or, for BGT1,
   `quirks/zha_quirk_efektalab_bgt1_gp.py`) to
   `<HA config>/custom_zha_quirks/` and restart Home Assistant. This needs a
   ZHA build with Green Power support.
2. Permit joining.
3. Power the sensor (first start), or hold its button for 3 s.

Each accepted Commissioning frame re-adds the device in ZHA while permit
join is open. After pairing, close permit join.

## Flashing

Download `flower-gp.hex` (or `bgt1-gp.hex`) from the
[latest release](https://github.com/faronov/flower-cc2530-gp/releases/latest)
and write it with a CC Debugger, for example:

```sh
cc-tool -e -w flower-gp.hex -v r --reset
```

## Building

```sh
git clone --recursive https://github.com/faronov/flower-cc2530-gp
cd flower-cc2530-gp
make test                       # frame builder, host (ASan/UBSan)
make test-quirk                 # needs: pip install pytest zha zha-quirks
make firmware LLVM_TOOLS=<llvm-build>/bin LLVM_SRC=<llvm-project>
make firmware BOARD=bgt1 LLVM_TOOLS=<llvm-build>/bin LLVM_SRC=<llvm-project>
```

`LLVM_SRC` must be the [faronov/llvm-project](https://github.com/faronov/llvm-project)
checkout at the revision in `cc2530-zigbee/tools/llvm/toolchain.json`. The
MCS-51 runtime and startup come from it, and `LLVM_TOOLS` must be built
from the same revision (`clang`, `lld`, `llvm-ar`, `llvm-objcopy`; see
`.github/workflows/ci.yml`). The output is
`build/firmware/flower-gp.{hex,elf,map}`, or `build/bgt1/bgt1-gp.{hex,elf,map}`
with `BOARD=bgt1`.

The image is linked with `cc2530-root.ld`. It is LLVM's `cc2530.ld` without
the code bank sections, so every function stays in the 32-KB common area.

The image has no DATA/IDATA globals, so the build patches the startup to
`mov sp, #0x47`. The hardware stack then gets IDATA 0x48–0xFF (184 bytes)
instead of 0x80–0xFF; the deepest path (commissioning → BME280 read) needs
about 144 bytes. After linking, `tools/stack.py` estimates the worst case
from the disassembly and fails the build if it does not fit.

## Measurements

These numbers come from the same GPD logic built inside the full stack image.
Supply current was measured with LP-XDS110ET EnergyTrace at 3.3 V, not
calibrated against a reference load:

- Commissioning round (E0 and A2, 3 times each): about 1027 µC.
- Report round: 534 µC.
  - 390 µC for wake-up, warm-up at about 5.5 mA, measurement and the first
    transmission.
  - 72 µC for each repetition.
- Average with one report every 60 s: about 9 µA.
- Sleep current: not yet measured with a calibrated meter (below 2 µA).

## Releases

Pushing a `v*` tag builds the firmware in CI. The workflow then publishes
`flower-gp.hex`, `flower-gp.elf`, `bgt1-gp.hex`, `bgt1-gp.elf`, `SHA256SUMS`
and both ZHA quirks as a GitHub release.

## License

BSD-3-Clause, see [LICENSE](LICENSE). `cc2530-root.ld` is derived from LLVM
(Apache-2.0 WITH LLVM-exception).
