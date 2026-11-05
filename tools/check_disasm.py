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
    rom = open(args.rom, "rb").read()
    rom = rom[512:] if len(rom) % 1024 == 512 else rom
    want = hashlib.sha256(rom).hexdigest()
    p = disasm.GAMES[want][1]
    subprocess.run(["ca65", p + ".s", "-o", p + ".o"], cwd=args.out, check=True)
    subprocess.run(["ld65", "-C", p + ".cfg", p + ".o", "-o", p + ".sfc"], cwd=args.out, check=True)
    digest = hashlib.sha256(open(os.path.join(args.out, p + ".sfc"), "rb").read()).hexdigest()
    if digest != want:
        sys.exit(f"reassembled ROM differs: {digest}")
    print("reassembled ROM is identical")


if __name__ == "__main__":
    main()
