#!/usr/bin/env python3
"""Checks the C race loop against the original when their timing differs.

In native mode the race program (src/port/program.c) runs in C and takes no CPU time, so a
race loads a few frames sooner than with the original's glue code. Everything else should be
the same, just earlier. This runs a recording twice in native mode:

  reference: the routines that wait for interrupts run emulated (TG_EMULATE_WAITING=1);
  C:         they run in C, with --race-frames 2 (the original's pace with no CPU time).

and compares the pictures frame by frame. Where the C run gets ahead, it moves the
recording's later input earlier by the same number of frames and runs it again, until
every frame matches (exit 0) or the two differ in some other way (exit 1).

  tools/shift_compare.py --bin build/tg_headless --rom ROM --input tests/inputs/race_1p.txt --frames 11000
"""

import argparse
import os
import subprocess
import sys
import tempfile

WINDOW = 30  # frames that must match after a shift
MAX_SHIFT = 30


def run(args, script, hashes, emulate):
    env = dict(os.environ)
    cmd = [args.bin, "--rom", args.rom, "--port", "native", "--frames", str(args.frames),
           "--input", script, "--frame-hashes", hashes]
    if emulate:
        env["TG_EMULATE_WAITING"] = "1"
    else:
        cmd += ["--race-frames", "2"]
    subprocess.run(cmd, env=env, stdout=subprocess.DEVNULL, check=True)
    with open(hashes) as f:
        return [line.strip() for line in f]


def read_script(path):
    header, events = [], []
    with open(path) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                header.append(line)
            else:
                frame, rest = line.split(None, 1)
                events.append((int(frame), rest))
    return header, events


def write_script(path, header, events, shifts):
    # shifts: (reference frame, frames earlier) in order; each applies from that frame on.
    with open(path, "w") as f:
        f.writelines(header)
        out = []
        for frame, rest in events:
            d = 0
            for start, delta in shifts:
                if frame >= start:
                    d = delta
            out.append((max(0, frame - d), rest))
        for frame, rest in sorted(out, key=lambda e: e[0]):
            f.write(f"{frame} {rest}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bin", required=True)
    p.add_argument("--rom", required=True)
    p.add_argument("--input", required=True)
    p.add_argument("--frames", type=int, required=True)
    p.add_argument("--max-rounds", type=int, default=40)
    args = p.parse_args()

    header, events = read_script(args.input)
    tmp = tempfile.mkdtemp(prefix="shift_compare_")
    ref = run(args, args.input, os.path.join(tmp, "ref.txt"), True)
    shifts = []
    segments = [(0, 0)]  # from C frame, C frame i shows reference frame i + offset
    script = os.path.join(tmp, "c_input.txt")
    for _ in range(args.max_rounds):
        write_script(script, header, events, shifts)
        c = run(args, script, os.path.join(tmp, "c.txt"), False)

        def offset_at(i):
            return [o for f, o in segments if f <= i][-1]

        i = 0
        while i < len(c) and i + offset_at(i) < len(ref) and c[i] == ref[i + offset_at(i)]:
            i += 1
        if i >= len(c) or i + offset_at(i) >= len(ref):
            print(f"match: every frame, with the C run ahead by {[o for _, o in segments]} frames")
            return 0
        offset = offset_at(i)
        best = None
        for d in range(offset - MAX_SHIFT, offset + MAX_SHIFT + 1):
            if d == offset or i + d < 0 or i + d + WINDOW > len(ref) or i + WINDOW > len(c):
                continue
            if c[i:i + WINDOW] == ref[i + d:i + d + WINDOW]:
                if best is None or abs(d - offset) < abs(best - offset):
                    best = d
        if best is None:
            print(f"differ: C frame {i} vs reference frame {i + offset}, and no shift lines them up")
            return 1
        print(f"C frame {i}: now {best} frames ahead (was {offset})")
        shifts.append((i + min(offset, best), best))
        segments = [(f, o) for f, o in segments if f < i] + [(i, best)]
    print("gave up: too many shifts")
    return 1


if __name__ == "__main__":
    sys.exit(main())
