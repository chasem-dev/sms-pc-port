"""Replace files inside a RARC archive, keeping its node/entry/string tables; write Yaz0."""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rarc

def paths(d):
    """Map 'dir/sub/file' -> entry index for every file entry."""
    nn, no, ne, eo, _, so = struct.unpack_from('>IIIIII', d, 0x20)
    no += 0x20; eo += 0x20; so += 0x20
    s = lambda o: d[so+o:d.index(b'\0', so+o)].decode('shift_jis')
    out = {}
    def walk(ni, prefix):
        _, nmo, _, cnt, first = struct.unpack_from('>4sIHHI', d, no + ni*16)
        for e in range(first, first + cnt):
            idx, h, fn, off, size = struct.unpack_from('>HHIII', d, eo + e*20)
            nm = s(fn & 0xFFFFFF)
            if nm in ('.', '..'): continue
            if (fn >> 24) & 2: walk(off, prefix + nm + '/')
            else: out[prefix + nm] = e
    walk(0, '')
    return out

def repack(src, replacements):
    d = bytearray(rarc.yaz0(src))
    nn, no, ne, eo, _, so = struct.unpack_from('>IIIIII', d, 0x20)
    eo += 0x20
    data_off = struct.unpack_from('>I', d, 0xC)[0] + 0x20
    files = paths(d)
    unknown = set(replacements) - set(files)
    if unknown: raise KeyError(unknown)
    byidx = {e: p for p, e in files.items()}
    blob = bytearray()
    for e in sorted(byidx, key=lambda e: struct.unpack_from('>I', d, eo + e*20 + 8)[0]):
        off, size = struct.unpack_from('>II', d, eo + e*20 + 8)
        content = replacements.get(byidx[e], bytes(d[data_off+off:data_off+off+size]))
        struct.pack_into('>II', d, eo + e*20 + 8, len(blob), len(content))
        blob += content + b'\0' * ((-len(content)) % 32)
    out = bytearray(d[:data_off]) + blob
    # header: file size, data size; info block: total file data sizes (MRAM/ARAM split as in original)
    struct.pack_into('>I', out, 4, len(out))
    struct.pack_into('>III', out, 0x10, len(blob), len(blob), 0)
    return bytes(out)

def yaz0_store(d):
    """Valid Yaz0 stream made only of literal runs."""
    out = bytearray(b'Yaz0' + struct.pack('>I', len(d)) + bytes(8))
    for i in range(0, len(d), 8):
        chunk = d[i:i+8]
        out.append(0xFF); out += chunk
    return bytes(out)

if __name__ == '__main__':
    src, srcdir, out = sys.argv[1:4]
    reps = {}
    for root, _, fs in os.walk(srcdir):
        for f in fs:
            p = os.path.join(root, f); reps[os.path.relpath(p, srcdir)] = open(p, 'rb').read()
    packed = repack(open(src, 'rb').read(), reps)
    open(out, 'wb').write(yaz0_store(packed))
    print('replaced', len(reps), 'files ->', out, len(packed))
