#ifndef PORT_CHARACTER_H
#define PORT_CHARACTER_H

// Characters (platform/misc/port_character.cpp): packs in mods/characters/
// that take the place of Mario: a folder <id>/ with model.szs (in place of
// /data/mario.szs: his model, hands, caps and animations), voice/ (WAV files
// for his voice cues) and name.txt; a loose <id>.szs, <id>.voice/ and <id>.txt
// are read as a pack too. The list is Mario (the disc's archive, or a game-file
// mod's) and then each pack found there, by id; `character = <id>` in
// settings.txt (SMS_CHARACTER) picks one at start, and CHARACTER_NEXT (F4) or
// the settings menu change it while the game runs. The game mounts the chosen
// archive as each stage loads Mario (patch zzzzzz-character-01).

#ifdef __cplusplus
extern "C" {
#endif

int port_character_count(void);              // Mario and every archive found (1: none found)
const char* port_character_name(int index);  // "Mario", "Luigi", ...
const char* port_character_id(int index);    // "mario", the pack's folder name ("luigi"), ...
int port_character_find(const char* id);     // the index of the character with this id; -1 when there is none
const char* port_character_path(int index);  // the archive's file; NULL for Mario
const char* port_character_voice_dir(int index); // its voice folder (ends in /); NULL without one
int port_character_selected(void);
void port_character_select(int index);

// The game's side says whether it can switch characters (not with a code mod,
// such as Super Mario Eclipse, that loads its own character archives).
void port_character_set_available(int available);
int port_character_available(void);
// Whether switching characters is on: one player alone (not an online game, not split-screen co-op).
int port_character_switching(void);

// CHARACTER_NEXT presses (platform/pad), counted as the radar's MAP is.
extern int port_character_presses;
// Whether CHARACTER_NEXT was pressed since the last call. The count of presses
// taken stays here, out of game memory, so loading a save state (which puts
// game memory back) never turns an old press into a new one.
int port_character_take_press(void);

// Voices (platform/misc/port_character_voice.cpp): WAV files in the pack's
// voice folder, named after Mario's voice cues, in place of his voice
// waves. One entry per wave of wave bank 1 (w2ndLoad_0.aw) that a cue plays:
// the game's side fills `id`, then `aram` (the wave group's ARAM base) and the
// rest from the wave's entry, and writes the rest back after apply.
#define PORT_CHARACTER_VOICE_MAX 128
typedef struct PortCharacterVoiceWave {
	unsigned int id, aram;
	float rate;
	unsigned int offset, size, loop, loopStart, loopEnd, count;
	short last, penult;
} PortCharacterVoiceWave;
int port_character_voice_waves(PortCharacterVoiceWave* waves, int max); // fills the ids; returns how many
// Puts the character's voice in place (Mario's for index 0 or no files): ARAM
// and `waves`. `applied` is the character whose voice is in place now (-1:
// one not in the list); the game keeps it in its own memory, as ARAM and the
// waves' entries are, so a save state brings back all three together. Does
// nothing when `index` is `applied`. Returns whether `index`'s voice is in place.
int port_character_voice_apply(int index, int applied, PortCharacterVoiceWave* waves, int count);

#ifdef __cplusplus
}
#endif

#endif
