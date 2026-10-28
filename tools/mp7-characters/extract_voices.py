"""Extract every per-character sound cue from Mario Party 7's sound bank (sound/MP7_SND.msm) to WAV.

extract_voices.py <MP7 iso> <output folder>
Writes <output>/<character>/voice/*.wav and <character>/sfx/*.wav.

The bank keeps the playable characters' cues in groups laid out character-major, in the order of
the game's per-character SE tables (main.dol CharSeTable 0x80256210, CharVoiceSeTable 0x80256240,
CharVoice2SeTable 0x80256270): daisy kinopio kinopiko luigi mario catherine karon peach teresa
wario waluigi yoshi, not the game's character numbering.
  group 3: 30 movement sounds each      group 4: 24 voice lines each
  group 5: 2 voice lines each           group 6: 6 voice lines each
  group 7: extra lines for some characters, assigned by the sample-ID range the character's
           group 4 lines occupy.
Files are named <group>-<cue>_se<game SE number>.wav, so the same cue number is the same action
for every character. Empty and silent placeholder slots are skipped.
"""
import os, sys, struct, wave

CHARS = ['daisy', 'toad', 'toadette', 'luigi', 'mario', 'birdo', 'dry-bones',
         'peach', 'boo', 'wario', 'waluigi', 'yoshi']
PER_CHAR = {3: 30, 4: 24, 5: 2, 6: 6}   # group -> cues per character
SFX_GROUPS = {3}
SILENT = 0x94                           # placeholder sample used for unused slots
# What group 4's lines are said for, where the game's code shows it (see mp7_voices.py)
MEANING = {0: 'star-get', 2: 'yahoo', 3: 'hurt', 4: 'win', 6: 'cheer', 8: 'shocked', 9: 'knocked-back',
           11: 'falling', 12: 'lose', 13: 'dismay', 15: 'squashed'}


def u16(d, o): return struct.unpack_from('>H', d, o)[0]
def u32(d, o): return struct.unpack_from('>I', d, o)[0]


def read_iso_file(iso, want):
    f = open(iso, 'rb')
    def rd(o, n): f.seek(o); return f.read(n)
    fst_off, fst_size = struct.unpack('>II', rd(0x424, 8))
    fst = rd(fst_off, fst_size)
    n = u32(fst, 8); strtab = fst[n * 12:]
    stack = [(n, '')]
    for i in range(1, n):
        while i >= stack[-1][0]: stack.pop()
        a, b, c = struct.unpack_from('>III', fst, i * 12)
        nm = strtab[a & 0xFFFFFF:strtab.index(b'\0', a & 0xFFFFFF)].decode('shift_jis', 'replace')
        path = stack[-1][1] + '/' + nm
        if a >> 24: stack.append((c, path))
        elif path.lower() == want.lower(): return rd(b, c)
    sys.exit('%s not found in %s' % (want, iso))


def parse(d):
    hdr = struct.unpack_from('>14I', d, 0x10)
    gi_o, gi_s, se_o, se_s, gd_o, sp_o = hdr[4], hdr[5], hdr[8], hdr[9], hdr[10], hdr[12]
    groups = {}
    for o in range(gi_o, gi_o + gi_s, 0x20):
        gid, _, go, gs, so, _ = struct.unpack_from('>HHIIII', d, o)
        if gs == 0: continue
        base = gd_o + go
        pool, proj, sdir, _ = struct.unpack_from('>4I', d, base)
        samples = {}
        p = base + sdir
        while u16(d, p) != 0xFFFF:
            sid, _, off, _, _, rate, ns, _, _, info = struct.unpack_from('>HHIIBxHIIII', d, p)
            ip = base + sdir + info
            samples[sid] = dict(off=sp_o + so + off, rate=rate, n=ns & 0xFFFFFF,
                                coefs=struct.unpack_from('>16h', d, ip + 8))
            p += 0x20
        macros = {}
        p = base + pool
        while u32(d, p) != 0xFFFFFFFF:
            size = u32(d, p)
            macros[u16(d, p + 4)] = [(u32(d, c) >> 8) & 0xFFFF for c in range(p + 8, p + size, 8)
                                     if u32(d, c) & 0x7F == 0x10]   # SAMPLE command
            p += size
        pj = base + proj; so_ = u32(d, pj + 0x1C)
        sfx = {}
        for k in range(u16(d, pj + so_)):
            e = pj + so_ + 4 + k * 10
            sfx[u16(d, e)] = macros.get(u16(d, e + 2), [])
        groups[gid] = dict(samples=samples, sfx=sfx)
    se = {}   # (group, sfx id) -> game SE number
    for i, o in enumerate(range(se_o, se_o + se_s, 16)):
        g, fx = struct.unpack_from('>hH', d, o)
        se.setdefault((g, fx), i)
    return groups, se


def decode(d, s):
    out = []; h1 = h2 = 0; o = s['off']; c = s['coefs']
    while len(out) < s['n']:
        ps = d[o]; sc = 1 << (ps & 15); c1, c2 = c[(ps >> 4) * 2], c[(ps >> 4) * 2 + 1]
        for k in range(14):
            if len(out) >= s['n']: break
            b = d[o + 1 + k // 2]; nib = b >> 4 if k % 2 == 0 else b & 15
            if nib >= 8: nib -= 16
            v = max(-32768, min(32767, (((nib * sc) << 11) + 1024 + c1 * h1 + c2 * h2) >> 11))
            out.append(v); h2, h1 = h1, v
        o += 8
    return out


def write_wav(path, s, pcm):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    w = wave.open(path, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(s['rate'])
    w.writeframes(struct.pack('<%dh' % len(pcm), *pcm)); w.close()


if __name__ == '__main__':
    iso, out = sys.argv[1:3]
    d = read_iso_file(iso, '/sound/MP7_SND.msm')
    groups, se = parse(d)
    count = {c: 0 for c in CHARS}

    def emit(char, kind, name, gid, fx):
        sids = [s for s in groups[gid]['sfx'][fx] if s != SILENT]
        for v, sid in enumerate(sids):
            s = groups[gid]['samples'][sid]
            suffix = chr(ord('a') + v) if len(sids) > 1 else ''
            write_wav(os.path.join(out, char, kind, '%s%s_se%03x.wav' % (name, suffix, se[(gid, fx)])),
                      s, decode(d, s))
            count[char] += 1

    for gid, per in PER_CHAR.items():
        ids = sorted(groups[gid]['sfx'])
        assert len(ids) == per * len(CHARS), (gid, len(ids))
        kind = 'sfx' if gid in SFX_GROUPS else 'voice'
        for ci, char in enumerate(CHARS):
            for cue in range(per):
                label = '-' + MEANING.get(cue, 'unknown') if gid == 4 else ''
                emit(char, kind, 'g%d-%02d%s' % (gid, cue, label), gid, ids[ci * per + cue])

    # group 7: assign by which character's group 4 sample-ID range the sample falls in
    g4 = sorted(groups[4]['sfx'])
    starts = sorted((min(s for fx in g4[ci * 24:ci * 24 + 24] for s in groups[4]['sfx'][fx] if s != SILENT), char)
                    for ci, char in enumerate(CHARS))
    end = max(s for fx in g4 for s in groups[4]['sfx'][fx]) + 1
    def owner(sid):
        for (lo, char), nxt in zip(starts, [s for s, _ in starts[1:]] + [end]):
            if lo <= sid < nxt: return char
    for fx in sorted(groups[7]['sfx']):
        sids = [s for s in groups[7]['sfx'][fx] if s != SILENT]
        chars = {owner(s) for s in sids}
        if len(chars) == 1 and None not in chars:
            emit(chars.pop(), 'voice', 'g7-%03x' % fx, 7, fx)

    for c in CHARS: print('%-10s %d files' % (c, count[c]))
