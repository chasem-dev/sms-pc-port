// Servers' game side (mp_game_api.h's "servers"): server commands carried out
// in this game, and a dedicated server's own ways. Game code: C++03.
#ifndef SMS_MP_SERVER_HPP
#define SMS_MP_SERVER_HPP

class TMarDirector;
class TMpPuppet;

// mp_game.cpp's, for the commands
TMpPuppet* MpPuppet(int slot);
bool MpWatchable(int slot);

// Each stage set up (before it plays).
void MpServerStageSetup();
// Each frame, multiplayer on: the commands due, the HUD's clock, the banner;
// a dedicated server's title and its player kept out of play; a stage a
// server mod asked for. `arrived`: playing a stage just loaded.
void MpServerFrame(TMarDirector* dir, bool arrived, bool playing);
// After each movement tick in a stage: the player held, watching.
void MpServerTick();
// The controllers as read: nothing from a player held.
void MpServerPadRead(void* status);
// May this game's own way out (a pipe, a door, Exit Area) go there? 0 while
// the host keeps the player in its stage (MPG_CMD_STAY).
int MpServerAllowLeave(int area);
// The host is setting off for a stage a server mod asked for (not a lost
// life's reload, though it is the same stage).
bool MpServerLoading();
// A dedicated server's own Mario (out of play, unseen) and title.
bool MpServerDedicated();
// The standing line (MPG_CMD_STATUS) shown when there is no news; "" none.
const char* MpServerStatusLine();
// Host, after the file select: a stage a server mod asked for instead (1).
int MpServerAfterFileSelect();

#endif
