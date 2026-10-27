#!/usr/bin/env python3
"""Map REL data objects to their addresses in the PC executable.

The PC build compiles the REL modules' data from the assets that the
matching build extracted. To use the data of the disc the game runs from
instead (the runtime asset path, and discs carrying the community Korean
patch, which rewrites data in place), the executable needs to know where
each REL data object ended up. This script reads the linker map (every
variable has its own section thanks to -fdata-sections, statics included),
matches it to config/<version>/<module>/symbols.txt, and writes a table that
pc_rel_overlay.c applies at boot:

    header: "RELO" u32 version, u32 count, u32 anchor_vma
    entry:  u8 module, u8 section, u8 kind, u8 pad, u32 offset, u32 size, u32 vma

Only objects whose element type is known to be plain data are listed.

usage: gen_rel_overlay.py VERSION MAP SOURCES_CMAKE [...] -o OUT
"""
import argparse
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULES = {"foresta": 1, "forestd": 2, "foresti": 3, "foresto": 4}
DATA_SECTIONS = (".data", ".rodata")
ANCHOR = "pc_rel_overlay_anchor"

# Element types -> kind (see pc_rel_overlay.c)
KIND_BYTES, KIND_U16, KIND_U32, KIND_VTX = 0, 1, 2, 3
TYPE_KINDS = {
    "u8": KIND_BYTES, "s8": KIND_BYTES, "char": KIND_BYTES, "unsigned char": KIND_BYTES,
    "u16": KIND_U16, "s16": KIND_U16, "mActor_name_t": KIND_U16,
    "u32": KIND_U32, "s32": KIND_U32, "int": KIND_U32, "f32": KIND_U32, "float": KIND_U32,
    "Vtx": KIND_VTX,
}
# Structs made only of fields of one width
STRUCT_KINDS = {
    "mCL_ftr_info_c": KIND_U16,
}
# Structs whose leading bytes are text (the rest holds pointers): (kind, size)
STRUCT_PREFIXES = {
    "mTG_tag_word_c": (KIND_BYTES, 8),
}

SYMBOL = re.compile(r"^(\S+) = (\.\w+):0x([0-9A-Fa-f]+);.*?size:0x([0-9A-Fa-f]+)")
# ELF: .data.NAME, .rodata.NAME, .data.rel.local.NAME; PE (MinGW): .data$NAME, .rdata$NAME
MAP_SECTION = re.compile(r"^ (\.(?:data|rodata|rdata)(?:\.rel(?:\.ro)?)?(?:\.local)?)[.$](\S+)(?:\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+))?$")
# LLD (Android NDK): "     VMA      LMA     Size Align Out     In      Symbol"
LLD_SECTION = re.compile(r"^\s*([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)\s+\d+\s+(\S+):\((\.(?:data|rodata)(?:\.rel)?(?:\.ro)?(?:\.local)?)\.(\S+)\)$")
LLD_SYMBOL = re.compile(r"^\s*([0-9a-f]+)\s+[0-9a-f]+\s+[0-9a-f]+\s+\d+\s+(\w+)$")
MAP_CONT = re.compile(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$")
MAP_SYMBOL = re.compile(r"^\s+0x([0-9a-f]+)\s+(\w+)$")
DECL = re.compile(r"^[ \t]*(?:static\s+)?(?:const\s+)?([A-Za-z_][\w ]*?)\s+(\w+)\s*((?:\[[^\]]*\]\s*)+)(?:ATTRIBUTE_ALIGN\(\d+\)\s*)?=", re.M)
# struct objects that are not arrays: "static mTG_tag_word_c name = {"
DECL_SCALAR = re.compile(r"^[ \t]*(?:static\s+)?(?:const\s+)?([A-Za-z_]\w*)\s+(\w+)\s*=\s*\{", re.M)
C_INCLUDE = re.compile(r'#include\s+"([^"]+\.(?:c_inc|c))"')


def read_sections(module, version):
    """REL section numbers from the splits.txt section list (1-based)."""
    names = []
    for line in (ROOT / "config" / version / module / "splits.txt").read_text(encoding="utf-8").splitlines():
        m = re.match(r"^\t(\.\w+)\s+type:", line)
        if m:
            names.append(m.group(1))
        elif names and not line.startswith("\t"):
            break
    return {name: i + 1 for i, name in enumerate(names)}


def read_splits(module, version):
    """[(unit, section, start, end)]"""
    out = []
    unit = None
    for line in (ROOT / "config" / version / module / "splits.txt").read_text(encoding="utf-8").splitlines():
        m = re.match(r"^(\S.*):$", line)
        if m and line != "Sections:":
            unit = m.group(1)
            continue
        m = re.match(r"^\t(\.\w+)\s+start:0x([0-9A-Fa-f]+) end:0x([0-9A-Fa-f]+)", line)
        if m and unit:
            out.append((unit, m.group(1), int(m.group(2), 16), int(m.group(3), 16)))
    return out


def read_map(path):
    """{(object, name): [(vma, size)]} and the anchor's vma."""
    entries = defaultdict(list)
    anchor = None
    pending = None
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    if text.lstrip().startswith("VMA"):
        for line in text.splitlines():
            m = LLD_SECTION.match(line)
            if m:
                entries[(m.group(3), m.group(5))].append((int(m.group(1), 16), int(m.group(2), 16)))
                continue
            m = LLD_SYMBOL.match(line)
            if m and m.group(2) == ANCHOR:
                anchor = int(m.group(1), 16)
        return entries, anchor
    # Skip the "Discarded input sections" list; only placed sections count
    start = text.find("Linker script and memory map")
    for line in text[start if start >= 0 else 0:].splitlines():
        m = MAP_SECTION.match(line)
        if m:
            name = m.group(2)
            if m.group(3):
                entries[(m.group(5), name)].append((int(m.group(3), 16), int(m.group(4), 16)))
                pending = None
            else:
                pending = name
            continue
        if pending:
            m = MAP_CONT.match(line)
            if m:
                entries[(m.group(3), pending)].append((int(m.group(1), 16), int(m.group(2), 16)))
            pending = None
            continue
        m = MAP_SYMBOL.match(line)
        if m and m.group(2) == ANCHOR:
            anchor = int(m.group(1), 16)
    return entries, anchor


def source_text(path, seen=None):
    """Text of a source file and the .c_inc/.c files it includes."""
    seen = seen if seen is not None else set()
    if path in seen or not path.exists():
        return ""
    seen.add(path)
    text = path.read_text(encoding="utf-8", errors="replace")
    parts = [text]
    for name in C_INCLUDE.findall(text):
        for base in (path.parent, ROOT / "include", ROOT):
            cand = (base / name).resolve()
            if cand.exists():
                parts.append(source_text(cand, seen))
                break
    return "\n".join(parts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("version")
    ap.add_argument("map")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()

    entries, anchor = read_map(args.map)
    if anchor is None:
        sys.exit(f"{ANCHOR} not found in {args.map}")

    # object file -> source path, and name -> objects
    objects_by_name = defaultdict(list)
    for (obj, name) in entries:
        objects_by_name[name].append(obj)

    # function-local statics: GCC names them VAR.N, Clang FUNCTION.VAR
    local_statics = defaultdict(list)
    local_static_var = {}
    for key in objects_by_name:
        stem, dot, last = key.rpartition(".")
        if not dot:
            continue
        var = stem if last.isdigit() else last
        local_statics[var].append(key)
        local_static_var[key] = var
    local_static_keys = set(local_static_var)

    decl_cache = {}

    build_dir = Path(args.map).resolve().parent

    def decl_type(obj, name):
        src = re.sub(r"\.(o|obj)$", "", obj)
        m = re.search(r"^(.*?)CMakeFiles/[^/]+\.dir/(.*)$", src)
        if not m:
            return None
        rel = m.group(2)
        # absolute sources keep their path minus the leading '/'; others are
        # relative to the target's binary dir, with ".." written as "__"
        target_dir = build_dir / m.group(1)
        path = Path("/" + rel) if Path("/" + rel).exists() else (target_dir / rel.replace("__/", "../")).resolve()
        if path not in decl_cache:
            text = source_text(path)
            decls = {d.group(2): d.group(1).strip() for d in DECL_SCALAR.finditer(text)}
            decls.update({d.group(2): d.group(1).strip() for d in DECL.finditer(text)})
            decl_cache[path] = decls
        return decl_cache[path].get(name)

    out = []
    stats = defaultdict(int)
    for module, mod_id in MODULES.items():
        sections = read_sections(module, args.version)
        splits = read_splits(module, args.version)
        for line in (ROOT / "config" / args.version / module / "symbols.txt").read_text(encoding="utf-8").splitlines():
            m = SYMBOL.match(line)
            if not m or m.group(2) not in DATA_SECTIONS:
                continue
            name, sec, off, size = m.group(1), m.group(2), int(m.group(3), 16), int(m.group(4), 16)
            if size == 0:
                continue
            base = name.split("$")[0]
            unit = next((u for (u, s, a, b) in splits if s == sec and a <= off < b), None)

            # candidate (object, mapped name) pairs
            cands = []
            for mapped in (base, f"{base}__{module}_dup"):
                for obj in objects_by_name.get(mapped, []):
                    cands.append((obj, mapped))
            if "$" in name:
                # function-local statics are named base.N
                cands += [(obj, key) for key in local_statics.get(base, ()) for obj in objects_by_name[key]]
            if unit:
                in_unit = [c for c in cands if re.search(re.escape("/" + unit) + r"\.(o|obj)$", c[0])]
                if in_unit:
                    cands = in_unit
            elif module in ("foresti", "foresto"):
                cands = [c for c in cands if f"/assetdis/{module}/" in c[0]] or cands
            # keep candidates whose size matches
            sized = [(c, v) for c in cands for v in entries[c] if v[1] == size]
            if not sized:
                # PE maps list sections padded to their alignment (3 bytes as 4)
                sized = [(c, v) for c in cands for v in entries[c]
                         if c[0].endswith(".obj") and size < v[1] <= (size + 31) & ~31]
            if len(sized) != 1:
                stats["unmatched" if not sized else "ambiguous"] += 1
                continue
            (obj, mapped), (vma, _) = sized[0]
            # a function-local static is declared without its .N suffix
            t = decl_type(obj, local_static_var.get(mapped, mapped))
            kind = TYPE_KINDS.get(t) if t else None
            if kind is None and t in STRUCT_KINDS:
                kind = STRUCT_KINDS[t]
            if kind is None and t in STRUCT_PREFIXES:
                kind, size = STRUCT_PREFIXES[t]
            if kind is None:
                stats[f"type:{t}"] += 1
                continue
            out.append((mod_id, sections[sec], kind, off, size, vma))
            stats["ok"] += 1

    # Two disc objects resolving to one executable object (a name shared by
    # several translation units) can't both be right
    claims = defaultdict(int)
    for e in out:
        claims[e[5]] += 1
    shared = sum(1 for e in out if claims[e[5]] > 1)
    out = [e for e in out if claims[e[5]] == 1]
    stats["shared"] = shared
    stats["ok"] = len(out)

    blob = b"RELO" + struct.pack("<III", 1, len(out), anchor)
    for mod_id, sec, kind, off, size, vma in out:
        blob += struct.pack("<BBBBIII", mod_id, sec, kind, 0, off, size, vma)
    Path(args.output).write_bytes(blob)
    top = sorted(stats.items(), key=lambda kv: -kv[1])[:12]
    print(f"rel overlay: {stats['ok']} objects; " + ", ".join(f"{k}={v}" for k, v in top if k != "ok"))


if __name__ == "__main__":
    main()
