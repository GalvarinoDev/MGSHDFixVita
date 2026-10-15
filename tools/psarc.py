#!/usr/bin/env python3
"""Read PSARC archives (the .psp2arc files of the Vita MGS HD Collection).
   psarc.py list <archive>                     print size and path of each file
   psarc.py check <archive>                    decompress each file in memory, report errors
   psarc.py extract <archive> <out-dir> [path ...]   write files (all, or only the given paths)"""
import hashlib, os, struct, sys, zlib

class Psarc:
    def __init__(self, path):
        self.f = open(path, "rb")
        magic, self.vmaj, self.vmin, comp, toc_len, ent_size, n, self.bsize, self.flags = struct.unpack(">4sHH4sIIIII", self.f.read(32))
        if magic != b"PSAR":
            sys.exit(f"psarc: {path} is not a PSARC archive")
        self.comp = comp.decode()
        toc = self.f.read(toc_len - 32)
        self.ents = []
        for i in range(n):
            e = toc[i * ent_size:(i + 1) * ent_size]
            self.ents.append((e[:16], struct.unpack(">I", e[16:20])[0], int.from_bytes(e[20:25], "big"), int.from_bytes(e[25:30], "big")))
        w = 1
        while 256 ** w < self.bsize:
            w += 1
        tbl = toc[n * ent_size:]
        self.zs = [int.from_bytes(tbl[i:i + w], "big") for i in range(0, len(tbl) - w + 1, w)]
        # entry 0 is the manifest: one path for each following entry
        self.names = [""] + self.read(0).decode().split("\n")

    def read(self, i):
        _, blk, size, off = self.ents[i]
        out = bytearray()
        self.f.seek(off)
        while len(out) < size:
            z = self.zs[blk] or self.bsize
            data = self.f.read(z)
            # a block smaller than its full size is compressed, except some stored last blocks
            if z < min(self.bsize, size - len(out)) or (z < self.bsize and data[:1] == b"\x78"):
                try:
                    data = zlib.decompress(data)
                except zlib.error:
                    pass
            out += data
            blk += 1
        return bytes(out[:size])

    def name_ok(self, i):
        md5 = self.ents[i][0]
        return any(hashlib.md5(v.encode()).digest() == md5 for v in (self.names[i], self.names[i].upper()))

def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("list", "check", "extract"):
        sys.exit(__doc__)
    a = Psarc(sys.argv[2])
    if sys.argv[1] == "list":
        for i in range(1, len(a.ents)):
            print(a.ents[i][2], a.names[i])
    elif sys.argv[1] == "check":
        bad = 0
        for i in range(len(a.ents)):
            try:
                ok = len(a.read(i)) == a.ents[i][2] and (i == 0 or a.name_ok(i))
            except Exception:
                ok = False
            if not ok:
                bad += 1
                print("BAD", i, a.names[i] if i < len(a.names) else "?")
        print(f"{sys.argv[2]}: v{a.vmaj}.{a.vmin} {a.comp}, {len(a.ents) - 1} files, block {a.bsize}, {bad} bad")
        sys.exit(1 if bad else 0)
    else:
        if len(sys.argv) < 4:
            sys.exit(__doc__)
        out, want = sys.argv[3], set(sys.argv[4:])
        for i in range(1, len(a.ents)):
            if want and a.names[i] not in want:
                continue
            p = os.path.join(out, a.names[i].lstrip("/"))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "wb") as o:
                o.write(a.read(i))
            print(p)

if __name__ == "__main__":
    main()
