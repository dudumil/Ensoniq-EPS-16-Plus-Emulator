#!/usr/bin/env python3
"""Add Ensoniq .EFE files to an EPS/EPS-16+/ASR disk image (800 KB or 1.6 MB).
   efe2img.py <in.img> <out.img> file.efe [file2.efe ...]"""
import sys, struct

def main():
    src, dst, files = sys.argv[1], sys.argv[2], sys.argv[3:]
    d = bytearray(open(src, 'rb').read())
    nblocks = len(d) // 512
    def fat_get(b):
        o = (5 + b // 170) * 512 + (b % 170) * 3
        return int.from_bytes(d[o:o + 3], 'big')
    def fat_set(b, v):
        o = (5 + b // 170) * 512 + (b % 170) * 3
        d[o:o + 3] = v.to_bytes(3, 'big')
    for f in files:
        e = open(f, 'rb').read()
        body = e[512:]
        n = (len(body) + 511) // 512
        ftype = e[0x32]
        name = e[0x12:0x1e]
        # find n contiguous free blocks
        start = None
        b = 15
        while b + n <= nblocks:
            if all(fat_get(x) == 0 for x in range(b, b + n)):
                start = b; break
            b += 1
        if start is None: sys.exit('no room for ' + f)
        for i in range(n):
            fat_set(start + i, start + i + 1 if i < n - 1 else 1)
            d[(start + i) * 512:(start + i + 1) * 512] = body[i * 512:(i + 1) * 512].ljust(512, b'\0')
        # directory entry
        for k in range(39):
            o = 3 * 512 + k * 26
            if d[o + 1] == 0:
                ent = bytes([0, ftype]) + name + struct.pack('>HHI', n, n, start) + bytes(4)
                d[o:o + 26] = ent
                break
        else: sys.exit('directory full')
        free = int.from_bytes(d[1024:1028], 'big') - n
        d[1024:1028] = free.to_bytes(4, 'big')
        print('added', name.decode(errors='replace'), 'type', ftype, n, 'blocks at', start)
    open(dst, 'wb').write(d)

main()
