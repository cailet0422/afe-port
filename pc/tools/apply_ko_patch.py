#!/usr/bin/env python3
"""Apply the community Korean patch (xdelta) to a disc image, CISO included.

The patch's own tool needs a full 1.4 GB ISO; this reads the source image
through gcdisc.Disc instead and uses the decoder shipped with the patch
(vcdiff.py, MIT). The result is accepted only if its SHA-256 matches the
patch manifest.

usage: apply_ko_patch.py SOURCE_IMAGE PATCH_DIR OUTPUT_ISO

PATCH_DIR is the unzipped patch folder (manifest.json, vcdiff.py, *.xdelta).
"""
import hashlib
import importlib.util
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gcdisc import Disc  # noqa: E402


def main():
    source, patch_dir, output = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    manifest = json.loads((patch_dir / "manifest.json").read_bytes())
    patch = (patch_dir / manifest["patch_filename"]).read_bytes()
    if hashlib.sha256(patch).hexdigest() != manifest["patch_sha256"]:
        sys.exit("patch file does not match its manifest")

    spec = importlib.util.spec_from_file_location("ko_vcdiff", patch_dir / "vcdiff.py")
    vcdiff = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(vcdiff)

    disc = Disc(source)
    target = vcdiff.decode(patch, disc.read)
    digest = hashlib.sha256(target).hexdigest()
    if digest != manifest["target_sha256"]:
        sys.exit(f"result {digest} does not match the manifest; is the source GAEJ01 Rev 1?")
    output.write_bytes(target)
    print(f"wrote {output} ({len(target)} bytes, sha256 {digest})")


if __name__ == "__main__":
    main()
