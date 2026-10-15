#!/usr/bin/env python3
"""Merges coverage files: covmerge.py OUT IN [IN ...] (OUT may also be an input)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tgcov import Coverage  # noqa: E402


def main():
    out, ins = sys.argv[1], [p for p in sys.argv[2:] if os.path.exists(p)]
    if not ins:
        sys.exit("no input files")
    acc = Coverage.load(ins[0])
    for path in ins[1:]:
        acc.merge(Coverage.load(path))
    acc.save(out)


if __name__ == "__main__":
    main()
