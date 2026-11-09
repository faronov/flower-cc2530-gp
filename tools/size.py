#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Print the flash use of an Intel HEX image and check that the reset and
Sleep Timer (vector 5, 0x2B) entries are programmed."""
import sys

FLASH = 256 * 1024


def load(path):
    data, base = {}, 0
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line.startswith(":"):
                continue
            raw = bytes.fromhex(line[1:])
            n, addr, kind = raw[0], int.from_bytes(raw[1:3], "big"), raw[3]
            if kind == 0:
                for i, b in enumerate(raw[4:4 + n]):
                    data[base + addr + i] = b
            elif kind == 4:
                base = int.from_bytes(raw[4:6], "big") << 16
            elif kind == 2:
                base = int.from_bytes(raw[4:6], "big") << 4
    return data


def main():
    data = load(sys.argv[1])
    if not data:
        sys.exit("size: empty image")
    top = max(data) + 1
    print(f"{sys.argv[1]}: {len(data)} bytes, top 0x{top:05x}, "
          f"{100 * len(data) / FLASH:.1f}% of 256 KB")
    for addr, name in ((0x00, "reset"), (0x2B, "sleep-timer ISR")):
        if addr not in data:
            sys.exit(f"size: {name} vector 0x{addr:02x} not programmed")
    if top > 0x8000:
        sys.exit("size: image exceeds the 32 KB common area (non-banked build)")


if __name__ == "__main__":
    main()
