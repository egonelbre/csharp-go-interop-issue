#!/usr/bin/env python3
"""Decode the binary log produced by trace_sigaltstack.so.

Usage: decode.py <log> [<maps-file>]

If <maps-file> is given (snapshot of /proc/$PID/maps from the run), the
caller PCs are mapped back to the owning library and offset.
"""
from __future__ import annotations

import struct
import sys
import bisect
from pathlib import Path

# 80 bytes total. Layout matches the C struct rec exactly.
# ts_ns(Q=8) tid(i=4) rc(i=4) errno(i=4) pad(i=4)
# new_sp(Q=8) new_size(Q=8) new_flags(i=4) new_present(i=4)
# old_sp(Q=8) old_size(Q=8) old_flags(i=4) old_present(i=4)
# caller(Q=8)
REC_FMT = "<QiiiiQQiiQQiiQ"
REC_SIZE = struct.calcsize(REC_FMT)
assert REC_SIZE == 80, REC_SIZE


def parse_maps(path: Path):
    rows = []
    for line in path.read_text().splitlines():
        if "-" not in line:
            continue
        rng, *_rest = line.split(maxsplit=5)
        lo, hi = (int(x, 16) for x in rng.split("-"))
        name = line.split(maxsplit=5)[5] if len(line.split(maxsplit=5)) > 5 else ""
        rows.append((lo, hi, name))
    rows.sort()
    return rows


def attr(maps, pc):
    if not maps:
        return ""
    los = [r[0] for r in maps]
    i = bisect.bisect_right(los, pc) - 1
    if i < 0 or pc >= maps[i][1]:
        return ""
    return f" [{Path(maps[i][2]).name or '?'}+0x{pc - maps[i][0]:x}]"


SS_DISABLE = 2
SS_ONSTACK = 1


def flags_str(f):
    parts = []
    if f & SS_ONSTACK:
        parts.append("ONSTACK")
    if f & SS_DISABLE:
        parts.append("DISABLE")
    if not parts:
        parts.append(f"{f}")
    return "+".join(parts)


def main():
    if len(sys.argv) < 2:
        print("usage: decode.py <log> [<maps>]", file=sys.stderr)
        sys.exit(2)
    log = Path(sys.argv[1]).read_bytes()
    maps = parse_maps(Path(sys.argv[2])) if len(sys.argv) >= 3 else []

    n = len(log) // REC_SIZE
    print(f"# {n} records, {len(log)} bytes")
    print(f"# {'ts_us':>10}  {'tid':>6}  rc  | new                              | old (returned)                   | caller")
    t0 = None
    for i in range(n):
        (
            ts_ns, tid, rc, err, _pad,
            new_sp, new_size, new_flags, new_present,
            old_sp, old_size, old_flags, old_present,
            caller,
        ) = struct.unpack_from(REC_FMT, log, i * REC_SIZE)
        if t0 is None:
            t0 = ts_ns
        new_str = (
            f"sp=0x{new_sp:013x} size={new_size:>7d} fl={flags_str(new_flags):<14s}"
            if new_present else " (NULL)" + " " * 36
        )
        old_str = (
            f"sp=0x{old_sp:013x} size={old_size:>7d} fl={flags_str(old_flags):<14s}"
            if old_present else " (NULL)" + " " * 36
        )
        print(f"{(ts_ns - t0) / 1000:>10.1f}  {tid:>6}  {rc:>2}  | {new_str} | {old_str} | 0x{caller:x}{attr(maps, caller)}")


if __name__ == "__main__":
    main()
