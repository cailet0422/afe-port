#!/usr/bin/env python3
"""Inspect a (patched) foresta.rel using the decomp's symbols.

    reldis.py hooks REL                  # references from original sections into added sections
    reldis.py retarget REL ORIG_REL      # address references the patch moved into added sections
    reldis.py dis REL SECTION START END  # disassemble with relocation targets annotated

Needs powerpc-linux-gnu-objdump (apt install binutils-powerpc-linux-gnu) for `dis`.
"""
import bisect
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

# The afe-decomp checkout: the decomp/ submodule unless CMake passes another
ROOT = Path(os.environ.get("AFE_DECOMP_ROOT") or Path(__file__).resolve().parents[2] / "decomp")
CFG = ROOT / "config" / "GAEJ01_01"
SECTION_NAMES = {1: ".text", 2: ".ctors", 3: ".dtors", 4: ".rodata", 5: ".data", 6: ".bss"}


def load_syms(path):
    out = {}
    for line in open(path, encoding="utf-8"):
        m = re.match(r"(\S+) = (\.\w+):0x([0-9A-Fa-f]+);", line)
        if m:
            out.setdefault(m.group(2), []).append((int(m.group(3), 16), m.group(1)))
    for lst in out.values():
        lst.sort()
    return out


def lookup(table, sec, addr):
    lst = table.get(sec, [])
    i = bisect.bisect_right([a for a, _ in lst], addr) - 1
    if i < 0:
        return f"{sec}+{addr:#x}"
    a, n = lst[i]
    return n if a == addr else f"{n}+{addr - a:#x}"


def parse_rel(d):
    (mid, _, _, nsec, secoff, _, _, _, _, _, impoff, impsize) = struct.unpack(">12I", d[:48])
    secs = [struct.unpack(">II", d[secoff + i * 8:secoff + i * 8 + 8]) for i in range(nsec)]
    relocs = []  # (patched section, patched offset, type, module, target section, addend)
    for i in range(impsize // 8):
        imp_mod, p = struct.unpack(">II", d[impoff + i * 8:impoff + i * 8 + 8])
        cur_sec = cur = 0
        while True:
            delta, typ, sec, addend = struct.unpack(">HBBI", d[p:p + 8])
            p += 8
            if typ == 203:
                break
            if typ == 202:
                cur_sec, cur = sec, 0
                continue
            cur += delta
            if typ != 201:
                relocs.append((cur_sec, cur, typ, imp_mod, sec, addend))
    return mid, secs, relocs


def target_name(mid, dol, mod, imod, ssec, addend):
    if imod == 0:
        return lookup(dol, ".text", addend)
    if imod == mid:
        if ssec in SECTION_NAMES:
            return lookup(mod, SECTION_NAMES[ssec], addend)
        return f"NEW_s{ssec}+{addend:#x}"
    return f"mod{imod}:s{ssec}+{addend:#x}"


def main():
    cmd, rel = sys.argv[1], Path(sys.argv[2])
    d = rel.read_bytes()
    mid, secs, relocs = parse_rel(d)
    dol = load_syms(CFG / "symbols.txt")
    mod = load_syms(CFG / "foresta" / "symbols.txt")
    if cmd == "retarget":
        _, _, orig_relocs = parse_rel(Path(sys.argv[3]).read_bytes())
        orig = {(ts, off): (m, sec, ad) for (ts, off, t, m, sec, ad) in orig_relocs}
        for tsec, toff, typ, imod, ssec, addend in sorted(relocs):
            if tsec in SECTION_NAMES and imod == mid and ssec not in SECTION_NAMES and typ != 10:
                o = orig.get((tsec, toff))
                was = target_name(mid, dol, mod, *o) if o else "-"
                site = lookup(mod, SECTION_NAMES[tsec], toff)
                print(f"{site:50s} {SECTION_NAMES[tsec]}+{toff:#08x} {was:40s} -> s{ssec}+{addend:#06x} (type {typ})")
        return
    if cmd == "hooks":
        for tsec, toff, typ, imod, ssec, addend in sorted(relocs):
            if tsec in SECTION_NAMES and imod == mid and ssec not in SECTION_NAMES:
                print(f"{lookup(mod, SECTION_NAMES[tsec], toff):55s} -> s{ssec}+{addend:#06x} (type {typ})")
        return
    secno, start, end = int(sys.argv[3]), int(sys.argv[4], 0), int(sys.argv[5], 0)
    notes = {}
    for tsec, toff, typ, imod, ssec, addend in relocs:
        if tsec == secno and start <= toff < end:
            notes[toff & ~3] = f"{target_name(mid, dol, mod, imod, ssec, addend)} (t{typ})"
    off = secs[secno][0] & ~1
    with tempfile.NamedTemporaryFile(suffix=".bin") as tmp:
        tmp.write(d[off + start:off + end])
        tmp.flush()
        out = subprocess.run(["powerpc-linux-gnu-objdump", "-D", "-b", "binary", "-m", "powerpc:750", "-EB",
                              f"--adjust-vma={start}", tmp.name], capture_output=True, text=True).stdout
    for line in out.splitlines():
        m = re.match(r"\s*([0-9a-f]+):", line)
        if m and int(m.group(1), 16) in notes:
            line += "    ; " + notes[int(m.group(1), 16)]
        print(line)


if __name__ == "__main__":
    main()
