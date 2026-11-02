// Player 2's character in split-screen co-op (coop_character.cpp,
// ext/coop/README.md): SMS_COOP_CHARACTER and SMS_COOP_LOOK, from the outfit
// the launcher's Character page chose for player 2.
// Game code: C++03.
#ifndef COOP_CHARACTER_H
#define COOP_CHARACTER_H

class TMario;

namespace coop_character {

// As a stage's player 2 is about to be made: their character's archive read
// for this stage (into the heap in force, the stage's), or none when they are
// player 1's character, Luigi (no SMS_COOP_CHARACTER), or it can't be.
void stageStart();
// Around player 2's load and loadAfter: their archive on the "mario" volume
// in player 1's place, and player 1's back after.
void enter();
void leave();
// Player 2 plays as the chosen character this stage (else as Luigi, Mario's
// model recoloured).
bool chosen();
// Player 2 is Mario this stage (chosen), whose textures are then their own,
// in their outfit's colours (dress).
bool mario();
// Who player 2 is, for the notices: "Luigi", or the chosen character's name.
const char* name();
// "him" for Luigi; "them" for a chosen character.
const char* them();
// After player 2's models are made, with textures of their own (as Luigi's
// are): a chosen Mario painted in SMS_COOP_LOOK's colours, or shown as he is
// on the disc, never in player 1's (the launcher's SMS_MARIO_LOOK pack).
void dress(TMario* m);

} // namespace coop_character

#endif
