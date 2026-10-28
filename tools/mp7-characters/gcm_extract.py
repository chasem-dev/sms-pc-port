import struct, sys, os
iso, out = sys.argv[1], sys.argv[2]
f = open(iso, 'rb')
def rd(o, n): f.seek(o); return f.read(n)
fst_off, fst_size = struct.unpack('>II', rd(0x424, 8))
fst = rd(fst_off, fst_size)
n = struct.unpack('>I', fst[8:12])[0]
strtab = fst[n*12:]
def name(o): return strtab[o:strtab.index(b'\0', o)].decode('shift_jis', 'replace')
os.makedirs(out, exist_ok=True)
# sys files
open(os.path.join(out, 'boot.bin'), 'wb').write(rd(0, 0x440))
dol_off = struct.unpack('>I', rd(0x420, 4))[0]
open(os.path.join(out, 'main.dol'), 'wb').write(rd(dol_off, min(fst_off - dol_off, 0x800000)) if fst_off > dol_off else b'')
stack = [(n, os.path.join(out, 'files'))]
os.makedirs(stack[0][1], exist_ok=True)
i = 1
while i < n:
    while stack and i >= stack[-1][0]: stack.pop()
    flags_name, a, b = struct.unpack('>III', fst[i*12:i*12+12])
    isdir = flags_name >> 24; nm = name(flags_name & 0xFFFFFF)
    path = os.path.join(stack[-1][1], nm)
    if isdir:
        os.makedirs(path, exist_ok=True); stack.append((b, path))
    else:
        open(path, 'wb').write(rd(a, b))
    i += 1
print('entries', n)
