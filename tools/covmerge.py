#!/usr/bin/env python3
"""Merges coverage files: covmerge.py [--strip] OUT IN [IN ...] (OUT may also be an input).

--strip saves OUT without the opcode bytes of the code run from work RAM (tools/tgcov.py);
always use it for the repository's disasm/coverage.tgcov."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tgcov import Coverage  # noqa: E402


def main():
    args = sys.argv[1:]
    strip = "--strip" in args
    args = [a for a in args if a != "--strip"]
    if not args:
        sys.exit(__doc__)
    out, ins = args[0], [p for p in args[1:] if os.path.exists(p)]
    if not ins:
        sys.exit("no input files")
    acc = Coverage.load(ins[0])
    for path in ins[1:]:
        acc.merge(Coverage.load(path))
    if strip:
        acc.strip()
    acc.save(out)


if __name__ == "__main__":
    main()
