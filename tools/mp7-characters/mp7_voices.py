"""Mario Party 7's character voices as Mario's voice cues, for the port's character voices.

mp7_voices.py <MP7 iso> <mods/characters>
Writes <id>.voice/<cue>.wav for each character pack_characters.py makes (mario-mp7, luigi, peach, ...):
one WAV per Mario voice cue (MV21_JUMP_SMALL_01.wav, ...; the names of MSound/SoundEffects.hpp),
chosen from the character's Mario Party 7 voice lines by what each is said for. The port plays them
in place of Mario's voice as the character (platform/misc/port_character_voice.cpp); a cue left
without a file keeps Mario's voice, and any WAV can be swapped for another.

Mario Party 7's 24 voice lines per character (sound/MP7_SND.msm group 4, at CharVoiceSeTable), by
where its code plays them (the Mario Party 5 and 6 decompilations, Mario Party 7's symbols):
  0 star get (biggest cheer)    2 jump for joy ("Yahoo!")       3 hurt, crying
  4 minigame win line           6 short happy cheer (coins)     8 shocked ("Whoa!", the "!")
  9 knocked back, yelp          11 falling scream               12 lose, disappointed
  13 dismay (Bowser, Boo)       15 squashed
The rest (1, 5, 7, 10, 14, 16-23) are not placed with confidence and are not used.
"""
import os, sys, struct, wave
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_voices as ev

# Mario's cue (without its _NN variant number) -> Mario Party 7 lines in order of preference;
# variants _01, _02, ... take the list in turn, so a cue with several variants gets several lines.
CUES = {
    'MV01_DAMAGE_BIG': [3, 9, 15],          'MV02_DAMAGE_MID': [9, 3, 15],
    'MV03_DAMAGE_LITLE': [9, 15],           'MV04_DAMAGE_ELEC': [11, 3],
    'MV05_DAMAGE_FIRE': [11, 3],            'MV06_DAMAGE_WATER': [9, 15, 8],
    'MV07_DAMAGE_REACT': [12, 3, 13],       'MV08A_DOWN': [12, 3, 13],
    'MV08B_DOWN_REACT': [13, 12, 3],        'MV10A_CRY_SHORT': [3, 12],
    'MV10B_CRY_JUMP': [11],                 'MV12_REACT': [8, 13],
    'MV13_ACTION_SMALL': [6, 9],            'MV15_EXERT_INST': [9, 6],
    'MV15_REACT_SMALL': [8, 9, 13],         'MV16_EXERT_CONT': [15, 3],
    'MV17_EXERT_REACT': [8, 13],            'MV18_FALL_AFTER': [9, 3],
    'MV19_JMP_PT': [6, 2],                  'MV20_JMP_PT_REACT': [2, 6],
    'MV21_JUMP_SMALL': [6, 9],              'MV22_JUMP_MID': [6, 2],
    'MV23_JUMP_LARGE': [2, 6],              'MV24_JUMP_SPECIAL': [2, 0, 4],
    'MV25A_JUMP_HUGE': [0, 2],              'MV25A_WIRE_ROLL_01': [6, 9],
    'MV25A_WIRE_ROLL_02': [2, 6],           'MV26_JUMP_REACT': [4, 0, 2, 6],
    'MV27_SPRISE': [8],                     'MV28_SPRISE_SMALL': [8, 9],
    'MV29_SPRISE_REACT': [13, 8, 12],       'MV30_FRIGHT': [11, 8],
    'MV31_OPEN_DOOR': [6, 2],               'MV33_T': [4, 0, 2, 6],
    'MV34_ACTION_T': [6, 9],                'MV38_EXERT_INST_T': [15, 3],
    'MV41_JUMP_T': [6, 2],                  'MV42_JUMP_HUGE_T': [0, 2],
    'MV46A_OPEN_DOOR_T': [6, 2],            'MV46B_NEXT_STG_T': [4, 0, 2],
    'MV_DOOR': [6],                         'MV_BYEBYE': [4, 0],
    # MV_IBIKI, MV_IBIKI2 (snoring) keep Mario's.
}
VARIANTS = {   # how many of each (the port's table); the cues that share a wave with another are left out
    'MV01_DAMAGE_BIG': 3, 'MV02_DAMAGE_MID': 4, 'MV03_DAMAGE_LITLE': 4, 'MV04_DAMAGE_ELEC': 2,
    'MV05_DAMAGE_FIRE': 2, 'MV06_DAMAGE_WATER': 3, 'MV07_DAMAGE_REACT': 4, 'MV08A_DOWN': 3,
    'MV08B_DOWN_REACT': 3, 'MV10A_CRY_SHORT': 2, 'MV10B_CRY_JUMP': 1, 'MV12_REACT': 3,
    'MV13_ACTION_SMALL': 3, 'MV15_EXERT_INST': 2, 'MV15_REACT_SMALL': 3, 'MV16_EXERT_CONT': 1,
    'MV17_EXERT_REACT': 2, 'MV18_FALL_AFTER': 1, 'MV19_JMP_PT': 2, 'MV20_JMP_PT_REACT': 1,
    'MV21_JUMP_SMALL': 4, 'MV22_JUMP_MID': 1, 'MV23_JUMP_LARGE': 3, 'MV24_JUMP_SPECIAL': 6,
    'MV25A_JUMP_HUGE': 2, 'MV25A_WIRE_ROLL_01': 3, 'MV25A_WIRE_ROLL_02': 3, 'MV26_JUMP_REACT': 8,
    'MV27_SPRISE': 2, 'MV28_SPRISE_SMALL': 2, 'MV29_SPRISE_REACT': 3, 'MV30_FRIGHT': 2,
    'MV31_OPEN_DOOR': 4, 'MV33_T': 6, 'MV34_ACTION_T': 3, 'MV38_EXERT_INST_T': 1, 'MV41_JUMP_T': 3,
    'MV42_JUMP_HUGE_T': 2, 'MV46A_OPEN_DOOR_T': 2, 'MV46B_NEXT_STG_T': 3, 'MV_DOOR': 0, 'MV_BYEBYE': 0,
}
FALLBACK = [6, 2, 0, 4, 3, 9, 8, 12]   # when a character lacks a line its list names
IDS = {'mario': 'mario-mp7'}           # pack_characters.py's archive ids where they differ


def names(cue):
    n = VARIANTS[cue]
    if n == 0: return [cue]
    if cue.startswith('MV25A_WIRE_ROLL_'): return [cue + c for c in 'ABC']
    return ['%s_%02d' % (cue, i + 1) for i in range(n)]


def write_wav(path, rate, pcm):
    w = wave.open(path, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(struct.pack('<%dh' % len(pcm), *pcm)); w.close()


if __name__ == '__main__':
    iso, out = sys.argv[1:3]
    d = ev.read_iso_file(iso, '/sound/MP7_SND.msm')
    groups, _ = ev.parse(d)
    g = groups[4]; ids = sorted(g['sfx'])
    for ci, char in enumerate(ev.CHARS):
        lines = {}
        for cue in range(24):
            sids = [s for s in g['sfx'][ids[ci * 24 + cue]] if s != ev.SILENT]
            if sids: lines[cue] = g['samples'][sids[0]]
        folder = os.path.join(out, IDS.get(char, char) + '.voice')
        os.makedirs(folder, exist_ok=True)
        pcm = {}
        n = 0
        for cue, prefs in CUES.items():
            have = [c for c in prefs if c in lines] or [c for c in FALLBACK if c in lines]
            for v, name in enumerate(names(cue)):
                line = have[v % len(have)]
                if line not in pcm: pcm[line] = ev.decode(d, lines[line])
                write_wav(os.path.join(folder, name + '.wav'), lines[line]['rate'], pcm[line])
                n += 1
        print('%-10s %d cues from %d lines -> %s' % (char, n, len(pcm), folder))
