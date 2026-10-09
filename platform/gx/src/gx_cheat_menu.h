// In-game cheat menu (F9): Dear ImGui drawn over the presented frame in the
// SDL window. Without Dear ImGui in the build these do nothing.
#ifndef SMS_GX_CHEAT_MENU_H
#define SMS_GX_CHEAT_MENU_H

struct SDL_Window;
union SDL_Event;

namespace gx {

#ifdef SMS_GX_HAVE_CHEAT_MENU
bool cheatMenuInit(SDL_Window* window, void* glContext);
void cheatMenuShutdown();
bool cheatMenuAvailable();
bool cheatMenuVisible();
void cheatMenuToggle();
void cheatMenuProcessEvent(const SDL_Event& ev);
// Draws into the bound framebuffer, between presenting the frame and the
// swap. Its close button can hide the menu.
void cheatMenuRender();
#else
inline bool cheatMenuInit(SDL_Window*, void*) { return false; }
inline void cheatMenuShutdown() {}
inline bool cheatMenuAvailable() { return false; }
inline bool cheatMenuVisible() { return false; }
inline void cheatMenuToggle() {}
inline void cheatMenuProcessEvent(const SDL_Event&) {}
inline void cheatMenuRender() {}
#endif

}  // namespace gx

#endif
