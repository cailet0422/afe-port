#!/usr/bin/env python3
"""Keeps the Download section of README.md and README.ko.md on the version in
pc/VERSION: the "latest release" link, the four files and the checksums.

    python3 pc/tools/readme_version.py           # rewrite them
    python3 pc/tools/readme_version.py --check   # fail if they name another version (CI)

Run it in the commit that bumps pc/VERSION (the release follows the merge)."""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
READMES = ["README.md", "README.ko.md"]
# a version in the release links: tag/vX.Y.Z, download/vX.Y.Z/, DobutsuNoMoriEPlus-vX.Y.Z-
LINK = re.compile(r"(releases/tag/|releases/download/|DobutsuNoMoriEPlus-|\*\*\[)v(\d+\.\d+\.\d+)")


def download_section(text):
    """The text from the first heading of the download section to the next ## heading"""
    m = re.search(r"^## (Download|다운로드)\s*$", text, re.M)
    if m is None:
        raise SystemExit("no Download section")
    end = re.search(r"^## ", text[m.end():], re.M)
    return m.start(), m.end() + (end.start() if end else len(text) - m.end())


def main():
    check = "--check" in sys.argv[1:]
    with open(os.path.join(ROOT, "pc", "VERSION"), encoding="utf-8") as f:
        version = f.read().strip()
    bad = []
    for name in READMES:
        path = os.path.join(ROOT, name)
        with open(path, encoding="utf-8") as f:
            text = f.read()
        start, end = download_section(text)
        section = text[start:end]
        found = {m.group(2) for m in LINK.finditer(section)}
        if not found:
            raise SystemExit(f"{name}: no release links in the Download section")
        if found == {version}:
            continue
        if check:
            bad.append(f"{name}: the Download section names {', '.join(sorted(found))}, pc/VERSION is {version}")
            continue
        section = LINK.sub(lambda m: f"{m.group(1)}v{version}", section)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text[:start] + section + text[end:])
        print(f"{name}: now v{version}")
    if bad:
        print("\n".join(bad))
        print("run: python3 pc/tools/readme_version.py")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
