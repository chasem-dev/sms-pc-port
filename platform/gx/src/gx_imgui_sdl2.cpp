// Dear ImGui's SDL2 backend, built without SDL_syswm.h's X11 and DirectFB parts:
// those include their SDKs' headers, and the backend reads only the Windows and
// Cocoa window handles from SDL_SysWMinfo (whose union keeps its size).
#include <SDL_config.h>
#undef SDL_VIDEO_DRIVER_X11
#undef SDL_VIDEO_DRIVER_DIRECTFB

#include "imgui_impl_sdl2.cpp"
