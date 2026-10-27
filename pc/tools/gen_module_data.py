#!/usr/bin/env python3
"""Generate C sources and assets for REL data that has no C source yet.

The indoor (foresti) and outdoor (foresto) data modules are still linked from
dtk's generated assembly in the matching build, so the PC build has nothing to
compile for them. This script converts them with the decomp's own
tools/converters/batch_assetdis.py (staging only, the project is not modified)
and extracts the assets the generated sources #include:

    OUT/assetdis/MODULE/src/data/MODULE/*.c   generated C translation units
    OUT/include/assets/...                    .inc files (raw, pal16, vtx)
    OUT/module_data.cmake                     GENERATED_DATA_SOURCES list

Global names that are already defined elsewhere in the PC build (the same
model shipped in two modules, or data that also lives in forestd) are renamed
inside the later translation unit so the link sees one definition.

Requires the matching build's split step (build/VERSION/MODULE/asm).

usage: gen_module_data.py VERSION OUT_DIR
"""
import importlib.util
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULES = ("foresti", "foresto")
DATA_SECTIONS = {".data", ".rodata", ".sdata", ".sdata2"}
# Modules whose objects are already compiled from src/ in the PC build.
COMPILED_MODULES = ("forestd", "foresta")

GLOBAL_DEF = re.compile(r"^(?!static\b)(?:const\s+)?[A-Za-z_]\w*\s*\*?\s+([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*(?:ATTRIBUTE_ALIGN\(\d+\)\s*)?=", re.M)


def load_batch_assetdis():
    path = ROOT / "tools" / "converters" / "batch_assetdis.py"
    spec = importlib.util.spec_from_file_location("batch_assetdis", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules["batch_assetdis"] = module
    spec.loader.exec_module(module)
    return module


def parse_fragment(path):
    """Parse the simple YAML extract list batch_assetdis writes."""
    entries = []
    current = None
    for line in path.read_text(encoding="utf-8").splitlines():
        m = re.match(r"^\s*-\s+symbol:\s*(\S+)\s*$", line)
        if m:
            current = {"symbol": m.group(1)}
            entries.append(current)
            continue
        m = re.match(r"^\s+(\w+):\s*(\S+)\s*$", line)
        if m and current is not None:
            current[m.group(1)] = m.group(2)
    return entries


def global_symbols(version, modules):
    names = set()
    paths = [ROOT / "config" / version / "symbols.txt"]
    paths += [ROOT / "config" / version / module / "symbols.txt" for module in modules]
    for path in paths:
        for line in path.read_text(encoding="utf-8").splitlines():
            if "scope:global" in line:
                names.add(line.split("=", 1)[0].strip())
    return names


def write_raw_inc(data, path):
    lines = []
    for i in range(0, len(data), 16):
        lines.append(", ".join(f"0x{b:02X}" for b in data[i : i + 16]) + ",")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def extract_assets(bad, version, module, work, out):
    symbols = bad.read_symbols(ROOT / "config" / version / module / "symbols.txt", DATA_SECTIONS)
    objects, _warnings = bad.index_assembly(ROOT / "build" / version / module / "asm", symbols)
    by_name = {}
    for obj in objects.values():
        by_name.setdefault(obj.symbol.name, []).append(obj)

    count = 0
    for entry in parse_fragment(work / "extract.fragment.yml"):
        symbol = entry["symbol"]
        if "!" in symbol:
            name, location = symbol.split("!", 1)
            section, address = location.split(":", 1)
            obj = objects[(section, int(address, 16))]
        else:
            matches = by_name.get(symbol, [])
            if len(matches) != 1:
                raise ValueError(f"{module}: cannot resolve extract symbol {symbol} ({len(matches)} matches)")
            obj = matches[0]
        if obj.error:
            raise ValueError(f"{module}: {symbol}: {obj.error}")

        bin_path = out / "bin" / entry["binary"]
        inc_path = out / "include" / entry["header"]
        bin_path.parent.mkdir(parents=True, exist_ok=True)
        inc_path.parent.mkdir(parents=True, exist_ok=True)
        bin_path.write_bytes(obj.data)

        custom = entry.get("custom_type")
        if custom in ("pal16", "vtx"):
            tool = ROOT / "tools" / "converters" / ("pal16dis.py" if custom == "pal16" else "vtxdis.py")
            subprocess.run([sys.executable, str(tool), str(bin_path), str(inc_path)], check=True)
        elif custom is None and entry.get("header_type") == "raw":
            write_raw_inc(obj.data, inc_path)
        else:
            raise ValueError(f"{module}: unsupported asset kind for {symbol}: {entry}")
        count += 1
    return count


RENAME_MARKER = "/* Renamed: also defined by an earlier module in the PC build */\n"


def rename_duplicates(source_path, defined, module):
    text = source_path.read_text(encoding="utf-8")
    if text.startswith(RENAME_MARKER):
        # Already processed by an earlier run
        dups = re.findall(r"^#define (\w+) \w+__\w+_dup$", text, re.M)
        names = GLOBAL_DEF.findall(text)
        defined.update(n for n in names if n not in dups)
        return len(dups)
    names = GLOBAL_DEF.findall(text)
    dups = [n for n in names if n in defined]
    if dups:
        header = "".join(f"#define {n} {n}__{module}_dup\n" for n in dups)
        source_path.write_text(RENAME_MARKER + header + text, encoding="utf-8")
    defined.update(n for n in names if n not in dups)
    return len(dups)


def missing_units(version):
    """Objects listed in configure.py whose source file does not exist, by module.

    The matching build silently links these from dtk's assembly."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from gen_sources import load_config

    config = load_config(version)
    missing = []
    for lib in config.libs:
        src_dir = Path(lib.get("src_dir", "src"))
        for obj in lib["objects"]:
            source = obj.options.get("source") or obj.name
            if not (ROOT / src_dir / source).exists():
                missing.append(Path(source).as_posix())

    by_module = {}
    for module in COMPILED_MODULES:
        splits = (ROOT / "config" / version / module / "splits.txt").read_text(encoding="utf-8")
        for unit in missing:
            if re.search(rf"^{re.escape(unit)}:\s*$", splits, re.M):
                by_module.setdefault(module, []).append(unit)
    return by_module


def run_batch_assetdis(version, module, work, units=()):
    if (work / "report.json").exists():
        return
    args = [sys.executable, str(ROOT / "tools" / "converters" / "batch_assetdis.py"),
            "--module", module, "-v", version, "--out", str(work), "--run"]
    for unit in units:
        args += ["--unit", unit]
    subprocess.run(args, cwd=ROOT, check=True, stdout=subprocess.DEVNULL)


def main():
    version, out = sys.argv[1], Path(sys.argv[2]).resolve()
    bad = load_batch_assetdis()
    defined = global_symbols(version, COMPILED_MODULES)
    sources = []

    # Units that configure.py lists for a compiled module but that have no
    # source file are canonical definitions: generate them first, unrenamed.
    jobs = []
    for module, units in missing_units(version).items():
        jobs.append((module, out / "assetdis" / f"{module}_missing", tuple(units)))
    jobs += [(module, out / "assetdis" / module, ()) for module in MODULES]

    for module, work, units in jobs:
        run_batch_assetdis(version, module, work, units)
        assets = extract_assets(bad, version, module, work, out)

        renamed = 0
        module_sources = sorted((work / "src").rglob("*.c"))
        for path in module_sources:
            if units:
                defined.update(GLOBAL_DEF.findall(path.read_text(encoding="utf-8")))
            else:
                renamed += rename_duplicates(path, defined, module)
        sources += module_sources
        print(f"{work.name}: {len(module_sources)} sources, {assets} assets, {renamed} duplicate names renamed")

    lines = "\n".join(f'    "{p.as_posix()}"' for p in sources)
    (out / "module_data.cmake").write_text(
        f"# Generated by pc/tools/gen_module_data.py for {version}; do not edit.\n"
        f"set(GENERATED_DATA_SOURCES\n{lines}\n)\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
