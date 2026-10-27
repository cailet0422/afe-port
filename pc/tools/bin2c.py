#!/usr/bin/env python3
"""Embed a binary file as a C byte array.

usage: bin2c.py INPUT OUTPUT SYMBOL [ALIGN]
"""
import sys
from pathlib import Path


def main():
    src, dst, symbol = sys.argv[1], Path(sys.argv[2]), sys.argv[3]
    align = int(sys.argv[4]) if len(sys.argv) > 4 else 4
    data = Path(src).read_bytes()

    lines = []
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[i : i + 16]) + ",")

    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(
        f"/* Generated from {Path(src).name} by pc/tools/bin2c.py; do not edit. */\n"
        f"__attribute__((aligned({align}))) unsigned char {symbol}[{len(data)}] = {{\n"
        + "\n".join(lines)
        + "\n};\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
