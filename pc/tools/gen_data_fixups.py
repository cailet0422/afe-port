#!/usr/bin/env python3
"""Find raw big-endian asset blobs that game code reads as typed data.

Many extracted assets are included as `u8 name[] = { #include ... }` while
other code declares them with a real element type (`extern u16 name[];`,
`extern mFM_move_actor_data_c name[];`) or loads them as TLUT palettes. On a
little-endian host those multi-byte fields must be swapped once at boot. This
script scans the sources the PC build compiles and writes a C table of
(address, size, kind) entries that pc_boot_fixups.c applies.

A blob declared `static` cannot be named from the table's file. For those
the script writes a header per source file, next to OUT_C under
static_fixups/, that declares the blob ahead (`static unsigned char name[];`,
completed by the file's own definition) and defines its table entry; and a
CMake file, OUT_C's directory/static_fixups.cmake, that has the compiler
force-include that header into the source (-include). The source and its
object file stay as they are (gen_rel_overlay.py finds objects by path).

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
    """(every file the build compiles, with the files they include;
    {top-level source: its path as the CMake list writes it};
    the top-level sources compiled as C++)"""
    files = set()
    top = {}
    cxx = set()

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
        text = Path(cmake).read_text(encoding="utf-8")
        for block in re.finditer(r"set\((\w+)(.*?)\)", text, re.S):
            for m in re.finditer(r'"([^"]+)"', block.group(2)):
                path = Path(m.group(1).replace("${DECOMP_ROOT}", str(ROOT))).resolve()
                top.setdefault(path, m.group(1))
                if "CXX" in block.group(1):
                    cxx.add(path)
                add(path)
    return files, top, cxx


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
    files, top, cxx = compiled_files(cmake_lists)
    sizes = symbol_sizes(version)

    raw = {}  # name -> (static, file); a static name can be in several files
    statics = []  # (name, file)
    types = defaultdict(set)
    tlut_names = set()
    dlist_names = set()
    vertex_names = set()
    for path in files:
        text = path.read_text(encoding="utf-8", errors="replace")
        for m in RAW_DEF.finditer(text):
            if m.group(1):
                statics.append((m.group(2), path))
            else:
                raw[m.group(2)] = (False, path)
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

    def kinds_of(name):
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
        return kinds

    def size_kind(name, kinds):
        """(size, kind) of a blob to fix, or None (with a warning)"""
        if len(kinds) > 1:
            print(f"warning: {name} has conflicting kinds {sorted(kinds)}", file=sys.stderr)
            return None
        size = sizes.get(name, set())
        if len(size) != 1:
            print(f"warning: no unique size for {name} ({sorted(size)})", file=sys.stderr)
            return None
        return min(size), min(kinds)

    entries = []
    unknown = []
    for name, (_, path) in sorted(raw.items()):
        kinds = kinds_of(name)
        unhandled = {t for t in types.get(name, ()) if t not in KINDS and t not in BYTE_TYPES}
        if unhandled:
            unknown.append((name, sorted(unhandled)))
        if kinds and (sk := size_kind(name, kinds)):
            entries.append((name, *sk))

    # static blobs: an entry defined in their own file through a forced include
    static_dir = out_path.parent / "static_fixups"
    static_headers = {}  # source -> [(entry symbol, name, size, kind)]
    for name, path in sorted(statics, key=lambda e: (str(e[1]), e[0])):
        kinds = kinds_of(name)
        if not kinds:
            continue
        if path not in top or path in cxx:
            print(f"warning: {name} is static in {path}, which is not a C source the build lists; "
                  "cannot fix it", file=sys.stderr)
            continue
        if sk := size_kind(name, kinds):
            stem = re.sub(r"\W", "_", path.stem)
            symbol = f"pc_static_fixup_{stem}_{name}"
            static_headers.setdefault(path, []).append((symbol, name, *sk))

    for name, t in unknown:
        print(f"note: {name} is declared as {', '.join(t)}; no fixup kind defined", file=sys.stderr)

    lines = [
        f"/* Generated by pc/tools/gen_data_fixups.py for {version}; do not edit. */",
        '#include "pc_data_fixups.h"',
        "",
    ]
    static_entries = [e for path in sorted(static_headers) for e in static_headers[path]]
    lines += [f"extern unsigned char {name}[];" for name, _, _ in entries]
    lines += [f"extern const PCDataFixup {symbol};" for symbol, _, _, _ in static_entries]
    lines += ["", "const PCDataFixup pc_data_fixups[] = {"]
    lines += [f"    {{ {name}, 0x{size:X}, {kind} }}," for name, size, kind in entries]
    lines += ["};", f"const int pc_data_fixup_count = {len(entries)};", ""]
    lines += ["/* static blobs: entries in their own files (static_fixups/) */",
              "const PCDataFixup* const pc_static_data_fixups[] = {"]
    lines += [f"    &{symbol}," for symbol, _, _, _ in static_entries] or ["    0,"]
    lines += ["};", f"const int pc_static_data_fixup_count = {len(static_entries)};", ""]
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines), encoding="utf-8")

    static_dir.mkdir(parents=True, exist_ok=True)
    cmake = ["# Generated by pc/tools/gen_data_fixups.py; do not edit.",
             "# The static blobs' fixup entries, force-included into their sources."]
    for path, items in sorted(static_headers.items()):
        for base in (ROOT, out_path.parent):
            if path.is_relative_to(base):
                rel = path.relative_to(base)
                break
        else:
            rel = Path(path.name)
        header = static_dir / (re.sub(r"\W", "_", str(rel.with_suffix(""))) + ".h")
        text = [f"/* Generated by pc/tools/gen_data_fixups.py for {path.name}; do not edit. */",
                '#include "pc_data_fixups.h"', ""]
        for symbol, name, size, kind in items:
            text += [f"static unsigned char {name}[]; /* completed by the definition in {path.name} */",
                     f"const PCDataFixup {symbol} = {{ {name}, 0x{size:X}, {kind} }};"]
        header.write_text("\n".join(text) + "\n", encoding="utf-8")
        cmake.append(f'set_property(SOURCE "{top[path]}" APPEND PROPERTY COMPILE_OPTIONS "-include;{header}")')
        cmake.append(f'set_property(SOURCE "{top[path]}" APPEND PROPERTY OBJECT_DEPENDS "{header}")')
    (out_path.parent / "static_fixups.cmake").write_text("\n".join(cmake) + "\n", encoding="utf-8")
    print(f"{len(entries)} data fixups, {len(static_entries)} of static blobs")


if __name__ == "__main__":
    main()
