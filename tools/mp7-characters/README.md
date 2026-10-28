# tools/mp7-characters

Turns Mario Party 7's characters into character packs for [`mods/characters/`](../../mods/README.md#characters-other-characters): each a folder with the character's model (a whole `/data/mario.szs` with the character in Mario's place) and voice, played with F4 (`CHARACTER_NEXT`) in the game.
Nothing of either game is in this folder; it reads both from your own discs.

Each character's `ma_mdl1.bmd` keeps Sunshine's 29 joints, 11 materials and shapes and its hierarchy, so the game's code and all of Mario's 199 animations work: the Mario Party 7 mesh is bound to Sunshine's joints by role (hip, thigh, shin, foot, shoulder, upper arm, forearm, hand, head), whatever its rig calls them, and the body animations (`.bck`) take the character's joint offsets, so he keeps his proportions.
The arms and legs keep Sunshine's bone directions (the animations twist them about those axes), the default fist is the character's fingers curled and turned to sit in the hand joint as Mario's does (fitted to his), and the hand models the game swaps in are the character's open hands.
What the character's motions start hidden (blink lids, spare eye frames) is left out; the textures go in one atlas (texture 0, which the hand and cap models share); characters with eye-only materials blink with Sunshine's eye animations.
Luigi's hat is the cap model, so it comes off as Mario's cap does.

Needs Python 3 with numpy and Pillow. From the repository's folder:

```sh
python3 -I tools/mp7-characters/gcm_extract.py "Mario Party 7 (USA).iso" build/mp7-characters/mp7
python3 -I tools/mp7-characters/gcm_extract.py rom/GMSE01.iso build/mp7-characters/sms
python3 -I tools/mp7-characters/rarc.py build/mp7-characters/sms/files/data/mario.szs build/mp7-characters/sms_mario
python3 -I tools/mp7-characters/mp7_to_sms.py build/mp7-characters/mp7/files/data build/mp7-characters/sms_mario/mario build/mp7-characters/chars \
    mario luigi peach daisy wario waluigi yoshi kinopio kinopiko teresa minikoopa catherine karon donkey
python3 -I tools/mp7-characters/pack_characters.py build/mp7-characters/sms/files/data/mario.szs build/mp7-characters/chars mods/characters
```

The names are Mario Party 7's file names (`kinopio` Toad, `kinopiko` Toadette, `teresa` Boo, `minikoopa` Koopa Kid, `catherine` Birdo, `karon` Dry Bones, `donkey` Donkey Kong); `pack_characters.py` writes each as a pack `<id>/` with `model.szs` and `name.txt`, the name the game shows.
Their voices come from the same disc:

```sh
python3 -I tools/mp7-characters/mp7_voices.py "Mario Party 7 (USA).iso" mods/characters
```

writes each pack's `voice/`, one WAV per Mario voice cue (`MV21_JUMP_SMALL_01.wav`, ...), which the port plays in place of Mario's voice as that character ([mods/README.md](../../mods/README.md#characters-other-characters)).
Each character has 24 voice lines in Mario Party 7 (`sound/MP7_SND.msm` group 4, laid out by the game's `CharVoiceSeTable`, which is not the character order); eleven are placed by where its code plays them (star get, "Yahoo!", hurt, win, cheer, shocked, knocked back, falling, lose, dismay, squashed), and `mp7_voices.py`'s `CUES` table gives each of Mario's cues the lines that suit it, taking them in turn for a cue's variants.
Mario's snores keep his voice. Any WAV can be swapped for another.
`extract_voices.py "Mario Party 7 (USA).iso" out` writes every character's voice lines and movement sounds as they are, for listening.

`preview_pose.py out.png yaw model.bmd anim.bck frame ...` renders a converted model posed by an animation, for checking one without the game.

Each character keeps its Mario Party 7 size (Donkey Kong and Waluigi are much taller than Mario, with his collision); Yoshi, Koopa Kid, Donkey Kong and Boo, whose eyes are part of the face's texture, do not blink.
