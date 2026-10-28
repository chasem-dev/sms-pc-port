"""Yaz0 + RARC extractor (Nintendo GameCube archives)."""
import struct, sys, os

def yaz0(d):
    if d[:4] != b'Yaz0': return d
    size = struct.unpack_from('>I', d, 4)[0]
    out = bytearray(); p = 16
    while len(out) < size:
        code = d[p]; p += 1
        for b in range(8):
            if len(out) >= size: break
            if code & (0x80 >> b):
                out.append(d[p]); p += 1
            else:
                b1, b2 = d[p], d[p+1]; p += 2
                dist = ((b1 & 0xF) << 8 | b2) + 1
                n = b1 >> 4
                if n == 0: n = d[p] + 0x12; p += 1
                else: n += 2
                for _ in range(n): out.append(out[-dist])
    return bytes(out)

def extract(d, out):
    d = yaz0(d)
    assert d[:4] == b'RARC', d[:4]
    data_off = struct.unpack_from('>I', d, 0xC)[0] + 0x20
    info = 0x20
    nnodes, node_off, nentries, entry_off, _, str_off = struct.unpack_from('>IIIIII', d, info)
    node_off += 0x20; entry_off += 0x20; str_off += 0x20
    def s(o): return d[str_off+o:d.index(b'\0', str_off+o)].decode('shift_jis')
    nodes = [struct.unpack_from('>4sIHHI', d, node_off + i*16) for i in range(nnodes)]
    def walk(ni, path):
        _, nmo, _, cnt, first = nodes[ni]
        os.makedirs(path, exist_ok=True)
        for e in range(first, first + cnt):
            idx, h, flags_name, off, size = struct.unpack_from('>HHIII', d, entry_off + e*20)
            flags = flags_name >> 24; nm = s(flags_name & 0xFFFFFF)
            if nm in ('.', '..'): continue
            if flags & 2: walk(off, os.path.join(path, nm))
            else: open(os.path.join(path, nm), 'wb').write(yaz0(d[data_off+off:data_off+off+size]))
    walk(0, os.path.join(out, s(nodes[0][1])))

if __name__ == '__main__':
    extract(open(sys.argv[1], 'rb').read(), sys.argv[2])
