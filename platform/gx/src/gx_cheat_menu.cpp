#include "gx_cheat_menu.h"
#include "gx_glcache.h"
#include "gx_internal.h"

#include <SDL.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

extern "C" int port_cheat_god_mode;
extern "C" int port_cheat_movement_speed;
extern "C" int port_cheat_jump_height;
extern "C" int port_cheat_infinite_lives;
extern "C" int port_cheat_infinite_fludd;
extern "C" int port_cheat_coin_multiplier;

namespace {

bool s_ready = false;
bool s_visible = false;

const char* const kMovementItems[] = {"OFF", "2x", "4x"};
const int kMovementFactors[] = {1, 2, 4};
const char* const kJumpItems[] = {"OFF", "2x", "4x", "6x"};
const int kJumpFactors[] = {1, 2, 4, 6};
const char* const kCoinItems[] = {"OFF", "2x", "5x", "10x"};
const int kCoinFactors[] = {1, 2, 5, 10};

void factorRow(const char* label, const char* id, int& factor, const char* const items[], const int factors[], int count) {
    int index = 0;
    for (int i = 0; i < count; i++)
        if (factors[i] == factor) index = i;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(140.0f);
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::Combo(id, &index, items, count))
        factor = factors[index];
}

// The menu sees no events while hidden, so it lets go of everything it held.
void releaseInput() {
    SDL_CaptureMouse(SDL_FALSE);
    ImGuiIO& io = ImGui::GetIO();
    io.ClearEventsQueue();
    io.ClearInputKeys();
    io.ClearInputMouse();
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
        bool godMode = port_cheat_god_mode != 0;
        if (ImGui::Checkbox("God Mode", &godMode))
            port_cheat_god_mode = godMode ? 1 : 0;
        bool infiniteLives = port_cheat_infinite_lives != 0;
        if (ImGui::Checkbox("Infinite Lives", &infiniteLives))
            port_cheat_infinite_lives = infiniteLives ? 1 : 0;
        bool infiniteFludd = port_cheat_infinite_fludd != 0;
        if (ImGui::Checkbox("Infinite FLUDD", &infiniteFludd))
            port_cheat_infinite_fludd = infiniteFludd ? 1 : 0;
        ImGui::SeparatorText("MOVEMENT");
        factorRow("Movement Speed", "##movement", port_cheat_movement_speed, kMovementItems, kMovementFactors, 3);
        factorRow("Jump Height", "##jump", port_cheat_jump_height, kJumpItems, kJumpFactors, 4);
        ImGui::SeparatorText("ITEMS");
        factorRow("Coin Multiplier", "##coins", port_cheat_coin_multiplier, kCoinItems, kCoinFactors, 4);
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
    drawMenu();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (!s_visible) releaseInput();
}

}  // namespace gx
