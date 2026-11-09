# FlowerX Green Power firmware (CC2530)

Firmware for the DIYRuZ FlowerX soil sensor (CC2530) that works as a
**Zigbee Green Power Device (GPD)**. It never joins a network. Every 60 s it
wakes from PM2, measures, broadcasts one GPDF and goes back to sleep. A Green
Power proxy or sink, such as ZHA with the bundled quirk, receives it.

- Image: about 9.5 KB, non-banked (the regular Zigbee end-device image is
  about 240 KB).
- Toolchain: LLVM MCS-51 ([faronov/llvm-project](https://github.com/faronov/llvm-project)),
  pinned through the [cc2530-zigbee](https://github.com/faronov/cc2530-zigbee)
  submodule. Only the stack's clock and Sleep Timer helpers are linked.
- Measurement: the same as [flower-cc2530](https://github.com/faronov/flower-cc2530):
  soil probe (Timer 3, 4 MHz), photo resistor, chip temperature, and battery
  voltage (2 × AAA alkaline curve).

## Radio protocol

- Frame type: Green Power Device Frames, ApplicationID 0 (SrcID), **no
  security**, unidirectional. The MAC sequence number is the frame counter.
- Channel: **15**, fixed (`GPD_CHANNEL` in `src/gpd_main.c`).
- SrcID: the low 32 bits of the chip IEEE address, avoiding the reserved
  values.
- Commissioning: for the first 12 rounds after power-on, every 10 s, it sends
  a Commissioning frame (0xE0) followed by a report. The Commissioning frame
  uses DeviceID 0x33, manufacturer 0x6D6B, model 0xF100, and server clusters
  0x0001, 0x0400, 0x0402 and 0x0408.
- Reports: after commissioning, a Multi-Cluster Reporting frame (0xA2) every
  60 s (`GPD_INTERVAL`). It carries temperature (0x0402), soil moisture
  (0x0408), illuminance (0x0400), battery voltage (0x0001/0x0020) and battery
  remaining (0x0001/0x0021), all in their ZCL encodings.
- Repeats and sleep: each GPDF is sent 3 times, 20 ms apart, with PM2 between
  repetitions and between rounds. During the 50-ms probe warm-up the CPU
  idles, because PM2 would stop Timer 3, which drives the soil probe.

## Pairing with ZHA

1. Copy `quirks/zha_quirk_diyruz_flowerx_gp.py` to
   `<HA config>/custom_zha_quirks/` and restart Home Assistant. This needs a
   ZHA build with Green Power support.
2. Put the coordinator network on channel 15, or rebuild with another
   channel: `make firmware GPD_DEFINES=-DGPD_CHANNEL=20`.
3. Permit joining, then power the sensor within two minutes.

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
