#!/usr/bin/env python3
"""Find raw big-endian asset blobs that game code reads as typed data.

Many extracted assets are included as `u8 name[] = { #include ... }` while
other code declares them with a real element type (`extern u16 name[];`,
`extern mFM_move_actor_data_c name[];`) or loads them as TLUT palettes. On a
little-endian host those multi-byte fields must be swapped once at boot. This
script scans the sources the PC build compiles and writes a C table of
(address, size, kind) entries that pc_boot_fixups.c applies.

usage: gen_data_fixups.py VERSION OUT_C SOURCES_CMAKE [SOURCES_CMAKE ...]
"""
import os
import re
import sys
from collections import defaultdict
from pathlib import Path

# The afe-decomp checkout: the decomp/ submodule unless CMake passes another
ROOT = Path(os.environ.get("AFE_DECOMP_ROOT") or Path(__file__).resolve().parents[2] / "decomp")

# Element type -> fixup kind (see pc_boot_fixups.c)
KINDS = {
    "u16": "PC_FIX_U16",
    "s16": "PC_FIX_U16",
    "mActor_name_t": "PC_FIX_U16",
    "u32": "PC_FIX_U32",
    "s32": "PC_FIX_U32",
    "int": "PC_FIX_U32",
    "Gfx": "PC_FIX_U32",
    "mFM_move_actor_data_c": "PC_FIX_MOVE_ACTOR",
    "mFM_combo_info_c": "PC_FIX_COMBO_INFO",
    "mNpc_NpcHouseData_c": "PC_FIX_NPC_HOUSE",
    "mNpc_Default_Data_c": "PC_FIX_NPC_DEFAULT",
    "mMP_HousePos_c": "PC_FIX_HOUSE_POS",
}
# Names of texture data, which stays in big-endian byte order
TEXTURE_NAME = re.compile(r"tex|txt|TXT|_i4|_i8|_ia\d|_ci\d|_rgb|_rgba|_TA$", re.I)
# Types that are byte data and need no swap
BYTE_TYPES = {"u8", "s8", "char", "void", "mNW_original_tex_c"}

RAW_DEF = re.compile(
    r"^(static\s+)?(?:u8|unsigned char|char|s8)\s+(\w+)\s*\[\s*\w*\s*\][^;=]*=\s*\{\s*\n\s*#include\s+\"(assets/[^\"]+)\"",
    re.M,
)
EXTERN = re.compile(r"\bextern\s+(?:const\s+)?([A-Za-z_]\w*)\s*(\*?)\s*(\w+)\s*(?:\[[^\]]*\]\s*)*;")
TLUT = re.compile(r"\b[gG]s?DPLoadTLUT\w*\s*\(([^;]*?)\)\s*[,;]", re.S)
# Raw blobs used directly as display lists or vertex buffers
DLIST = re.compile(r"\b[gG]s?SP(?:DisplayList|BranchList)\s*\(([^;]*?)\)\s*[,;]", re.S)
VERTEX = re.compile(r"\bgsSPVertex\w*\s*\(\s*&?\s*(\w+)|\bgSPVertex\w*\s*\([^,;]*,\s*&?\s*(\w+)", re.S)
C_INCLUDE = re.compile(r'#include\s+"([^"]+\.(?:c_inc|c))"')
SYMBOL = re.compile(r"^(\w+)\s*=\s*\.(\w+):(0x[0-9A-Fa-f]+);\s*//.*?size:(0x[0-9A-Fa-f]+)")


def compiled_files(cmake_lists):
    files = set()

    def add(path):
        if path in files or not path.exists():
            return
        files.add(path)
        for name in C_INCLUDE.findall(path.read_text(encoding="utf-8", errors="replace")):
            for base in (path.parent, ROOT / "include", ROOT):
                cand = (base / name).resolve()
                if cand.exists():
                    add(cand)
                    break

    for cmake in cmake_lists:
        for m in re.finditer(r'"([^"]+)"', Path(cmake).read_text(encoding="utf-8")):
            add(Path(m.group(1).replace("${DECOMP_ROOT}", str(ROOT))).resolve())
    return files


def symbol_sizes(version):
    sizes = defaultdict(set)
    paths = [ROOT / "config" / version / "symbols.txt"]
    paths += sorted((ROOT / "config" / version).glob("*/symbols.txt"))
    for path in paths:
        for line in path.read_text(encoding="utf-8").splitlines():
            m = SYMBOL.match(line)
            if m:
                sizes[m.group(1)].add(int(m.group(4), 16))
    return sizes


def main():
    version, out_path, cmake_lists = sys.argv[1], Path(sys.argv[2]), sys.argv[3:]
    files = compiled_files(cmake_lists)
    sizes = symbol_sizes(version)

    raw = {}  # name -> (static, file)
    types = defaultdict(set)
    tlut_names = set()
    dlist_names = set()
    vertex_names = set()
    for path in files:
        text = path.read_text(encoding="utf-8", errors="replace")
        for m in RAW_DEF.finditer(text):
            raw[m.group(2)] = (bool(m.group(1)), path)
        for m in EXTERN.finditer(text):
            if not m.group(2):
                types[m.group(3)].add(m.group(1))
        for m in TLUT.finditer(text):
            args = [a.strip() for a in m.group(1).split(",")]
            if args:
                tlut_names.add(re.sub(r"^[&(].*?(\w+)\W*$", r"\1", args[-1]))
        for m in DLIST.finditer(text):
            args = [a.strip() for a in m.group(1).split(",")]
            if args:
                dlist_names.add(re.sub(r"^[&(].*?(\w+)\W*$", r"\1", args[-1]))
        for m in VERTEX.finditer(text):
            vertex_names.add(m.group(1) or m.group(2))
    for path in (ROOT / "include").rglob("*.h"):
        for m in EXTERN.finditer(path.read_text(encoding="utf-8", errors="replace")):
            if not m.group(2):
                types[m.group(3)].add(m.group(1))

    entries = []
    unknown = []
    for name, (is_static, path) in sorted(raw.items()):
        kinds = {KINDS[t] for t in types.get(name, ()) if t in KINDS}
        if TEXTURE_NAME.search(name) and name not in dlist_names:
            # Textures are sometimes declared as Gfx for segment use; they are bytes
            kinds.discard(KINDS["Gfx"])
        if name in tlut_names:
            kinds.add("PC_FIX_U16")
        if name in dlist_names:
            # Gfx is 16 bytes in 64-bit builds: these get a converted copy
            kinds.discard("PC_FIX_U32")
            kinds.add("PC_FIX_GFX")
        if name in vertex_names:
            kinds.add("PC_FIX_VTX")
        unhandled = {t for t in types.get(name, ()) if t not in KINDS and t not in BYTE_TYPES}
        if unhandled:
            unknown.append((name, sorted(unhandled)))
        if not kinds:
            continue
        if is_static:
            print(f"warning: {name} is static in {path}; cannot fix it from outside", file=sys.stderr)
            continue
        if len(kinds) > 1:
            print(f"warning: {name} has conflicting kinds {sorted(kinds)}", file=sys.stderr)
            continue
        size = sizes.get(name, set())
        if len(size) != 1:
            print(f"warning: no unique size for {name} ({sorted(size)})", file=sys.stderr)
            continue
        entries.append((name, size.pop(), kinds.pop()))

    for name, t in unknown:
        print(f"note: {name} is declared as {', '.join(t)}; no fixup kind defined", file=sys.stderr)

    lines = [
        f"/* Generated by pc/tools/gen_data_fixups.py for {version}; do not edit. */",
        '#include "pc_data_fixups.h"',
        "",
    ]
    lines += [f"extern unsigned char {name}[];" for name, _, _ in entries]
    lines += ["", "const PCDataFixup pc_data_fixups[] = {"]
    lines += [f"    {{ {name}, 0x{size:X}, {kind} }}," for name, size, kind in entries]
    lines += ["};", f"const int pc_data_fixup_count = {len(entries)};", ""]
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"{len(entries)} data fixups")


if __name__ == "__main__":
    main()
