#!/usr/bin/env python3
"""Map data objects of main.dol and the RELs to their addresses in the PC executable.

The PC build compiles the game's data from the assets that the matching
build extracted. To use the data of the disc the game runs from instead
(the runtime asset path, and discs carrying the community Korean patch,
which rewrites data in place), the executable needs to know where each data
object ended up. This script reads the linker map (every variable has its
own section thanks to -fdata-sections, statics included), matches it to
config/<version>/[<module>/]symbols.txt, and writes a table that
pc_rel_overlay.c applies at boot:

    header: "RELO" u32 version, u32 count, u32 anchor_vma
    entry:  u8 module, u8 section, u8 kind, u8 flags, u32 offset, u32 size, u32 vma

Module 0 is main.dol (offset is then the address); flag 1 marks an asset,
an object filled from an extracted asset (.inc, or an embedded binary).
Only objects whose element type is known to be plain data are listed, and
of main.dol only the assets. --require-assets fails if an asset compiled
into the executable is missing from the table (a build without the asset
bytes would then lack it).

usage: gen_rel_overlay.py VERSION MAP [--require-assets] -o OUT
"""
import argparse
import os
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

# The afe-decomp checkout: the decomp/ submodule unless CMake passes another
ROOT = Path(os.environ.get("AFE_DECOMP_ROOT") or Path(__file__).resolve().parents[2] / "decomp")
MODULES = {"static": 0, "foresta": 1, "forestd": 2, "foresti": 3, "foresto": 4}
DATA_SECTIONS = (".data", ".rodata")
DOL_DATA_SECTIONS = DATA_SECTIONS + (".sdata", ".sdata2")
FLAG_ASSET = 1
ANCHOR = "pc_rel_overlay_anchor"

# Element types -> kind (see pc_rel_overlay.c)
KIND_BYTES, KIND_U16, KIND_U32, KIND_VTX = 0, 1, 2, 3
TYPE_KINDS = {
    "u8": KIND_BYTES, "s8": KIND_BYTES, "char": KIND_BYTES, "unsigned char": KIND_BYTES,
    "u16": KIND_U16, "s16": KIND_U16, "mActor_name_t": KIND_U16,
    "u32": KIND_U32, "s32": KIND_U32, "int": KIND_U32, "f32": KIND_U32, "float": KIND_U32,
    "Vtx": KIND_VTX,
}
ELEM_SIZES = {KIND_BYTES: 1, KIND_U16: 2, KIND_U32: 4, KIND_VTX: 16}
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
MAP_SECTION = re.compile(r"^ (\.(?:data|rodata|rdata|bss)(?:\.rel(?:\.ro)?)?(?:\.local)?)[.$](\S+)(?:\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+))?$")
# LLD (Android NDK): "     VMA      LMA     Size Align Out     In      Symbol"
LLD_SECTION = re.compile(r"^\s*([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)\s+\d+\s+(\S+):\((\.(?:data|rodata|bss)(?:\.rel)?(?:\.ro)?(?:\.local)?)\.(\S+)\)$")
LLD_SYMBOL = re.compile(r"^\s*([0-9a-f]+)\s+[0-9a-f]+\s+[0-9a-f]+\s+\d+\s+(\w+)$")
MAP_CONT = re.compile(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$")
MAP_SYMBOL = re.compile(r"^\s+0x([0-9a-f]+)\s+(\w+)$")
ALIGN = r"(?:(?:ATTRIBUTE_ALIGN\(\d+\)|__attribute__\(\(aligned\(\d+\)\)\))\s*)?"
DECL = re.compile(r"^[ \t]*(?:extern\s+)?(?:static\s+)?(?:const\s+)?([A-Za-z_][\w ]*?)\s+(\w+)\s*((?:\[[^\]]*\]\s*)+)" + ALIGN + "=", re.M)
# an array whose initializer is an extracted asset
ASSET_DECL = re.compile(r"(\w+)\s*(?:\[[^\]]*\]\s*)+" + ALIGN
                        + r'=\s*\{\s*(?:#ifndef __INTELLISENSE__\s*)?#include\s+"(assets/[^"]+)"')
EXTRACT = re.compile(r"^\s*- symbol: (\S+)\n(?:[ \t]+\w+:.*\n)*?[ \t]+header: (\S+)", re.M)
EMBEDDED = "by pc/tools/bin2c.py"  # bin2c.py output: an embedded binary from the disc
# struct objects that are not arrays: "static mTG_tag_word_c name = {"
DECL_SCALAR = re.compile(r"^[ \t]*(?:static\s+)?(?:const\s+)?([A-Za-z_]\w*)\s+(\w+)\s*=\s*\{", re.M)
C_INCLUDE = re.compile(r'#include\s+"([^"]+\.(?:c_inc|c))"')
# gen_module_data.py renames a name an earlier module also defines
DUP_DEFINE = re.compile(r"^#define (\w+) (\w+__\w+_dup)$", re.M)


def config_dir(module, version):
    return ROOT / "config" / version / ("" if module == "static" else module)


def read_sections(module, version):
    """REL section numbers from the splits.txt section list (1-based)."""
    names = []
    for line in (config_dir(module, version) / "splits.txt").read_text(encoding="utf-8").splitlines():
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
    for line in (config_dir(module, version) / "splits.txt").read_text(encoding="utf-8").splitlines():
        m = re.match(r"^(\S.*):$", line)
        if m and line != "Sections:":
            unit = m.group(1)
            continue
        m = re.match(r"^\t(\.\w+)\s+start:0x([0-9A-Fa-f]+) end:0x([0-9A-Fa-f]+)", line)
        if m and unit:
            out.append((unit, m.group(1), int(m.group(2), 16), int(m.group(3), 16)))
    return out


def read_extracts(version):
    """{header: (module, symbol)} from config.yml; a symbol may carry its place
    (name!.section:0xOFFSET) where the name alone is ambiguous"""
    text = (ROOT / "config" / version / "config.yml").read_text(encoding="utf-8")
    module = "static"
    out = {}
    pos = 0
    for m in re.finditer(r"^- object: files/(\w+)\.rel$", text, re.M):
        for e in EXTRACT.finditer(text, pos, m.start()):
            out[e.group(2)] = (module, e.group(1))
        module, pos = m.group(1), m.end()
    for e in EXTRACT.finditer(text, pos):
        out[e.group(2)] = (module, e.group(1))
    return out


def read_map(path):
    """{(object, name): [(vma, size)]}, the anchor's vma and the (object, name)s
    in .bss (zero-filled: assets of a build without them)"""
    entries = defaultdict(list)
    bss = set()
    anchor = None
    pending = None
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    if text.lstrip().startswith("VMA"):
        for line in text.splitlines():
            m = LLD_SECTION.match(line)
            if m:
                entries[(m.group(3), m.group(5))].append((int(m.group(1), 16), int(m.group(2), 16)))
                if m.group(4).startswith(".bss"):
                    bss.add((m.group(3), m.group(5)))
                continue
            m = LLD_SYMBOL.match(line)
            if m and m.group(2) == ANCHOR:
                anchor = int(m.group(1), 16)
        return entries, anchor, bss
    # Skip the "Discarded input sections" list; only placed sections count
    start = text.find("Linker script and memory map")
    for line in text[start if start >= 0 else 0:].splitlines():
        m = MAP_SECTION.match(line)
        if m:
            name = m.group(2)
            in_bss = m.group(1).startswith(".bss")
            if m.group(3):
                entries[(m.group(5), name)].append((int(m.group(3), 16), int(m.group(4), 16)))
                if in_bss:
                    bss.add((m.group(5), name))
                pending = None
            else:
                pending = name
            continue
        if pending:
            m = MAP_CONT.match(line)
            if m:
                entries[(m.group(3), pending)].append((int(m.group(1), 16), int(m.group(2), 16)))
                if in_bss:
                    bss.add((m.group(3), pending))
            pending = None
            continue
        m = MAP_SYMBOL.match(line)
        if m and m.group(2) == ANCHOR:
            anchor = int(m.group(1), 16)
    return entries, anchor, bss


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
    ap.add_argument("--require-assets", action="store_true")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()

    entries, anchor, bss = read_map(args.map)
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

    def source_info(obj):
        """({name: type}, {asset names}, {name: [(type, bytes, element bytes)]}) of the
        source an object file was built from; the last lists every declaration
        of a name (function-local statics of one name in several functions)"""
        src = re.sub(r"\.(o|obj)$", "", obj)
        m = re.search(r"^(.*?)CMakeFiles/[^/]+\.dir/(.*)$", src)
        if not m:
            return {}, {}, {}
        rel = m.group(2)
        # absolute sources keep their path minus the leading '/'; others are
        # relative to the target's binary dir, with ".." written as "__"
        target_dir = build_dir / m.group(1)
        path = Path("/" + rel) if Path("/" + rel).exists() else (target_dir / rel.replace("__/", "../")).resolve()
        if path not in decl_cache:
            text = source_text(path)
            decls = {d.group(2): d.group(1).strip() for d in DECL_SCALAR.finditer(text)}
            decls.update({d.group(2): d.group(1).strip() for d in DECL.finditer(text)})
            sized = defaultdict(list)
            for d in DECL.finditer(text):
                count = 1
                for n in re.findall(r"\[\s*(\w*)\s*\]", d.group(3)):
                    count = count * int(n) if count and n.isdigit() else None
                elem = ELEM_SIZES.get(TYPE_KINDS.get(d.group(1).strip()))
                sized[d.group(2)].append((d.group(1).strip(), count * elem if count and elem else None, elem))
            assets = ({name: None for name in decls} if EMBEDDED in text
                      else {d.group(1): d.group(2) for d in ASSET_DECL.finditer(text)})
            for d in DUP_DEFINE.finditer(text):
                if d.group(1) in decls:
                    decls[d.group(2)] = decls[d.group(1)]
                if d.group(1) in assets:
                    assets[d.group(2)] = assets[d.group(1)]
            decl_cache[path] = (decls, assets, sized)
        return decl_cache[path]

    def decl_type(obj, name, size=None):
        decls, _, sized = source_info(obj)
        if len({TYPE_KINDS.get(t, t) for t, _, _ in sized.get(name, ())}) > 1:
            # declared with types of several kinds: the one of that size, or else the
            # one of no given size whose elements fit, if only one is
            if size is None:
                return None
            fits = ({t for t, n, _ in sized[name] if n == size}
                    or {t for t, n, elem in sized[name] if n is None and (elem is None or size % elem == 0)})
            return fits.pop() if len({TYPE_KINDS.get(t, t) for t in fits}) == 1 else None
        return decls.get(name)

    def is_asset(obj, name):
        return local_static_var.get(name, name) in source_info(obj)[1]

    def range_objects(module, symbols, splits):
        """foresti/foresto are one unit on the disc, cut into a translation
        unit per .data range by gen_module_data.py; which object file holds
        which range, judged by the names of the other symbols in it (static
        copies of a palette repeat across ranges, the rest do not)"""
        names = defaultdict(set)
        for name, sec, off, _ in symbols:
            for (unit, s, a, b) in splits:
                if s == sec and a <= off < b:
                    names[(sec, a, b)].add(name.split("$")[0])
                    break
        found = {}
        for rng, in_range in names.items():
            votes = defaultdict(int)
            for name in in_range:
                for obj in objects_by_name.get(name, ()):
                    if f"/assetdis/{module}/" in obj:
                        votes[obj] += 1
            if votes:
                found[rng] = max(votes, key=votes.get)
        return found

    out = []
    vma_obj = {}
    stats = defaultdict(int)
    disc_symbols = {}
    for module, mod_id in MODULES.items():
        sections = read_sections(module, args.version)
        splits = read_splits(module, args.version)
        symbols = []
        for line in (config_dir(module, args.version) / "symbols.txt").read_text(encoding="utf-8").splitlines():
            m = SYMBOL.match(line)
            if m:
                symbols.append((m.group(1), m.group(2), int(m.group(3), 16), int(m.group(4), 16)))
        by_range = range_objects(module, symbols, splits) if module in ("foresti", "foresto") else {}
        disc_symbols[module] = (symbols, sections)
        for line in (config_dir(module, args.version) / "symbols.txt").read_text(encoding="utf-8").splitlines():
            m = SYMBOL.match(line)
            if not m or m.group(2) not in (DOL_DATA_SECTIONS if mod_id == 0 else DATA_SECTIONS):
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
            # zero-filled objects are of no interest unless they are stripped assets
            cands = [c for c in cands if c not in bss or is_asset(*c)]
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
            if not sized:
                # a struct of which only the leading text is overlaid is larger
                # where it also holds pointers (64-bit)
                sized = [(c, v) for c in cands for v in entries[c]
                         if decl_type(c[0], local_static_var.get(c[1], c[1])) in STRUCT_PREFIXES and v[1] >= size]
            if len(sized) > 1 and by_range:
                rng = next(((sec, a, b) for (u, s_, a, b) in splits if s_ == sec and a <= off < b), None)
                sized = [x for x in sized if x[0][0] == by_range.get(rng)] or sized
            if len(sized) != 1:
                stats["unmatched" if not sized else "ambiguous"] += 1
                continue
            (obj, mapped), (vma, _) = sized[0]
            flags = FLAG_ASSET if is_asset(obj, mapped) else 0
            if mod_id == 0 and not flags:
                continue
            # a function-local static is declared without its .N suffix
            t = decl_type(obj, local_static_var.get(mapped, mapped), size)
            kind = TYPE_KINDS.get(t) if t else None
            if kind is None and t in STRUCT_KINDS:
                kind = STRUCT_KINDS[t]
            if kind is None and t in STRUCT_PREFIXES:
                kind, size = STRUCT_PREFIXES[t]
            if kind is None:
                stats[f"type:{t}"] += 1
                continue
            out.append((mod_id, 0 if mod_id == 0 else sections[sec], kind, flags, off, size, vma))
            vma_obj[vma] = obj
            stats["ok"] += 1

    # Two disc objects resolving to one executable object (a name shared by
    # several translation units) can't both be right, unless it is the copy
    # gen_module_data.py kept of an object foresti and foresto both have
    claims = defaultdict(list)
    for e in out:
        claims[e[6]].append(e)
    out = []
    names = {v: k for k, v in MODULES.items()}
    for vma, claimants in claims.items():
        own = [e for e in claimants if f"/assetdis/{names[e[0]]}/" in vma_obj[vma]]
        if len(claimants) == 1 or len(own) == 1:
            out.append(claimants[0] if len(claimants) == 1 else own[0])
        else:
            stats["shared"] += len(claimants)
    stats["ok"] = len(out)

    # Assets the names left unresolved (static copies, objects of another
    # version's layout): config.yml says which disc object each .inc is
    extracts = read_extracts(args.version)
    covered = {e[6] for e in out if e[3] & FLAG_ASSET}
    taken = {e[6] for e in out}
    for (obj, name), places in entries.items():
        header = source_info(obj)[1].get(local_static_var.get(name, name))
        if header is None or header not in extracts:
            continue
        module, spec = extracts[header]
        symbols, sections = disc_symbols[module]
        sym, _, place = spec.partition("!")
        hits = [(sec, off, size) for (n, sec, off, size) in symbols
                if n == sym and (not place or f"{sec}:0x{off:08X}" == place)]
        t = decl_type(obj, local_static_var.get(name, name))
        kind = TYPE_KINDS.get(t)
        if len(hits) != 1 or kind is None:
            continue
        sec, off, disc_size = hits[0]
        for vma, size in places:
            if vma not in taken and (size == disc_size or (obj.endswith(".obj") and disc_size < size <= (disc_size + 31) & ~31)):
                out.append((MODULES[module], 0 if module == "static" else sections[sec], kind, FLAG_ASSET, off, size, vma))
                taken.add(vma)
                covered.add(vma)
                stats["by .inc"] += 1
    stats["ok"] = len(out)

    # every asset the executable carries should come from the disc
    missing = sorted({(obj, name) for (obj, name), places in entries.items()
                      if is_asset(obj, name) and any(vma not in covered for vma, _ in places)})
    stats["assets"] = len(covered)

    blob = b"RELO" + struct.pack("<III", 2, len(out), anchor)
    for mod_id, sec, kind, flags, off, size, vma in out:
        blob += struct.pack("<BBBBIII", mod_id, sec, kind, flags, off, size, vma)
    Path(args.output).write_bytes(blob)
    top = sorted(stats.items(), key=lambda kv: -kv[1])[:12]
    print(f"rel overlay: {stats['ok']} objects; " + ", ".join(f"{k}={v}" for k, v in top if k != "ok"))
    if missing:
        print(f"rel overlay: {len(missing)} assets not found on the disc:", file=sys.stderr)
        for obj, name in missing[:30]:
            print(f"    {name} ({Path(obj).name})", file=sys.stderr)
        if args.require_assets:
            sys.exit(1)


if __name__ == "__main__":
    main()
