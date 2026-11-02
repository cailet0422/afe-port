**English** · [한국어](texture_pack.ko.md)

# HD texture packs

The port reads HD texture packs made for Dolphin's "Load Custom Textures": files named
`tex1_{W}x{H}[_m]_{hash}[_{palette hash}|_$]_{format}.dds` (or `.png`) that replace the game's textures by the hash
of their data. `pc/src/pc_texture_pack.c`, the settings page `pc/src/gui/pc_gui_textures.cpp`.

## The pack this was made for

**Animal Crossing HD Texture Pack, version 25 (October 11th 2026)**, redrawn by hand (not upscaled) by TechieAndroid
and the contributors of the Dolphin forums project since 2016. It is not part of this repository and the port does not
ship it; users download it themselves.

- Original post: <https://forums.dolphin-emu.org/Thread-animal-crossing-hd-texture-pack-version-25-october-11th-2026>
- Download: <https://drive.google.com/file/d/1Z8-pMx20xO8q3caiqy4NOrSNVT1veDSk/view> (`ACHD V25.zip`, 160 MB)

The game's Settings > HD textures page names both, with the pack's own title and version read from its `Readme.txt`.
The pack is made for the North American Animal Crossing (`GAF`), with the e+ version (`GAE`) in mind: textures that are
the same in both are replaced, those with English text in the American version simply never match e+.

## Installing

Settings > **HD textures** > **Install a texture pack (zip)...** opens the system's file picker (Windows, Android; on
Linux zenity or kdialog when one is installed). The chosen zip is copied into `texture_pack/` next to the game (the
app's folder on Android) on a thread, checked to be a zip with Dolphin texture names, and turned on: textures change as
the game draws them, without a restart. Without a picker, put the zip as it is into `texture_pack/` and press
**Read the folder again**.

- Zips are read **in place**: the index of the zip's central directory is built once, and each texture's local header
  and deflated bytes are read and inflated when the game first draws it. The 160 MB zip stays 160 MB (unpacked it is
  1.27 GB).
- Folders of unpacked files below `texture_pack/` (Dolphin's layout) work too; a texture found in a folder wins over a
  zip's.
- Each zip can be deleted from the page. `hd_textures = 0` in `settings.ini` (the page's switch) turns packs off at once
  (every texture is decided again).

## Loading

- **While playing** (default, `preload_textures = 0`): a texture with a replacement is queued for a thread that reads,
  inflates and decodes it; the original is drawn until it is ready, then the game's texture cache drops the original
  (`pc_gx_texture_hd_ready`) and the next draw takes the HD one. No frame waits for the disk.
- **All at start** (`preload_textures = 1`): every texture of the pack is loaded before the title (19,061 textures, about
  1.3 GB of GPU memory for BC7).
- With `--deterministic` or an input script, textures load at once on the game thread so runs stay identical.
- BC7 (all of version 25), BC1 and BC3 DDS go to the GPU as they are when it has `GL_ARB/EXT_texture_compression_bptc`
  / `_s3tc`. Otherwise (most phones) they are decoded on the thread with bcdec (MIT/Unlicense, `pc/lib/bcdec`) to RGBA
  with mipmaps. PNG through stb_image (`pc/lib/stb`). `hd_quality = 1` (Size: Half, the Android default) halves them.
- Memory budget: least recently drawn textures are freed above 1.5 GB (BC7 on the GPU), 1 GB (decoded) or 320 MB on
  Android, and come back from the zip when drawn again (`pc_texture_pack_touch`, `pc_gx_texture_forget`).
- Hashes: XXH64 (seed 0) of the texture as the GameCube has it (big-endian tiles, the size of level 0 by format); for
  C4/C8/C14X2 textures the palette hash covers only the entries used (min..max index), big-endian. A `$` palette hash
  matches any palette. Names that are not Dolphin's (`..._8(1).dds`, `-export`) are left out (17 in version 25).

## With the Korean patch

Textures are matched by the hash of their exact data, so a texture the Korean patch changed (the logo
"안녕동물의숲 e+", name plates, the Hangul font and text in pictures) has another hash than the original and never
matches a pack made for the original game: it is drawn as the patch made it. While running in Korean the pack's
`Localisation/` folders (German and Spanish text, 203 textures in version 25) are not indexed at all.

Checked with `--deterministic` on the Korean-patched disc, the same scripts with and without the pack: the title
(Korean logo kept, the copyright line HD), K.K., name entry (Korean editor and keyboard kept), and Rover's dialogue on
the train (Korean text and name plate kept; Rover, seats and walls HD).
