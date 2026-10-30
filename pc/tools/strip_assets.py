#!/usr/bin/env python3
"""Write the extracted assets (.inc) as zeros of the same shape.

A build with PC_STRIP_ASSETS finds these before the real ones, so the
executable keeps the size and layout of every asset but none of its bytes;
pc_rel_overlay.c fills them from the disc at boot. Each number in a .inc
(they hold nothing else) becomes 0. Files are only rewritten when their
source is newer.

usage: strip_assets.py OUT_INCLUDE_DIR INCLUDE_DIR [...]
    (every INCLUDE_DIR/assets/**/*.inc goes to OUT_INCLUDE_DIR/assets/...)
"""
import re
import sys
from pathlib import Path

NUMBER = re.compile(r"-?(?:0[xX][0-9A-Fa-f]+|\d+)")


def main():
    out = Path(sys.argv[1])
    written = kept = 0
    for root in map(Path, sys.argv[2:]):
        for src in sorted((root / "assets").rglob("*.inc")):
            dst = out / src.relative_to(root)
            if dst.exists() and dst.stat().st_mtime >= src.stat().st_mtime:
                kept += 1
                continue
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_text(NUMBER.sub("0", src.read_text(encoding="utf-8")), encoding="utf-8")
            written += 1
    print(f"strip_assets: {written} written, {kept} up to date")


if __name__ == "__main__":
    main()
