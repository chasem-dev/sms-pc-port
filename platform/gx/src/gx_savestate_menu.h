// The save state menu (src/port_include/port_savestate.h), drawn with the debug
// overlay's renderer: the states in the save state folder, newest first, with
// a picture of the selected one, to load or delete. The pause menu's "Load
// State" opens it. Also the line that says a state was saved or loaded.
#ifndef SMS_GX_SAVESTATE_MENU_H
#define SMS_GX_SAVESTATE_MENU_H

union SDL_Event;

namespace gx {

#ifdef SMS_GX_HAVE_SDL2
bool savestateMenuVisible();
// True when the event was the menu's and must not reach the game.
bool savestateMenuHandleEvent(const SDL_Event& ev);
// Once per event pump: opens the menu when the game asked, and held-direction repeats.
void savestateMenuTick();
// On the GL thread, between presenting the frame and the swap: the menu and the message.
void savestateMenuRender(int winW, int winH);
#else
inline bool savestateMenuVisible() { return false; }
inline bool savestateMenuHandleEvent(const SDL_Event&) { return false; }
inline void savestateMenuTick() {}
inline void savestateMenuRender(int, int) {}
#endif

}  // namespace gx

#endif
