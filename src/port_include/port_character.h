#ifndef PORT_CHARACTER_H
#define PORT_CHARACTER_H

// Characters (platform/misc/port_character.cpp): archives in mods/characters/
// that take the place of Mario's own (/data/mario.szs: his model, hands, caps
// and animations). The list is Mario (the disc's archive, or a game-file mod's)
// and then each <id>.szs found there, by name; `character = <id>` in
// settings.txt (SMS_CHARACTER) picks one at start, and CHARACTER_NEXT (F4) or
// the settings menu change it while the game runs. The game mounts the chosen
// archive as each stage loads Mario (patch zzzzzz-character-01).

#ifdef __cplusplus
extern "C" {
#endif

int port_character_count(void);              // Mario and every archive found (1: none found)
const char* port_character_name(int index);  // "Mario", "Luigi", ...
const char* port_character_path(int index);  // the archive's file; NULL for Mario
int port_character_selected(void);
void port_character_select(int index);

// The game's side says whether it can switch characters (not with a code mod,
// such as Super Mario Eclipse, that loads its own character archives).
void port_character_set_available(int available);
int port_character_available(void);

// CHARACTER_NEXT presses (platform/pad), counted as the radar's MAP is.
extern int port_character_presses;

#ifdef __cplusplus
}
#endif

#endif
