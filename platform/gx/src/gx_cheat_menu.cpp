#include "gx_cheat_menu.h"
#include "gx_glcache.h"
#include "gx_internal.h"

#include <SDL.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

namespace {

// Placeholders: nothing reads them yet.
struct CheatToggles {
    bool godMode, infiniteLives, infiniteFludd;
    bool movementSpeed2x, jumpHeight2x;
};

CheatToggles s_toggles;
bool s_ready = false;
bool s_visible = false;

// The menu sees no events while hidden, so it lets go of everything it held.
void releaseInput() {
    SDL_CaptureMouse(SDL_FALSE);
    ImGuiIO& io = ImGui::GetIO();
    io.ClearEventsQueue();
    io.ClearInputKeys();
    io.ClearInputMouse();
}

// DEBUG_SAULO: the size and DPI scale the menu is laid out for
void debugLogLayout() {
    static float lastW = 0, lastH = 0, lastScale = 0;
    const ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x == lastW && io.DisplaySize.y == lastH && io.DisplayFramebufferScale.x == lastScale) return;
    lastW = io.DisplaySize.x;
    lastH = io.DisplaySize.y;
    lastScale = io.DisplayFramebufferScale.x;
    gx::logmsg("DEBUG_SAULO cheat menu: window %gx%g, framebuffer scale %g", double(lastW), double(lastH), double(lastScale));
}

void drawMenu() {
    const float margin = 16.0f;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - margin, viewport->WorkPos.y + margin),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Cheats", &s_visible, flags)) {
        ImGui::SeparatorText("PLAYER");
        ImGui::Checkbox("God Mode", &s_toggles.godMode);
        ImGui::Checkbox("Infinite Lives", &s_toggles.infiniteLives);
        ImGui::Checkbox("Infinite FLUDD", &s_toggles.infiniteFludd);
        ImGui::SeparatorText("MOVEMENT");
        ImGui::Checkbox("Movement Speed 2x", &s_toggles.movementSpeed2x);
        ImGui::Checkbox("Jump Height 2x", &s_toggles.jumpHeight2x);
        ImGui::Separator();
        ImGui::TextDisabled("F9 Close Menu");
    }
    ImGui::End();
}

}  // namespace

namespace gx {

bool cheatMenuInit(SDL_Window* window, void* glContext) {
    if (s_ready) return true;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    if (!ImGui_ImplSDL2_InitForOpenGL(window, glContext)) {
        ImGui::DestroyContext();
        logmsg("cheat menu: Dear ImGui's SDL2 backend did not start; continuing without the menu");
        return false;
    }
    // The backend sets these for the whole process; the game keeps SDL's defaults.
#ifdef SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "0");
#endif
#ifdef SDL_HINT_MOUSE_AUTO_CAPTURE
    SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "1");
#endif
    ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual);
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        logmsg("cheat menu: Dear ImGui's OpenGL 3 backend did not start; continuing without the menu");
        return false;
    }
    s_ready = true;
    logmsg("cheat menu ready (F9), Dear ImGui %s", IMGUI_VERSION);
    return true;
}

void cheatMenuShutdown() {
    if (!s_ready) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    s_ready = false;
    s_visible = false;
}

bool cheatMenuAvailable() { return s_ready; }
bool cheatMenuVisible() { return s_visible; }

void cheatMenuToggle() {
    if (!s_ready) return;
    s_visible = !s_visible;
    if (!s_visible) releaseInput();
}

void cheatMenuProcessEvent(const SDL_Event& ev) {
    if (s_visible) ImGui_ImplSDL2_ProcessEvent(&ev);
}

void cheatMenuRender() {
    if (!s_visible) return;
    glcInvalidate();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    debugLogLayout();
    drawMenu();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (!s_visible) releaseInput();
}

}  // namespace gx
