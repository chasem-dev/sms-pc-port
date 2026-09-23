# Resolve dolphin-oracle symbolic trace ranges (ranges/*.txt) against the
# native port binary, with byte-order layouts taken from its DWARF types.
# Runs inside gdb (static, the program is not started):
#
#   TRACE_RANGES=/home/netflix/dolphin-oracle/ranges/play.txt TRACE_OUT=build/trace.ranges \
#       gdb -batch -x tools/trace_resolve.py build/sms
#
# Several range files: TRACE_RANGES=a.txt:b.txt. Writes TRACE_OUT (read by
# platform/trace, SMS_TRACE_RANGES) and TRACE_OUT.json (field names per label,
# used by tools/trace_compare.py).
#
# Range syntax (as dolphin-oracle): <start> <length|size> <label>
#   start: symbol[+off] | 0xADDR | *symbol[+off] | *(symbol+n)[+off]
# CodeWarrior-mangled static members (mPadStatus__10JUTGamePad) are looked up
# as JUTGamePad::mPadStatus. 0xADDR in MEM1 is used as is (the port maps MEM1
# at 0x80000000); its layout is unknown unless a "# type <label> <Type>" line
# gives one.
#
# Layout tokens, covering the range from offset 0: <width><kind>[x<count>]
#   kind: i integer, f float, p pointer, b raw bytes (no swap)
import json
import os
import re

import gdb

MEM1_LO, MEM1_HI = 0x80000000, 0x81800000


def cw_demangle(name):
    """mPadStatus__10JUTGamePad -> JUTGamePad::mPadStatus (static members only)."""
    m = re.match(r'^(\w+?)__(\d+)(\w+)$', name)
    if m and int(m.group(2)) == len(m.group(3)):
        return '%s::%s' % (m.group(3), m.group(1))
    return name


def lookup(sym):
    """(static address, gdb.Type) of a global, or None."""
    for cand in (sym, cw_demangle(sym)):
        try:
            v = gdb.parse_and_eval(cand)
            addr = int(gdb.parse_and_eval('&%s' % cand).cast(gdb.lookup_type('unsigned long')))
            return addr, v.type
        except gdb.error:
            continue
    return None


def layout_of(t, base=0, name='', out=None, limit=1 << 16):
    """Flatten a type into (offset, width, kind, name) scalars."""
    if out is None:
        out = []
    if base >= limit:
        return out
    t = t.strip_typedefs()
    code = t.code
    if code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
        for f in t.fields():
            if not hasattr(f, 'bitpos') or f.bitpos is None:
                continue  # static member
            if f.bitsize:
                continue  # bitfield: left as raw bytes
            fname = f.name or ('<base>' if f.is_base_class else '?')
            layout_of(f.type, base + f.bitpos // 8, (name + '.' if name else '') + fname, out, limit)
            if code == gdb.TYPE_CODE_UNION:
                break
    elif code == gdb.TYPE_CODE_ARRAY:
        et = t.target()
        n = t.sizeof // max(et.sizeof, 1)
        for i in range(n):
            if base + i * et.sizeof >= limit:
                break
            layout_of(et, base + i * et.sizeof, '%s[%d]' % (name, i), out, limit)
    elif code in (gdb.TYPE_CODE_PTR, gdb.TYPE_CODE_REF, gdb.TYPE_CODE_METHODPTR, gdb.TYPE_CODE_MEMBERPTR):
        out.append((base, t.sizeof, 'p', name))
    elif code == gdb.TYPE_CODE_FLT:
        out.append((base, t.sizeof, 'f', name))
    elif code in (gdb.TYPE_CODE_INT, gdb.TYPE_CODE_ENUM, gdb.TYPE_CODE_BOOL, gdb.TYPE_CODE_CHAR):
        out.append((base, t.sizeof, 'i' if t.sizeof > 1 else 'b', name))
    else:
        out.append((base, t.sizeof, 'b', name))
    return out


def tokens(scalars, length):
    """Compact layout string covering [0, length)."""
    runs = []
    pos = 0
    for off, w, k, _ in sorted(scalars):
        if off < pos or off + w > length:
            continue
        if off > pos:
            runs.append([1, 'b', off - pos])
        if w == 1 and k != 'b':
            k = 'b'
        if runs and runs[-1][0] == w and runs[-1][1] == k:
            runs[-1][2] += 1
        else:
            runs.append([w, k, 1])
        pos = off + w
    if pos < length:
        runs.append([1, 'b', length - pos])
    # merge adjacent byte runs
    merged = []
    for r in runs:
        if merged and r[1] == 'b' and merged[-1][1] == 'b':
            merged[-1][2] += r[0] * r[2]
            merged[-1][0] = 1
        else:
            merged.append(r if r[1] != 'b' else [1, 'b', r[0] * r[2]])
    return ','.join('%d%s%s' % (w, k, ('x%d' % c) if c > 1 else '') for w, k, c in merged)


def member_type_at(t, off):
    """gdb.Type of the scalar/pointer member at byte offset `off` of type t, or None."""
    t = t.strip_typedefs()
    if t.code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
        for f in t.fields():
            if getattr(f, 'bitpos', None) is None or f.bitsize:
                continue
            fo = f.bitpos // 8
            if fo <= off < fo + f.type.sizeof:
                return member_type_at(f.type, off - fo)
        return None
    if t.code == gdb.TYPE_CODE_ARRAY:
        et = t.target()
        return member_type_at(et, off % et.sizeof) if et.sizeof else None
    return t if off == 0 else None


def resolve(line, types):
    words = line.split('#', 1)[0].split()
    if len(words) < 3:
        return None
    start, length, label = words[:3]
    m = re.match(r'^(\*)?(?:\((\w+)\+(\w+)\)|(\w+))(?:\+(\w+))?$', start)
    if not m:
        return label, None, 'unsupported start %s' % start
    deref = bool(m.group(1))
    sym = m.group(2) or m.group(4)
    inner = int(m.group(3), 0) if m.group(3) else 0
    off = int(m.group(5), 0) if m.group(5) else 0
    typ = None
    if re.match(r'^0x[0-9a-fA-F]+$', sym):
        addr = int(sym, 16) + inner
        loc = '0x%08x' % addr if MEM1_LO <= addr < MEM1_HI else None
        if loc is None:
            return label, None, 'absolute address outside MEM1'
        where = 'mem'
    else:
        hit = lookup(sym)
        if not hit:
            return label, None, 'symbol %s not in the port binary' % sym
        addr, typ = hit
        addr += inner
        loc = '@0x%x' % addr
        where = 'host'
        if inner:
            typ = member_type_at(typ, inner)
    if deref:
        # the object is where the pointer points; its static type is the pointee
        if typ is not None:
            st = typ.strip_typedefs()
            typ = st.target() if st.code == gdb.TYPE_CODE_PTR else None
        loc = '*%s+0x%x' % (loc, off)
        base_off = off
    else:
        if off:
            loc = '%s+0x%x' % (loc, off) if where == 'host' else '0x%08x' % (int(loc, 16) + off)
        base_off = off
    if label in types:
        try:
            typ = gdb.lookup_type(types[label])
        except gdb.error:
            pass
    if length == 'size':
        if typ is None:
            return label, None, 'size of an untyped range'
        n = typ.sizeof - base_off
    else:
        n = int(length, 0)
    scalars = []
    if typ is not None:
        full = layout_of(typ, 0, '', None, base_off + n)
        scalars = [(o - base_off, w, k, nm) for o, w, k, nm in full if base_off <= o and o + w <= base_off + n]
    else:
        # unknown layout: 4-byte words
        scalars = [(o, 4, 'i', '+0x%x' % o) for o in range(0, n - n % 4, 4)]
    return label, (loc, n, tokens(scalars, n), scalars, str(typ) if typ is not None else '?'), None


def main():
    paths = os.environ.get('TRACE_RANGES', '/home/netflix/dolphin-oracle/ranges/play.txt').split(':')
    out = os.environ.get('TRACE_OUT', 'trace.ranges')
    anchor = lookup('port_trace_anchor')
    lines, types = [], {}
    for p in paths:
        for raw in open(p):
            m = re.match(r'^#\s*type\s+(\S+)\s+(.+?)\s*$', raw)
            if m:
                types[m.group(1)] = m.group(2)
            elif raw.split('#', 1)[0].strip():
                lines.append(raw)
    names = {}
    with open(out, 'w') as f:
        f.write('# sms-port native trace ranges v1 (tools/trace_resolve.py from %s)\n' % ' '.join(paths))
        f.write('# anchor 0x%x\n' % (anchor[0] if anchor else 0))
        for raw in lines:
            r = resolve(raw, types)
            if r is None:
                continue
            label, info, err = r
            if err:
                f.write('# skip %s: %s\n' % (label, err))
                print('trace_resolve: skip %s: %s' % (label, err))
                continue
            loc, n, lay, scalars, tname = info
            f.write('%s 0x%x %s layout=%s\n' % (loc, n, label, lay))
            names[label] = {'type': tname, 'fields': [[o, w, k, nm] for o, w, k, nm in sorted(scalars)]}
            print('trace_resolve: %-16s %-24s 0x%-5x %s' % (label, loc, n, tname))
    json.dump(names, open(out + '.json', 'w'), indent=0)
    if not anchor:
        print('trace_resolve: WARNING: port_trace_anchor not found (platform/trace not linked?)')
    print('trace_resolve: wrote %s' % out)


main()
