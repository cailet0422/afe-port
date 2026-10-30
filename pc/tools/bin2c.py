#!/usr/bin/env python3
"""Embed a binary file as a C byte array.

With --zero the array has the file's size but only zeros (a build without
the assets, PC_STRIP_ASSETS, fills it from the disc at boot).

usage: bin2c.py [--zero] INPUT OUTPUT SYMBOL [ALIGN]
"""
import sys
from pathlib import Path


def main():
    args = [a for a in sys.argv[1:] if a != "--zero"]
    src, dst, symbol = args[0], Path(args[1]), args[2]
    align = int(args[3]) if len(args) > 3 else 4
    data = Path(src).read_bytes()
    if "--zero" in sys.argv:
        data = bytes(len(data))

    lines = []
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[i : i + 16]) + ",")

    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(
        f"/* Generated from {Path(src).name} by pc/tools/bin2c.py; do not edit. */\n"
        f"unsigned char {symbol}[{len(data)}] __attribute__((aligned({align}))) = {{\n"
        + "\n".join(lines)
        + "\n};\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
