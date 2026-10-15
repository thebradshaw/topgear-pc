#!/usr/bin/env python3
"""Rebuilds disasm/coverage.tgcov by replaying every recording in tests/inputs.

    python3 tools/collect_coverage.py --rom "Top Gear (USA).sfc" [--headless build/tg_headless]

Each recording runs for the frame count in its '# frames N' or '# end frame N' line.
"""
import argparse
import glob
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from tgcov import Coverage  # noqa: E402


def frames_of(path):
    for line in open(path):
        m = re.match(r"#\s*(?:end frame|frames)\s+(\d+)", line)
        if m:
            return int(m.group(1))
    sys.exit(f"{path}: no '# frames N' line")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", required=True)
    ap.add_argument("--headless", default=os.path.join(ROOT, "build", "tg_headless"))
    ap.add_argument("--out", default=os.path.join(ROOT, "disasm", "coverage.tgcov"))
    args = ap.parse_args()
    acc = None
    with tempfile.TemporaryDirectory() as tmp:
        for rec in sorted(glob.glob(os.path.join(ROOT, "tests", "inputs", "*.txt"))):
            cov = os.path.join(tmp, os.path.basename(rec) + ".tgcov")
            n = frames_of(rec)
            print(f"{os.path.basename(rec)}: {n} frames", flush=True)
            subprocess.run([args.headless, "--rom", args.rom, "--input", rec, "--frames", str(n),
                            "--coverage", cov], check=True, stdout=subprocess.DEVNULL)
            c = Coverage.load(cov)
            if acc is None:
                acc = c
            else:
                acc.merge(c)
    acc.save(args.out)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
