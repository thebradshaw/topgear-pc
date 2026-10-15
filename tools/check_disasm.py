#!/usr/bin/env python3
"""Disassembles your ROM, reassembles it with ca65/ld65 and checks the result is identical.

    python3 tools/check_disasm.py --rom "Top Gear (USA).sfc" --out build/disasm
"""
import argparse
import hashlib
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import disasm  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", required=True)
    ap.add_argument("--coverage", default=os.path.join(HERE, "..", "disasm", "coverage.tgcov"))
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    subprocess.run([sys.executable, os.path.join(HERE, "disasm.py"), "--rom", args.rom,
                    "--coverage", args.coverage, "--out", args.out], check=True)
    subprocess.run(["ca65", "topgear.s", "-o", "topgear.o"], cwd=args.out, check=True)
    subprocess.run(["ld65", "-C", "topgear.cfg", "topgear.o", "-o", "topgear.sfc"], cwd=args.out, check=True)
    digest = hashlib.sha256(open(os.path.join(args.out, "topgear.sfc"), "rb").read()).hexdigest()
    if digest != disasm.ROM_SHA256:
        sys.exit(f"reassembled ROM differs: {digest}")
    print("reassembled ROM is identical")


if __name__ == "__main__":
    main()
