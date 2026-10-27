#!/usr/bin/env python3
"""Read GameCube disc images (ISO/GCM or CISO) and extract files.

    gcdisc.py list IMAGE
    gcdisc.py extract-orig IMAGE ORIG_DIR     # sys/main.dol + files/*.rel for the matching build
    gcdisc.py extract IMAGE PATH OUT          # one file, e.g. files/forest_msg.arc

Yaz0-compressed files (*.szs) are decompressed on extraction.
"""
import struct
import sys
from pathlib import Path


def yaz0_decompress(src):
    if src[:4] != b"Yaz0":
        return src
    size = struct.unpack(">I", src[4:8])[0]
    dst = bytearray(size)
    s, d = 16, 0
    while d < size:
        code = src[s]
        s += 1
        for bit in range(8):
            if d >= size:
                break
            if code & (0x80 >> bit):
                dst[d] = src[s]
                d += 1
                s += 1
            else:
                b1, b2 = src[s], src[s + 1]
                s += 2
                dist = ((b1 & 0xF) << 8 | b2) + 1
                n = b1 >> 4
                if n == 0:
                    n = src[s] + 0x12
                    s += 1
                else:
                    n += 2
                for _ in range(n):
                    dst[d] = dst[d - dist]
                    d += 1
    return bytes(dst)


class Disc:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.ciso = self.f.read(4) == b"CISO"
        if self.ciso:
            self.f.seek(0)
            hdr = self.f.read(0x8000)
            self.block = struct.unpack("<I", hdr[4:8])[0]
            self.index, n = [], 0
            for present in hdr[8:]:
                self.index.append(n if present else None)
                n += 1 if present else 0

    def read(self, off, size):
        if not self.ciso:
            self.f.seek(off)
            return self.f.read(size)
        out = bytearray()
        while size:
            blk, o = divmod(off, self.block)
            take = min(size, self.block - o)
            if blk >= len(self.index) or self.index[blk] is None:
                out += bytes(take)
            else:
                self.f.seek(0x8000 + self.index[blk] * self.block + o)
                out += self.f.read(take)
            off += take
            size -= take
        return bytes(out)

    def files(self):
        """{path: (offset, size)} including sys/main.dol."""
        out = {}
        dol_off = struct.unpack(">I", self.read(0x420, 4))[0]
        hdr = self.read(dol_off, 0x100)
        offs = struct.unpack(">18I", hdr[0:72])
        sizes = struct.unpack(">18I", hdr[0x90:0x90 + 72])
        out["sys/main.dol"] = (dol_off, max(o + s for o, s in zip(offs, sizes)))
        fst_off, fst_size = struct.unpack(">II", self.read(0x424, 8))
        fst = self.read(fst_off, fst_size)
        count = struct.unpack(">I", fst[8:12])[0]
        strtab = fst[count * 12:]

        def name(i):
            o = struct.unpack(">I", fst[i * 12:i * 12 + 4])[0] & 0xFFFFFF
            return strtab[o:].split(b"\0")[0].decode("shift_jis", "replace")

        path, ends = ["files"], [count]
        for i in range(1, count):
            while i >= ends[-1]:
                ends.pop()
                path.pop()
            word, a, b = struct.unpack(">III", fst[i * 12:i * 12 + 12])
            if word >> 24:
                path.append(name(i))
                ends.append(b)
            else:
                out["/".join(path + [name(i)])] = (a, b)
        return out

    def extract(self, path):
        off, size = self.files()[path]
        data = self.read(off, size)
        return yaz0_decompress(data) if path.endswith(".szs") else data


def main():
    cmd, image = sys.argv[1], sys.argv[2]
    disc = Disc(image)
    if cmd == "list":
        for path, (off, size) in sorted(disc.files().items()):
            print(f"{size:10d} {path}")
    elif cmd == "extract-orig":
        out = Path(sys.argv[3])
        (out / "sys").mkdir(parents=True, exist_ok=True)
        (out / "files").mkdir(parents=True, exist_ok=True)
        (out / "sys" / "main.dol").write_bytes(disc.extract("sys/main.dol"))
        for path in disc.files():
            if path.endswith(".rel.szs"):
                (out / "files" / Path(path).name[:-4]).write_bytes(disc.extract(path))
        print(f"extracted main.dol and RELs to {out}")
    elif cmd == "extract":
        Path(sys.argv[4]).write_bytes(disc.extract(sys.argv[3]))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
