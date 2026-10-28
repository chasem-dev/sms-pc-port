"""Hudson .bin archive reader (Mario Party 4-8)."""
import struct, zlib

def lzss(src, size):
    out = bytearray(); text = bytearray(1024); w = 958; p = 0; flag = 0
    while len(out) < size:
        flag >>= 1
        if not flag & 0x100:
            flag = 0xFF00 | src[p]; p += 1
        if flag & 1:
            c = src[p]; p += 1
            out.append(c); text[w] = c; w = (w + 1) & 1023
        else:
            i = src[p]; j = src[p+1]; p += 2
            i |= (j & 0xC0) << 2; j = (j & 0x3F) + 3
            for k in range(j):
                c = text[(i + k) & 1023]; out.append(c); text[w] = c; w = (w + 1) & 1023
    return bytes(out[:size])

def slide(src, size, fslide=False):
    # Yay0-like sliding window used by Hudson (types 2/3/4)
    out = bytearray(); p = 4; bits = 0; flag = 0
    while len(out) < size:
        if bits == 0:
            flag = struct.unpack_from('>I', src, p)[0]; p += 4; bits = 32
        if flag & 0x80000000:
            out.append(src[p]); p += 1
        else:
            v = (src[p] << 8) | src[p+1]; p += 2
            dist = (v & 0xFFF) + 1; n = v >> 12
            if n == 0: n = src[p] + 18; p += 1
            else: n += 2
            for _ in range(n): out.append(out[-dist])
        flag = (flag << 1) & 0xFFFFFFFF; bits -= 1
    return bytes(out[:size])

def rle(src, size):
    out = bytearray(); p = 0
    while len(out) < size:
        c = src[p]; p += 1
        if c < 128:
            out += bytes([src[p]]) * c; p += 1
        else:
            c -= 128; out += src[p:p+c]; p += c
    return bytes(out[:size])

def read(path):
    d = open(path, 'rb').read()
    n = struct.unpack_from('>I', d, 0)[0]
    offs = struct.unpack_from('>%dI' % n, d, 4)
    files = []
    for o in offs:
        size, typ = struct.unpack_from('>II', d, o)
        src = d[o+8:]
        if typ == 0: data = src[:size]
        elif typ == 1: data = lzss(src, size)
        elif typ in (2, 3, 4): data = slide(src, size)
        elif typ == 5: data = rle(src, size)
        elif typ == 7: data = zlib.decompress(src[8:])[:size]
        else: raise ValueError('compression %d' % typ)
        files.append(data)
    return files

if __name__ == '__main__':
    import sys
    for path in sys.argv[1:]:
        for i, f in enumerate(read(path)):
            print(path.split('/')[-1], i, len(f), f[:8])
