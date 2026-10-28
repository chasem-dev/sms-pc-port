"""Pack converted characters into sms-pc-port character archives.

pack_characters.py <Sunshine data/mario.szs> <converted chars folder> <sms-pc-port mods/characters>
Writes a character pack <id>/ for each: model.szs (Sunshine's mario archive with the character's files
in place) and name.txt (its name). mp7_voices.py adds its voice/.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rarcpack

# MP7 file name -> (archive id, name shown in game)
CHARACTERS = {
    'mario': ('mario-mp7', 'Mario (Mario Party 7)'), 'luigi': ('luigi', 'Luigi'), 'peach': ('peach', 'Peach'),
    'daisy': ('daisy', 'Daisy'), 'wario': ('wario', 'Wario'), 'waluigi': ('waluigi', 'Waluigi'),
    'yoshi': ('yoshi', 'Yoshi'), 'kinopio': ('toad', 'Toad'), 'kinopiko': ('toadette', 'Toadette'),
    'teresa': ('boo', 'Boo'), 'minikoopa': ('koopa-kid', 'Koopa Kid'), 'catherine': ('birdo', 'Birdo'),
    'karon': ('dry-bones', 'Dry Bones'), 'donkey': ('donkey-kong', 'Donkey Kong'),
}

if __name__ == '__main__':
    src_path, chars, out = sys.argv[1:4]
    src = open(src_path, 'rb').read()
    os.makedirs(out, exist_ok=True)
    for c in sorted(os.listdir(chars)):
        cid, name = CHARACTERS.get(c, (c, c.capitalize()))
        folder = os.path.join(chars, c)
        reps = {}
        for root, _, fs in os.walk(folder):
            for f in fs:
                p = os.path.join(root, f); reps[os.path.relpath(p, folder)] = open(p, 'rb').read()
        pack = os.path.join(out, cid)
        os.makedirs(pack, exist_ok=True)
        open(os.path.join(pack, 'model.szs'), 'wb').write(rarcpack.repack(src, reps))
        open(os.path.join(pack, 'name.txt'), 'w').write(name + '\n')
        print(cid, name)
