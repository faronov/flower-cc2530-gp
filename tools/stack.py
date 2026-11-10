#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Estimate the worst-case hardware stack of the image from its llvm-objdump
disassembly and check it against the IDATA from the reset SP to 0xFF.

Per function: pushes/pops, SP adjustments (mov a,SP; add a,#n; mov SP,a)
and the registers pushed by the __mcs51_save_* helpers, in code order; a
call adds its return address and the callee's depth. Every path through
main, plus one interrupt (all at one priority level, so they do not nest).
An estimate: branches are taken in code order, misdecoded bytes may add
calls (over-estimates), and there are no indirect calls or recursion in
this image."""
import bisect
import functools
import re
import sys

SP = 0x81
LABEL = re.compile(r"^([0-9a-f]+) <([^>]+)>:")
CALL = re.compile(r"\t[la]call (\d+)")
JUMP = re.compile(r"\tljmp (\d+)")
ADJUST = re.compile(r"\tadd a, #(\d+)")


def parse(text):
    starts, names, bodies, current = [], [], {}, None
    for line in text.splitlines():
        m = LABEL.match(line)
        if m and not (m.group(2).startswith(".L") and current):
            current = m.group(2)
            starts.append(int(m.group(1), 16))
            names.append(current)
            bodies[current] = []
        elif current:
            bodies[current].append(line)
    return starts, names, bodies


def main():
    disassembly, reset_sp, isrs = sys.argv[1], int(sys.argv[2], 0), sys.argv[3:]
    with open(disassembly) as f:
        starts, names, bodies = parse(f.read())

    def owner(address):
        return names[bisect.bisect_right(starts, address) - 1]

    helper = {}
    for name, body in bodies.items():
        if name.startswith("__mcs51_save_"):
            helper[name] = 0
            for line in body:
                if "jmp @a+dptr" in line:
                    break
                helper[name] += "\tpush " in line

    @functools.lru_cache(None)
    def depth(name, path=()):
        if name in path:
            sys.exit(f"stack: recursion through {'>'.join(path + (name,))}")
        run = peak = 0
        body = bodies.get(name, [])
        for i, line in enumerate(body):
            run += ("\tpush " in line) - ("\tpop " in line)
            m = ADJUST.search(line)
            if (m and i and f"mov a, {SP}" in body[i - 1] and i + 1 < len(body)
                    and f"mov {SP}, a" in body[i + 1]):
                n = int(m.group(1))
                run += n if n < 128 else n - 256
            m = CALL.search(line)
            if m:
                callee = owner(int(m.group(1)))
                if callee in helper:
                    run += helper[callee]
                elif callee != name:  # a call into itself is misdecoded data
                    peak = max(peak, run + 2 + depth(callee, path + (name,)))
            m = JUMP.search(line)
            if m:
                callee = owner(int(m.group(1)))
                if callee != name and not callee.startswith(("__mcs51_restore", "__mcs51_return")):
                    peak = max(peak, run + depth(callee, path + (name,)))
            peak = max(peak, run)
        return peak

    need = 2 + depth("main") + max((2 + depth(isr) for isr in isrs), default=0)
    room = 0xFF - reset_sp
    print(f"stack: ~{need} of {room} bytes (SP 0x{reset_sp:02x})")
    if need > room:
        sys.exit("stack: the estimated worst case overflows IDATA")


if __name__ == "__main__":
    main()
