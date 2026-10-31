#!/usr/bin/env python3
"""Generate the byte-swap table for the e+ save data (pc/src/pc_save_swap_table.inc).

The PC port keeps its saves in the host's (little-endian) byte order; the
GameCube's memory card files are big-endian. Converting between the two needs
to know, for every field of the saved structures, how wide it is. Rather than
writing that list by hand, this script compiles a probe that includes the e+
headers from the decomp (with the port's TARGET_PC flags), reads the DWARF
debug info of the result and writes one table per structure.

It compiles the probe twice, for 32-bit and 64-bit x86, and stops if the two
layouts differ (the saves of both builds are the same bytes, so the table must
be too). It also stops on anything it cannot decide by itself:

  * a union whose members would be swapped differently, unless UNION_POLICY
    names the member to follow;
  * a structure the port reorders under TARGET_PC (TYPE_OVERRIDE gives its
    swap by hand);
  * pointers (they have no place in a save).

The generated file also carries offsetof/sizeof checks for every entry, so a
change to the e+ headers that moves a field breaks the build instead of the
saves.

Usage (needs gcc with -m32 support and pyelftools):
    python3 pc/tools/gen_save_swap.py [--decomp DIR] [--out FILE] [--check]

--check compares with the existing file instead of writing it (exit 1 if it
would change).
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PC_DIR = os.path.dirname(HERE)
ROOT = os.path.dirname(PC_DIR)

# Top-level structures stored in the save files
ROOTS = [
    "Save_t",                 # town data (main and backup copy in DobutsunomoriE_MURA)
    "mCD_keep_diary_c",       # diary, after the card header at the start of the town file
    "FamicomSaveDataHeader",  # header of the Famicom common save (DobutsunomoriE_F_SAVE)
]

PROBE = """\
#include "m_common_data.h"
#include "m_card.h"
#include "Famicom/famicom.h"
""" + "".join("%s probe_%d;\n" % (r, i) for i, r in enumerate(ROOTS))

# Unions: which member's layout the bytes follow. Key: the union's type name,
# or "<parent type>.<member name>" for an anonymous union. Value: a member name,
# or None when no member needs swapping.
UNION_POLICY = {
    # struct { u8 month, day } on the GameCube, struct { u8 day, month } under
    # TARGET_PC (m_event.h), so that `raw` is (month << 8) | day on both: the
    # bytes are one u16.
    "mEv_MonthDay_u": "raw",
    # mEv_schedule_date_c is { month, day, _2, hour } on the GameCube and
    # { day, month, hour, _2 } under TARGET_PC, so `md` (offset 0) and the
    # u16 at offset 2 keep their values: two u16s, not the u32 `raw`.
    "mEv_schedule_date_u": "d",  # (not in the saved structures today)
    # Tagged by the errand_type bit-field of the mQst_errand_c holding it:
    # `first_job` ends in a bit-field byte where `chain` has villager IDs.
    # pc_save_swap.c picks the member's table from the tag.
    "mQst_errand_info_u": ("custom", "pc_swap_errand_info"),
    # Tagged by mQst_contest_c.type: only letter_data has a u16 (present);
    # flower_data and grass_data are a byte at offset 0, which a u16 swap
    # would move.
    "mQst_contest_info_u": ("custom", "pc_swap_contest_info"),
    # Tagged by mEv_special_c.kind (an mEv_SPNPC_* value; its subtype 0-5 is
    # the member's index: bargin, designer, broker, artist, arabian, gypsy).
    # No event (-1): the bytes are left as they are.
    "mEv_special_u": ("custom", "pc_swap_special_event"),
    # Only kabu_peddler is used (m_event.c, ac_ev_kabuPeddler_move.c_inc);
    # dozaemon appears nowhere in the decomp's sources.
    "mEv_weekly_u": "kabu_peddler",
    # The game writes `raw` itself (bit 7 = used_all_locations, the low 7 bits
    # counted up by raw++: m_event.c, ac_event_manager.c, ac_ev_soncho_talk):
    # the byte's value is the same on both machines, no repacking.
    "Save_t.event_save_common.bridge_flags": "raw",
    # Used as its two s16 halves (m_post_office.c, ac_npc_post_man_move);
    # `raw` is only set to 0x000F000F, the same either way.
    "Save_t.post_office.leaflet_recipient_flags": "<anon>",
}

# Unions whose member depends on where the structure holding them is saved:
# name -> (default member, [(substring of the field path, member), ...]).
# The structures on the path get one table per choice.
CONTEXT_UNION_POLICY = {
    # Anmmem_c.memuni: a town villager remembers the player's town (`land`:
    # name bytes + u16 id, m_npc.c); the islander keeps furniture flags
    # (`island`: u32 check + u16 have_bitfield, m_island.c, m_npc_island.c_inc).
    "memuni_u": ("land", [(".island.animal.", "island")]),
    # Animal_c.anmuni: a town villager keeps the town it came from (an
    # unnamed struct: name bytes + u16 previous_land_id, m_npc.c); the islander
    # its furniture (`island_ftr`: u16[4], m_npc_island.c_inc). None = the
    # unnamed member.
    "anmuni_u": (None, [(".island.animal.", "island_ftr")]),
}

# Structures reordered under TARGET_PC: their swap, given by hand as
# (offset, width) pairs. Each needs a reason.
TYPE_OVERRIDE = {
    # see mEv_schedule_date_u above: two u16 halves (not in the saved
    # structures today; here so that adding it cannot go unnoticed)
    "mEv_schedule_date_c": [(0, 2), (2, 2)],
}


def die_name(die):
    a = die.attributes.get("DW_AT_name")
    return a.value.decode() if a else None


def strip_cv(die):
    while die.tag in ("DW_TAG_const_type", "DW_TAG_volatile_type", "DW_TAG_restrict_type", "DW_TAG_atomic_type"):
        die = die.get_DIE_from_attribute("DW_AT_type")
    return die


class Gen:
    def __init__(self, elf_path):
        from elftools.elf.elffile import ELFFile

        self.f = open(elf_path, "rb")
        self.elf = ELFFile(self.f)
        self.dwarf = self.elf.get_dwarf_info()
        self.typedefs = {}       # name -> DIE
        self.typedef_of = {}     # struct DIE offset -> typedef name
        for cu in self.dwarf.iter_CUs():
            for die in cu.iter_DIEs():
                if die.tag == "DW_TAG_typedef" and "DW_AT_type" in die.attributes:
                    name = die_name(die)
                    self.typedefs[name] = die
                    t = strip_cv(die.get_DIE_from_attribute("DW_AT_type"))
                    if t.tag in ("DW_TAG_structure_type", "DW_TAG_union_type"):
                        self.typedef_of.setdefault(t.offset, name)
        self.tables = {}         # key -> table dict
        self.order = []          # table keys, dependencies first
        self.unions_seen = {}    # union key -> chosen member
        self.overrides_used = set()

    # --- type helpers ---
    def resolve(self, die):
        """Follow typedefs/cv to the underlying type, remembering the last typedef name."""
        name = None
        while True:
            die = strip_cv(die)
            if die.tag == "DW_TAG_typedef":
                name = name or die_name(die)
                if name in TYPE_OVERRIDE:
                    return die, name
                die = die.get_DIE_from_attribute("DW_AT_type")
                continue
            return die, name

    def type_name(self, die, typedef_name=None):
        if typedef_name:
            return typedef_name
        if die.offset in self.typedef_of:
            return self.typedef_of[die.offset]
        n = die_name(die)
        if n:
            return ("struct " if die.tag == "DW_TAG_structure_type" else "union ") + n
        return None

    def byte_size(self, die):
        die, _ = self.resolve(die)
        if die.tag == "DW_TAG_typedef":  # an override
            return self.byte_size(die.get_DIE_from_attribute("DW_AT_type"))
        if die.tag == "DW_TAG_array_type":
            n = 1
            for d in self.dims(die):
                n *= d
            return n * self.byte_size(die.get_DIE_from_attribute("DW_AT_type"))
        return die.attributes["DW_AT_byte_size"].value

    def dims(self, arr):
        out = []
        for c in arr.iter_children():
            if c.tag != "DW_TAG_subrange_type":
                continue
            if "DW_AT_count" in c.attributes:
                out.append(c.attributes["DW_AT_count"].value)
            elif "DW_AT_upper_bound" in c.attributes:
                out.append(c.attributes["DW_AT_upper_bound"].value + 1)
            else:
                out.append(0)  # flexible array
        return out

    # --- layout ---
    def flat(self, die, ofs=0, ctx=""):
        """All swaps of a type as a flat, sorted list of (offset, kind, data);
        used to compare union members."""
        out = []
        for e in self.entries_of(die, ctx):
            if e["kind"] == "custom":
                out.append((ofs + e["ofs"], "c", e["fn"]))
            elif e["kind"] == "scalar":
                for i in range(e["count"]):
                    out.append((ofs + e["ofs"] + i * e["width"], "s", e["width"]))
            elif e["kind"] == "bits":
                out.append((ofs + e["ofs"], "b", e["width"], tuple(e["fields"])))
            else:
                sub = self.tables[e["table"]]
                sflat = self.flat_table(sub)
                for i in range(e["count"]):
                    out.extend((o + ofs + e["ofs"] + i * e["stride"],) + tuple(rest) for (o, *rest) in sflat)
        return sorted(out)

    def flat_table(self, t):
        return self.flat(t["die"], 0, t["ctx"])

    def entries_of(self, die, ctx=""):
        """Entries for a value of type `die` placed at offset 0 (inlined form)."""
        die, tdname = self.resolve(die)
        if tdname in TYPE_OVERRIDE:
            self.overrides_used.add(tdname)
            return [dict(kind="scalar", ofs=o, width=w, count=1, path=None) for o, w in TYPE_OVERRIDE[tdname]]
        tag = die.tag
        if tag == "DW_TAG_base_type" or tag == "DW_TAG_enumeration_type":
            w = die.attributes["DW_AT_byte_size"].value
            if w == 1:
                return []
            if w not in (2, 4, 8):
                raise SystemExit("unexpected scalar width %d in %s" % (w, ctx))
            return [dict(kind="scalar", ofs=0, width=w, count=1, path=None)]
        if tag == "DW_TAG_pointer_type":
            raise SystemExit("pointer in saved data: %s" % ctx)
        if tag == "DW_TAG_array_type":
            n = 1
            for d in self.dims(die):
                n *= d
            elem = die.get_DIE_from_attribute("DW_AT_type")
            esz = self.byte_size(elem)
            sub = self.entries_of(elem, ctx + "[]")
            if not sub:
                return []
            if len(sub) == 1 and sub[0]["kind"] == "scalar" and sub[0]["ofs"] == 0 and sub[0]["width"] * sub[0]["count"] == esz:
                return [dict(kind="scalar", ofs=0, width=sub[0]["width"], count=sub[0]["count"] * n, path=None)]
            key = self.table_for(elem, ctx + "[]")
            return [dict(kind="struct", ofs=0, table=key, count=n, stride=esz, path=None)]
        if tag == "DW_TAG_structure_type":
            return self.struct_entries(die, ctx)
        if tag == "DW_TAG_union_type":
            return self.union_entries(die, tdname, ctx)
        raise SystemExit("unexpected type %s in %s" % (tag, ctx))

    def union_entries(self, die, tdname, ctx):
        name = self.type_name(die, tdname) or ctx
        members = [m for m in die.iter_children() if m.tag == "DW_TAG_member"]
        if name in CONTEXT_UNION_POLICY:
            pick = self.context_pick(name, ctx)
            self.unions_seen["%s (%s)" % (name, pick)] = "by field path"
            for m in members:
                if die_name(m) == pick:
                    sub = self.entries_of(m.get_DIE_from_attribute("DW_AT_type"), ctx + "." + (pick or "<anon>"))
                    return [dict(e, path=self.join(pick, e["path"])) for e in sub]
            raise SystemExit("union %s has no member %s" % (name, pick))
        if name in UNION_POLICY:
            pick = UNION_POLICY[name]
            self.unions_seen[name] = pick if not isinstance(pick, tuple) else "%s() by tag" % pick[1]
            if pick is None:
                return []
            if isinstance(pick, tuple):  # ("custom", C handler): a table per member, the handler picks
                for m in members:
                    self.table_for(m.get_DIE_from_attribute("DW_AT_type"), ctx + "." + die_name(m),
                                   key="%s_%s" % (name, die_name(m)))
                return [dict(kind="custom", ofs=0, fn=pick[1], size=die.attributes["DW_AT_byte_size"].value, path=None)]
            for m in members:
                if die_name(m) == pick or (pick == "<anon>" and die_name(m) is None):
                    mn = die_name(m)
                    sub = self.entries_of(m.get_DIE_from_attribute("DW_AT_type"), ctx + "." + pick)
                    return [dict(e, path=self.join(mn, e["path"])) for e in sub]
            raise SystemExit("union %s has no member %s" % (name, pick))
        flats = [self.flat(m.get_DIE_from_attribute("DW_AT_type"), 0, ctx) for m in members]
        if all(f == flats[0] for f in flats):
            self.unions_seen[name] = "(all members agree)"
            m = members[0]
            sub = self.entries_of(m.get_DIE_from_attribute("DW_AT_type"), ctx)
            mn = die_name(m)
            return [dict(e, path=self.join(mn, e["path"]) if mn else e["path"]) for e in sub]
        raise SystemExit("union %s (in %s) needs a UNION_POLICY entry: members %s swap differently"
                         % (name, ctx, [die_name(m) for m in members]))

    @staticmethod
    def context_pick(name, ctx):
        default, rules = CONTEXT_UNION_POLICY[name]
        for sub, member in rules:
            if sub in ctx + ".":
                return member
        return default

    def contains_union(self, die, uname, seen=None):
        """Does the type hold (at any depth) a union named uname?"""
        die, tdname = self.resolve(die)
        memo = self.__dict__.setdefault("_contains", {})
        k = (die.offset, uname)
        if k in memo:
            return memo[k]
        memo[k] = False
        r = False
        if die.tag == "DW_TAG_union_type" and self.type_name(die, tdname) == uname:
            r = True
        elif die.tag == "DW_TAG_array_type":
            r = self.contains_union(die.get_DIE_from_attribute("DW_AT_type"), uname)
        elif die.tag in ("DW_TAG_structure_type", "DW_TAG_union_type"):
            r = any(self.contains_union(m.get_DIE_from_attribute("DW_AT_type"), uname)
                    for m in die.iter_children() if m.tag == "DW_TAG_member")
        memo[k] = r
        return r

    @staticmethod
    def join(a, b):
        if a is None:
            return b
        if b is None:
            return a
        return a + ("" if b.startswith("[") else ".") + b

    def struct_entries(self, die, ctx):
        out = []
        bits = {}  # (unit ofs, unit width) -> fields
        for m in die.iter_children():
            if m.tag != "DW_TAG_member":
                continue
            mname = die_name(m)
            mtype = m.get_DIE_from_attribute("DW_AT_type")
            if "DW_AT_bit_size" in m.attributes:
                w = self.byte_size(mtype)
                bsz = m.attributes["DW_AT_bit_size"].value
                if "DW_AT_data_bit_offset" in m.attributes:
                    boff = m.attributes["DW_AT_data_bit_offset"].value
                else:
                    raise SystemExit("bit-field without DW_AT_data_bit_offset (need DWARF 4+): %s" % ctx)
                unit = (boff // (w * 8)) * w
                k = boff - unit * 8
                if k + bsz > w * 8:
                    raise SystemExit("bit-field %s.%s crosses its unit" % (ctx, mname))
                bits.setdefault((unit, w), []).append((k, bsz, mname))
                continue
            mofs = m.attributes["DW_AT_data_member_location"].value
            sub = self.entries_of(mtype, ctx + "." + (mname or "<anon>"))
            for e in sub:
                e = dict(e)
                e["ofs"] += mofs
                e["path"] = self.join(mname, e["path"]) if mname else e["path"]
                out.append(e)
        for (unit, w), fields in bits.items():
            fields.sort()
            # Only the bytes the fields occupy belong to them: a u32 unit
            # holding 16 bits of fields shares its other two bytes with the
            # next members (mQst_base_c: time_limit at offset 2). Counting
            # from the start of the unit, the fields' bytes are the same on
            # both machines (MSB first on the GameCube, LSB first on the PC),
            # so the repack works on that byte range alone.
            lo = min(k for k, _, _ in fields) // 8
            hi = (max(k + b for k, b, _ in fields) + 7) // 8
            out.append(dict(kind="bits", ofs=unit + lo, width=hi - lo,
                            fields=[(k - lo * 8, b) for k, b, _ in fields],
                            names=[n for _, _, n in fields], path=None))
        out.sort(key=lambda e: e["ofs"])
        return out

    def table_for(self, die, ctx, key=None):
        die, tdname = self.resolve(die)
        name = self.type_name(die, tdname)
        key = key or name or ("anon_" + ctx.strip(".").replace(".", "_").replace("[]", "").replace("<", "").replace(">", ""))
        for uname in sorted(CONTEXT_UNION_POLICY):
            pick = self.context_pick(uname, ctx)
            if pick != CONTEXT_UNION_POLICY[uname][0] and self.contains_union(die, uname):
                key += "__" + pick
        if key in self.tables:
            return key
        self.tables[key] = None  # recursion guard
        if tdname in TYPE_OVERRIDE:
            die = self.typedefs[tdname]  # entries_of applies the override
        self.tables[key] = dict(key=key, cname=name, die=die, ctx=ctx, size=self.byte_size(die),
                                entries=self.entries_of(die, ctx))
        self.order.append(key)
        return key

    def run(self):
        for r in ROOTS:
            if r not in self.typedefs:
                raise SystemExit("type %s not found" % r)
            self.table_for(self.typedefs[r], r)
        return self


def c_ident(key):
    return "pc_swt_" + "".join(ch if ch.isalnum() else "_" for ch in key.replace("struct ", "").replace("union ", ""))


def describe(gen):
    """A layout description used to compare the 32-bit and 64-bit builds."""
    out = []
    for k in gen.order:
        t = gen.tables[k]
        out.append((k, t["size"], tuple((e["kind"], e["ofs"], e.get("width"), e.get("count"), e.get("table"),
                                         e.get("stride"), tuple(e.get("fields", ())), e.get("fn")) for e in t["entries"])))
    return out


def emit(gen):
    lines = []
    w = lines.append
    w("/* pc_save_swap_table.inc - GENERATED by pc/tools/gen_save_swap.py from the e+ headers")
    w(" * in decomp/include. Do not edit: change the headers or the script and run it again.")
    w(" *")
    w(" * One table per saved structure: the fields wider than a byte, with their")
    w(" * offset, width and count, nested structures by table, and bit-fields with")
    w(" * their little-endian bit positions. Unions follow the member named in the")
    w(" * script's UNION_POLICY:")
    for u, m in sorted(gen.unions_seen.items()):
        w(" *   %-28s -> %s" % (u, m))
    w(" * Structures swapped by hand (TYPE_OVERRIDE): %s" % (", ".join(sorted(gen.overrides_used)) or "none"))
    w(" */")
    w("")
    bit_arrays = {}
    for k in gen.order:
        for e in gen.tables[k]["entries"]:
            if e["kind"] == "bits":
                key = tuple(e["fields"])
                if key not in bit_arrays:
                    bit_arrays[key] = "pc_swb_%d" % len(bit_arrays)
                    w("static const pc_swap_bitfield_t %s[] = { %s };"
                      % (bit_arrays[key], ", ".join("{%d, %d}" % f for f in key)))
    w("")
    for k in gen.order:
        t = gen.tables[k]
        w("static const pc_swap_entry_t %s[] = {" % c_ident(k))
        for e in t["entries"]:
            label = e["path"] or ""
            if e["kind"] == "scalar":
                w("    PC_SWAP_SCALAR(0x%05X, %d, %d), /* %s */" % (e["ofs"], e["width"], e["count"], label))
            elif e["kind"] == "custom":
                w("    PC_SWAP_CUSTOM(0x%05X, %s, 0x%X), /* %s */" % (e["ofs"], e["fn"], e["size"], label))
            elif e["kind"] == "struct":
                w("    PC_SWAP_STRUCT(0x%05X, %s, %d, 0x%X), /* %s */" % (e["ofs"], c_ident(e["table"]), e["count"], e["stride"], label))
            else:
                w("    PC_SWAP_BITS(0x%05X, %d, %d, %s), /* %s */"
                  % (e["ofs"], e["width"], len(e["fields"]), bit_arrays[tuple(e["fields"])], ", ".join(e["names"])))
        w("    PC_SWAP_END")
        w("};")
        w("")
    # checks against the headers
    w("/* The tables against the headers: sizes, offsets and field widths */")
    for k in gen.order:
        t = gen.tables[k]
        cn = t["cname"]
        if not cn:
            continue
        w("PC_SWAP_CHECK(sizeof(%s) == 0x%X);" % (cn, t["size"]))
        for e in t["entries"]:
            p = e["path"]
            if not p or e["kind"] in ("bits", "custom"):
                continue
            # path may end inside an array element (union member etc.): only check named members
            w("PC_SWAP_CHECK(offsetof(%s, %s) == 0x%X);" % (cn, p, e["ofs"]))
            if e["kind"] == "scalar":
                w("PC_SWAP_CHECK(sizeof(((%s*)0)->%s) == %d);" % (cn, p, e["width"] * e["count"]))
            else:
                w("PC_SWAP_CHECK(sizeof(((%s*)0)->%s) == 0x%X);" % (cn, p, e["stride"] * e["count"]))
    w("")
    for r in ROOTS:
        w("#define PC_SWAP_TABLE_%s %s" % (r, c_ident(r)))
    return "\n".join(lines) + "\n"


def compile_probe(decomp, arch, tmp):
    src = os.path.join(tmp, "probe.c")
    with open(src, "w") as f:
        f.write(PROBE)
    obj = os.path.join(tmp, "probe%s.o" % arch)
    inc = os.path.join(decomp, "include")
    cmd = ["gcc", "-m%s" % arch, "-malign-double", "-g", "-gdwarf-5", "-c", "-std=gnu11",
           "-DTARGET_PC", "-DVERSION=1", "-DF3DEX_GBI_2", "-DNDEBUG", "-DBUGFIXES", "-D_LANGUAGE_C",
           "-Dnullptr=NULL", "-fno-strict-aliasing", "-w",
           "-I" + inc, "-I" + os.path.join(decomp, "src"), "-I" + decomp, "-I" + os.path.join(PC_DIR, "include"),
           "-idirafter", os.path.join(inc, "dolphin"), src, "-o", obj]
    if arch == "32":
        cmd[3:3] = ["-msse2", "-mfpmath=sse"]
    subprocess.run(cmd, check=True)
    return obj


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--decomp", default=os.path.join(ROOT, "decomp"))
    ap.add_argument("--out", default=os.path.join(PC_DIR, "src", "pc_save_swap_table.inc"))
    ap.add_argument("--check", action="store_true", help="compare with the existing file")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        g32 = Gen(compile_probe(args.decomp, "32", tmp)).run()
        g64 = Gen(compile_probe(args.decomp, "64", tmp)).run()
        if describe(g32) != describe(g64):
            for a, b in zip(describe(g32), describe(g64)):
                if a != b:
                    print("32-bit: %r\n64-bit: %r" % (a[:2], b[:2]), file=sys.stderr)
            raise SystemExit("the 32-bit and 64-bit layouts differ")
        text = emit(g64)

    unused = set(UNION_POLICY) - set(g64.unions_seen) - {"mEv_schedule_date_u"}  # kept on purpose, see above
    if unused:
        print("note: UNION_POLICY entries not used: %s" % ", ".join(sorted(unused)), file=sys.stderr)

    if args.check:
        old = open(args.out).read() if os.path.exists(args.out) else ""
        if old != text:
            print("%s is out of date" % args.out, file=sys.stderr)
            return 1
        print("%s is up to date" % args.out)
        return 0
    with open(args.out, "w") as f:
        f.write(text)
    print("wrote %s: %d tables" % (args.out, len(g64.order)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
