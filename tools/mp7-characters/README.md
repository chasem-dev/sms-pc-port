# tools/mp7-characters

Turns Mario Party 7's characters into character packs for [`mods/characters/`](../../mods/README.md#characters-other-characters): each a folder with the character's model (a whole `/data/mario.szs` with the character in Mario's place) and voice, played with F4 (`CHARACTER_NEXT`) in the game.
Nothing of either game is in this folder; it reads both from your own discs.

Each character's `ma_mdl1.bmd` keeps Sunshine's 29 joints, 11 materials and shapes and its hierarchy, so the game's code and all of Mario's 199 animations work: the Mario Party 7 mesh is bound to Sunshine's joints by role (hip, thigh, shin, foot, shoulder, upper arm, forearm, hand, head), whatever its rig calls them, and the body animations (`.bck`) take the character's joint offsets, so he keeps his proportions.
The arms and legs keep Sunshine's bone directions (the animations twist them about those axes), the default fist is the character's fingers curled and turned to sit in the hand joint as Mario's does (fitted to his), and the hand models the game swaps in are the character's open hands.
What the character's motions start hidden (blink lids, spare eye frames) is left out; the textures go in one atlas (texture 0, which the hand and cap models share); characters with eye-only materials blink with Sunshine's eye animations.
Luigi's hat is the cap model, so it comes off as Mario's cap does.

The conversion ends with `refine.py`'s fixes on the finished files:
- parts left on Sunshine's `center` joint for want of a counterpart (Peach's and Daisy's gowns, Dry Bones' shell) are bound to the waist and the chest by height, so they turn with the hips and the body;
- in a gown, the legs swing less (the knees and ankles most) and the hem follows the thighs a little, so the legs stay under it and it sways as she walks;
- in a gown, the somersaults (rolls, the triple jump's flip, the ground pound's, the U-turn jump's) become pirouettes, a turn about the vertical with a lean, since a gown upside down is a bell; any other turn that would tip the gown past lying down (slipping onto her back, the spin into a bottle) is turned back just that far;
- every body animation is raised or lowered, frame by frame, so that while Mario stands the character's lowest point is where his is: nobody sinks into the floor or stands above it (the converter scales the hips' height, which left some in it and some over it; Boo, who has no legs, hovers a tenth of his height above it); between standing frames the correction eases from the one before to the one after; animations with no floor under the feet (the jumps, flips and bounces off enemies, climbing fences, trees and ladders, swimming) keep the character's standing correction throughout, so they keep their shape; it changes by at most 1.2% of the character's height a frame plus as fast as Mario's own body rises and falls in that animation (so a fall or getting up keeps up, and a walk does not bob), never leaves anything below the floor and lifts no more than that needs, and closes the seam of looping animations;
- while Mario hangs from a ledge or a rope, the character is moved so its hands are where his are, and each arm is bent (shoulder and elbow, in the plane it bends in) to reach his hand, so long arms (Donkey Kong, Waluigi) hold the ledge rather than the air above it; arms too short to reach both his hands (Toad, Dry Bones) put the middle of the character's own grip where the middle of his is, and come as near his hands as they reach; toward standing, the hand's aim moves back to its own;
- Peach's and Daisy's hands, whose rigs bend each finger at one joint (a hook rather than a fist), are their open hands with the fingers curved a little at three joints;
- the open hand models are turned about the wrist to lie as Mario's do (fitted to his `ma_hnd2`, at most 60 degrees): they came with the turn that fitted the fist, and Donkey Kong's stood upright on a ledge;
- triangles with no texture of their own (an object Birdo only shows at times, on her left hand) are left out;
- the atlas's empty space takes the colours of the textures' edges, so texture coordinates clamped to an edge do not filter in black.

`refine.py <Sunshine data/mario.szs> <character .szs> [<out .szs>]` applies them to an archive made before.

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
`refine.py` takes the character's id from its pack's folder (`peach/model.szs`, as `pack_characters.py` writes them) or from a loose archive's name (`peach.szs`).

Each character keeps its Mario Party 7 size (Donkey Kong and Waluigi are much taller than Mario, with his collision); Yoshi, Koopa Kid, Donkey Kong and Boo, whose eyes are part of the face's texture, do not blink.
