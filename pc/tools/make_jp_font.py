#!/usr/bin/env python3
"""Cuts the menus' Japanese font (pc/res/gui_font_jp.ttf, "AFE Menu Rounded")
out of M PLUS Rounded 1c.

The menu font (pc/res/gui_font.ttf, NanumGothic) has no kana or kanji. The
full touch mode held upright draws the original disc's answers as buttons
with ImGui, merged with this font (pc_gui.cpp): every character the game's
font has (pc/tools/gen_jp_charset.py --chars: the kana and signs, the kanji
of its two kanji pages), the whole kana blocks, CJK punctuation and the
full-width forms. M PLUS Rounded 1c is under the SIL Open Font License 1.1
(pc/res/gui_font_jp_OFL.txt).

    python3 pc/tools/make_jp_font.py MPLUSRounded1c-Medium.ttf pc/res/gui_font_jp.ttf

MPLUSRounded1c-Medium.ttf: Google Fonts, "M PLUS Rounded 1c" (weight 500).
Needs fontTools (pip install fonttools); DECOMP_ROOT as gen_jp_charset.py.
"""
import os
import subprocess
import sys

from fontTools import subset

FAMILY = "AFE Menu Rounded"
HERE = os.path.dirname(os.path.abspath(__file__))


def codepoints():
    chars = subprocess.run([sys.executable, os.path.join(HERE, "gen_jp_charset.py"), "--chars"], check=True,
                           capture_output=True, text=True).stdout
    cps = {ord(c) for c in chars}
    cps |= set(range(0x20, 0x7F))
    cps |= set(range(0x3000, 0x3040))  # CJK symbols and punctuation
    cps |= set(range(0x3041, 0x3097)) | set(range(0x309B, 0x30A0))  # hiragana
    cps |= set(range(0x30A1, 0x3100))  # katakana
    cps |= set(range(0xFF01, 0xFF5F)) | set(range(0xFF61, 0xFFA0))  # full and half width forms
    return sorted(cps)


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
    # a subset is a Modified Version under the OFL: its own family name,
    # keeping the copyright (0) and license (13, 14) records
    for rec in list(font["name"].names):
        if rec.nameID in (1, 3, 4, 6, 16):
            rec.string = FAMILY if rec.nameID != 6 else FAMILY.replace(" ", "")
    subset.save_font(font, sys.argv[2], opts)
    print(f"{sys.argv[2]}: {len(font.getBestCmap())} characters, {os.path.getsize(sys.argv[2])} bytes")


if __name__ == "__main__":
    main()
