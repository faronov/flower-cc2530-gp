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
  voltage (2 × AAA alkaline curve).
- Optional sensors, detected at every start: a BME280 (I²C, SCL P0.5,
  SDA P0.6) for air temperature, humidity and pressure, and a DS18B20
  (1-Wire, P1.3) for soil temperature.

## Radio protocol

- Frame type: Green Power Device Frames, ApplicationID 0 (SrcID), **no
  security**, unidirectional. The MAC sequence number is the frame counter.
- SrcID: the low 32 bits of the chip IEEE address, avoiding the reserved
  values.
- Commissioning frame (0xE0): DeviceID 0x33, manufacturer 0x6D6B, model
  `0xF100 | sensors` (bit 0 BME280, bit 1 DS18B20). Server clusters are
  0x0001, 0x0400, 0x0402 and 0x0408, plus 0x0405 and 0x0403 with a BME280.
- Reports: a Multi-Cluster Reporting frame (0xA2) every 60 s
  (`GPD_INTERVAL`). It carries the following values, all in their ZCL
  encodings:
  - temperature (0x0402), from the BME280 if present, else the chip
    sensor;
  - soil moisture (0x0408);
  - illuminance (0x0400);
  - battery voltage (0x0001/0x0020) and battery remaining (0x0001/0x0021);
  - with a BME280: humidity (0x0405) and pressure (0x0403, hPa);
  - with a DS18B20: soil temperature (0x0402, attribute 0x5000).
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

### Settings

The channel and the sensor set are kept in flash page 125 (0x3E800), as a
log of 4-byte records written by DMA. A chip erase (`cc-tool -e`) therefore
means a first start.

## Pairing with ZHA

1. Copy `quirks/zha_quirk_diyruz_flowerx_gp.py` to
   `<HA config>/custom_zha_quirks/` and restart Home Assistant. This needs a
   ZHA build with Green Power support.
2. Permit joining.
3. Power the sensor (first start), or hold its button for 3 s.

Each accepted Commissioning frame re-adds the device in ZHA while permit
join is open. After pairing, close permit join.

## Flashing

Download `flower-gp.hex` from the
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
```

`LLVM_SRC` must be the [faronov/llvm-project](https://github.com/faronov/llvm-project)
checkout at the revision in `cc2530-zigbee/tools/llvm/toolchain.json`. The
MCS-51 runtime and startup come from it, and `LLVM_TOOLS` must be built
from the same revision (`clang`, `lld`, `llvm-ar`, `llvm-objcopy`; see
`.github/workflows/ci.yml`). The output is
`build/firmware/flower-gp.{hex,elf,map}`.

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
`flower-gp.hex`, `flower-gp.elf`, `SHA256SUMS` and the ZHA quirk as a GitHub
release.

## License

BSD-3-Clause, see [LICENSE](LICENSE). `cc2530-root.ld` is derived from LLVM
(Apache-2.0 WITH LLVM-exception).
