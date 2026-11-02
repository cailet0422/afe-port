#!/usr/bin/env python3
"""Cuts the menu font (pc/res/gui_font.ttf, "AFE Menu Gothic") out of NanumGothic.

The port's own menus (pc/src/gui) draw with Dear ImGui and need Hangul for
their Korean labels, folder names and player names. NanumGothic (SIL Open Font
License 1.1, pc/res/gui_font_OFL.txt) has far more than that, so this keeps
ASCII, Latin-1, the punctuation and symbols the menus use, and the non-Hanja
part of KS X 1001 (the 2,350 Hangul syllables the Korean patch can show,
compatibility jamo, kana for names from Japanese saves, Greek, box drawing).

    python3 pc/tools/make_gui_font.py NanumGothic-Regular.ttf pc/res/gui_font.ttf

NanumGothic-Regular.ttf: github.com/google/fonts, ofl/nanumgothic.
Needs fontTools (pip install fonttools).
"""
import sys

from fontTools import subset

FAMILY = "AFE Menu Gothic"


def codepoints():
    cps = set(range(0x20, 0x7F)) | set(range(0xA0, 0x100))
    cps |= set(range(0x2010, 0x2070))  # general punctuation
    cps |= {0x2190, 0x2191, 0x2192, 0x2193, 0x25B2, 0x25B6, 0x25BC, 0x25C0, 0x25CF, 0x25CB,
            0x2605, 0x2606, 0x266A, 0x2713, 0x2714, 0x00D7, 0x2026, 0x00B7, 0x3001, 0x3002,
            0x300C, 0x300D, 0x30FB, 0x30FC, 0xFF5E}
    cps |= set(range(0x3131, 0x318F))  # compatibility jamo
    # every character of KS X 1001 but the Hanja (lead bytes 0xCA-0xFD)
    for lead in range(0xA1, 0xCA):
        for trail in range(0xA1, 0xFF):
            try:
                ch = bytes((lead, trail)).decode("euc_kr")
            except UnicodeDecodeError:
                continue
            cps.add(ord(ch))
    return sorted(c for c in cps if not (0x4E00 <= c <= 0x9FFF or 0xF900 <= c <= 0xFAFF))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    opts = subset.Options()
    opts.layout_features = []  # ImGui does no shaping
    opts.hinting = False
    opts.name_IDs = ["*"]      # keep the copyright and license notices
    opts.notdef_outline = True
    opts.drop_tables += ["DSIG", "GSUB", "GPOS", "GDEF"]
    font = subset.load_font(sys.argv[1], opts)
    sub = subset.Subsetter(opts)
    sub.populate(unicodes=codepoints())
    sub.subset(font)
    # A subset is a Modified Version under the OFL, which may not keep the
    # Reserved Font Names ("Nanum", "NanumGothic"): rename it, keeping the
    # copyright (0) and license (13, 14) records
    names = font["name"]
    for rec in list(names.names):
        if rec.nameID in (1, 16):
            rec.string = FAMILY
        elif rec.nameID in (4, 18):
            rec.string = FAMILY + " Regular"
        elif rec.nameID == 3:
            rec.string = FAMILY + ";afe-port"
        elif rec.nameID == 6:
            rec.string = FAMILY.replace(" ", "") + "-Regular"
        elif rec.nameID not in (0, 2, 5, 13, 14):
            names.names.remove(rec)
    subset.save_font(font, sys.argv[2], opts)
    print("wrote %s (%d glyphs)" % (sys.argv[2], len(font.getGlyphOrder())))


if __name__ == "__main__":
    main()
