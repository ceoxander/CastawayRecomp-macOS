#include "overlay.h"
#include "settings_ui.h"
#include "ui_shot.h"
#include "../log.h"
#include "../input/input.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#ifdef CASTAWAY_DXMT
#include <imgui_impl_sdlrenderer3.h>
#include "../ios/save_backup.h"
#include "../config.h"
#include "../gx/video.h"
#include <mach/mach.h>
#include <sys/resource.h>
#elif !defined(__APPLE__)
#include <imgui_impl_dx11.h>
#endif
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

void App_Restart();
void Input_StopRumble();

namespace
{
SDL_Window* s_window = nullptr;
#ifdef CASTAWAY_DXMT
SDL_Renderer* s_renderer = nullptr;
ImFont* s_menuFont = nullptr;
ImGuiStyle s_baseStyle;
std::atomic<bool> s_mainMenu{false};
std::atomic<bool> s_newGameDialog{false};
std::atomic<uint64_t> s_mainMenuReadyAt{0};
bool s_performanceHud = false;
const char* s_menuShotPath = nullptr;
bool s_testNativeMenu = false;

bool NativeMenuOpen() { return s_mainMenu; }

void DrawMenuBackdrop(const ImGuiViewport& viewport)
{
    static SDL_Texture* photo = []() -> SDL_Texture*
    {
        const char* base = SDL_GetBasePath();
        if (!base) return nullptr;
        const std::string path = std::string(base) + "tropical-menu.png";
        SDL_Surface* source = SDL_LoadPNG(path.c_str());
        if (!source)
        {
            LOG_WARN("tropical menu image: {}", SDL_GetError());
            return nullptr;
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(s_renderer, source);
        SDL_DestroySurface(source);
        if (texture)
        {
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
            LOG_INFO("tropical menu image loaded");
        }
        return texture;
    }();

    auto* background = ImGui::GetBackgroundDrawList();
    const ImVec2 end(viewport.Pos.x + viewport.Size.x, viewport.Pos.y + viewport.Size.y);
    background->AddRectFilled(viewport.Pos, end, IM_COL32(10, 32, 42, 255));
    if (!photo || viewport.Size.x <= 0 || viewport.Size.y <= 0) return;

    const float aspect = (viewport.Size.x / viewport.Size.y) / (float(photo->w) / photo->h);
    const float phase = float(SDL_GetTicks() % 24000) * (6.2831853f / 24000.0f);
    const float zoom = 1.06f + 0.025f * (1.0f - std::cos(phase));
    const float u = std::min(1.0f, aspect) * 0.5f / zoom;
    const float v = std::min(1.0f, 1.0f / aspect) * 0.5f / zoom;
    const float x = 0.5f + (0.5f - u) * 0.45f * std::sin(phase);
    const float y = 0.5f + (0.5f - v) * 0.25f * std::sin(phase);
    background->AddImage((ImTextureID)(intptr_t)photo, viewport.Pos, end, {x - u, y - v}, {x + u, y + v});
    background->AddRectFilled(viewport.Pos, end, IM_COL32(6, 22, 26, 24));
}

uint64_t ResidentMiB()
{
    static uint64_t nextCheck = 0, resident = 0;
    const uint64_t now = SDL_GetTicks();
    if (now >= nextCheck)
    {
        task_vm_info_data_t info{};
        mach_msg_type_number_t size = TASK_VM_INFO_COUNT;
        if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &size) == KERN_SUCCESS)
            resident = info.phys_footprint / (1024 * 1024);
        nextCheck = now + 1000;
    }
    return resident;
}

float ProcessCpuPercent()
{
    static uint64_t previousTime = 0;
    static double previousCpu = 0;
    static float percent = 0;
    const uint64_t now = SDL_GetTicks();
    if (now - previousTime >= 1000)
    {
        rusage usage{};
        if (getrusage(RUSAGE_SELF, &usage) == 0)
        {
            const double cpu = usage.ru_utime.tv_sec + usage.ru_stime.tv_sec +
                (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000000.0;
            if (previousTime) percent = float((cpu - previousCpu) * 100000.0 / (now - previousTime));
            previousCpu = cpu;
            previousTime = now;
        }
    }
    return percent;
}

void DrawPerformanceHud(const ImGuiViewport& viewport)
{
    ImGui::SetNextWindowPos(ImVec2(viewport.Size.x - 18, 18), ImGuiCond_Always, ImVec2(1, 0));
    ImGui::SetNextWindowBgAlpha(0.87f);
    ImGui::Begin("##performance", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
    const float ms = Video_PresentationFrameTimeMs();
    ImGui::Text("SDL present: %.1f FPS   %.1f ms", ms > 0.0f ? 1000.0f / ms : 0.0f, ms);
    ImGui::Text("1%% low: %.1f FPS", Video_PresentationOnePercentLow());
    ImGui::TextDisabled("CPU %.0f%% · %llu MiB · %d×", ProcessCpuPercent(),
                        static_cast<unsigned long long>(ResidentMiB()), g_videoConfig.internalScale);
    ImGui::End();
}
#endif
ID3D11Device* s_dev = nullptr;
ID3D11DeviceContext* s_ctx = nullptr;
bool s_ready = false;
std::atomic<bool> s_open{ false };
std::atomic<bool> s_homePress{ false };
std::mutex s_msgMutex;
std::vector<SDL_Event> s_msgs;     // input events, replayed on the render thread
std::deque<std::string> s_texts;   // text of the queued text-input events (SDL owns the original)
// test aid: BKR_UI_SHOT_OVERLAY=<file.bmp> opens the overlay BKR_UI_SHOT_AFTER seconds
// (default 30) into the game, saves a screenshot a second later and closes it again
const char* s_shotPath = nullptr;
uint64_t s_shotOpenAt = 0;

bool IsInput(const SDL_Event& e)
{
    switch (e.type)
    {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: case SDL_EVENT_TEXT_INPUT: case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_WINDOW_FOCUS_GAINED: case SDL_EVENT_WINDOW_FOCUS_LOST: case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        return true;
    default:
        return false;
    }
}

}   // namespace

void Overlay_Init(SDL_Window* window, ID3D11Device* device, ID3D11DeviceContext* ctx)
{
    s_shotPath = getenv("BKR_UI_SHOT_OVERLAY");
#ifdef CASTAWAY_DXMT
    s_menuShotPath = getenv("BKR_UI_SHOT_MAIN_MENU");
    s_testNativeMenu = s_menuShotPath != nullptr;
    if (Input_ScriptActive() && !s_shotPath && !s_menuShotPath) return;
#else
    if (Input_ScriptActive() && !s_shotPath) return;   // scripted runs drive the game; no overlay
#endif
    if (s_shotPath)
    {
        const char* after = getenv("BKR_UI_SHOT_AFTER");
        s_shotOpenAt = SDL_GetTicks() + (after ? atoi(after) : 30) * 1000ull;
    }
    s_window = window;
    s_dev = device;
    s_ctx = ctx;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // nothing to remember between runs
    // the window thread shows and hides the system cursor (with the overlay)
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NoMouseCursorChange;
    SettingsUI_LoadFont();
#ifdef CASTAWAY_DXMT
    const char* menuFontPath = "/System/Library/Fonts/Supplemental/Arial Narrow Bold.ttf";
    if (FILE* fontFile = fopen(menuFontPath, "rb"))
    {
        fclose(fontFile);
        s_menuFont = io.Fonts->AddFontFromFileTTF(menuFontPath, 18.0f);
    }
#endif
    SettingsUI_ApplyStyle();
#ifdef CASTAWAY_DXMT
    s_baseStyle = ImGui::GetStyle();
    ImGui_ImplSDL3_InitForSDLRenderer(window, s_renderer);
    ImGui_ImplSDLRenderer3_Init(s_renderer);
#elif !defined(__APPLE__)
    ImGui_ImplSDL3_InitForD3D(window);
    ImGui_ImplDX11_Init(device, ctx);
#endif
    s_ready = true;
}

#ifdef CASTAWAY_DXMT
bool Overlay_UseNativeMenu() { return !Input_ScriptActive() || s_testNativeMenu; }

void Overlay_NotifyGuestMainMenu(bool active)
{
    if (!active) s_newGameDialog = false;
    if (active)
    {
        s_mainMenuReadyAt = SDL_GetTicks() + 950;
    }
    s_mainMenu = active;
}

void Overlay_NewGameDialogClosed(bool accepted)
{
    if (s_newGameDialog.exchange(false))
    {
        LOG_INFO("native main menu: new game confirmation {}", accepted ? "accepted" : "cancelled");
        if (!accepted) Overlay_NotifyGuestMainMenu(true);
    }
}

void Overlay_SelectNewGameForTest()
{
    s_newGameDialog = true;
    s_mainMenu = false;
    Input_SelectMacMenu(MacMenuAction::NewGame);
}

void Overlay_InitSDL(SDL_Window* window, SDL_Renderer* renderer)
{
    s_renderer = renderer;
    Overlay_Init(window, nullptr, nullptr);
}
#endif

bool Overlay_HandleEvent(const SDL_Event& e)
{
    // ImGui's SDL backend discovers controllers on device events. Forward them
    // even while the overlay is closed, and let the gamepad layer handle them too.
    if (s_ready && (e.type == SDL_EVENT_GAMEPAD_ADDED || e.type == SDL_EVENT_GAMEPAD_REMOVED))
    {
        std::lock_guard lk(s_msgMutex);
        s_msgs.push_back(e);
        return false;
    }
    if (!s_ready || !IsInput(e)) return false;
#ifdef CASTAWAY_DXMT
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_F3)
    {
        s_performanceHud = !s_performanceHud;
        return true;
    }
#endif
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
        (e.key.scancode == SDL_SCANCODE_F1 || (e.key.scancode == SDL_SCANCODE_ESCAPE && s_open)))
    {
        Overlay_Toggle();
        return true;
    }
    if (!Overlay_IsOpen()) return false;
    std::lock_guard lk(s_msgMutex);
    s_msgs.push_back(e);
    if (e.type == SDL_EVENT_TEXT_INPUT) s_texts.emplace_back(e.text.text ? e.text.text : "");
    // the game sees no keys or clicks while open; focus changes still reach the window
    return e.type != SDL_EVENT_WINDOW_FOCUS_GAINED && e.type != SDL_EVENT_WINDOW_FOCUS_LOST;
}

void Overlay_Toggle()
{
    if (!s_ready) return;
    s_open = !s_open;
    LOG_INFO("settings overlay {}", s_open ? "opened" : "closed");
    // text input (the texture pack folder field) only while the overlay is up
#ifndef CASTAWAY_DXMT
    if (s_window) s_open ? SDL_StartTextInput(s_window) : SDL_StopTextInput(s_window);
#endif
}

bool Overlay_IsOpen()
{
#ifdef CASTAWAY_DXMT
    return s_open || NativeMenuOpen();
#else
    return s_open;
#endif
}

bool Overlay_TakeHomePress() { return s_homePress.exchange(false); }

bool Overlay_ShotPending() { return s_shotPath && s_shotOpenAt && SDL_GetTicks() >= s_shotOpenAt; }

void Overlay_Render(float w, float h)
{
    if (!s_ready) return;
    {
        std::vector<SDL_Event> msgs;
        std::deque<std::string> texts;
        {
            std::lock_guard lk(s_msgMutex);
            msgs.swap(s_msgs);
            texts.swap(s_texts);
        }
        for (auto& m : msgs)
        {
            if (m.type == SDL_EVENT_TEXT_INPUT)
            {
                m.text.text = texts.front().c_str();
                ImGui_ImplSDL3_ProcessEvent(&m);
                texts.pop_front();
                continue;
            }
            ImGui_ImplSDL3_ProcessEvent(&m);
        }
    }
    if (s_shotPath && s_shotOpenAt && SDL_GetTicks() >= s_shotOpenAt && !s_open) s_open = true;
#ifdef CASTAWAY_DXMT
    const bool saveNotice = g_config.showSaveNotice && SDL_GetTicks() < SaveBackup_NoticeUntil();
    const bool nativeMenu = NativeMenuOpen();
    const bool showBackdrop = nativeMenu || Input_MacMenuSelectionPending();
    // Keep the tropical cover during the synthetic pad press. The guest menu
    // remains the source of truth until its state machine actually transitions.
    const bool showMenu = nativeMenu && !s_open && !Input_MacMenuSelectionPending() &&
                          SDL_GetTicks() >= s_mainMenuReadyAt;
    const bool showHud = s_performanceHud;
#else
    constexpr bool saveNotice = false, showMenu = false, showHud = false, showBackdrop = false;
#endif
    if (!s_open && !saveNotice && !showBackdrop && !showHud) return;

#ifdef CASTAWAY_DXMT
    ImGui_ImplSDLRenderer3_NewFrame();
#elif !defined(__APPLE__)
    ImGui_ImplDX11_NewFrame();
#endif
    ImGui_ImplSDL3_NewFrame();
#ifndef CASTAWAY_DXMT
    ImGui::GetIO().DisplaySize = ImVec2(w, h);
#endif
    // Layout is in window points. The SDL renderer separately maps points to Retina pixels.
#ifdef CASTAWAY_DXMT
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float fit = std::min(display.x / 1024.0f, display.y / 720.0f);
    const float scale = std::clamp(fit * (g_config.uiScale / 100.0f), 0.60f, 2.5f);
    ImGui::GetStyle() = s_baseStyle;
    ImGui::GetStyle().ScaleAllSizes(scale);
#else
    const float scale = std::max(0.75f, ImGui::GetIO().DisplaySize.y / 720.0f);
#endif
    ImGui::GetStyle().FontScaleMain = scale;
    ImGui::NewFrame();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
#ifdef CASTAWAY_DXMT
    if (showHud) DrawPerformanceHud(*vp);
    if (showBackdrop) DrawMenuBackdrop(*vp);
    if (showMenu)
    {
        const bool spanish = Config_SpanishInterface();
        const float menuWidth = std::min(440.0f * scale, vp->Size.x * 0.88f);
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(menuWidth, 0), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24 * scale, 23 * scale));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.0f * scale);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.68f, 0.84f, 0.85f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.58f, 0.62f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.12f, 0.32f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.88f, 0.96f, 0.97f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.74f, 0.91f, 0.94f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60f, 0.82f, 0.85f, 1.0f));
        ImGui::Begin("##mac-main-menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::PushFont(s_menuFont, ImGui::GetStyle().FontSizeBase);
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.8f);
        ImGui::TextUnformatted("Castaway");
        ImGui::PopFont();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.87f, 0.37f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.94f, 0.45f, 1.0f));
        const bool newGame = ImGui::Button(spanish ? "Nueva partida" : "New game", ImVec2(-1, 42 * scale));
        ImGui::PopStyleColor(2);
        ImGui::SetItemDefaultFocus();
        const bool resume = ImGui::Button(spanish ? "Continuar" : "Continue", ImVec2(-1, 38 * scale));
        if (newGame || resume)
        {
            // The guest can open a confirmation dialog without changing its
            // main-menu state. Release input so that dialog remains visible and
            // accepts the player's response after the synthetic selection.
            s_newGameDialog = newGame;
            s_mainMenu = false;
            Input_SelectMacMenu(newGame ? MacMenuAction::NewGame : MacMenuAction::Continue);
        }
        if (ImGui::Button(spanish ? "Gráficos" : "Graphics", ImVec2(-1, 38 * scale)))
        {
            SettingsUI_ShowGraphics();
            s_open = true;
        }
        if (ImGui::Button(spanish ? "Ajustes" : "Settings", ImVec2(-1, 38 * scale))) s_open = true;
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("RECOMPILED BY HOODRICH X");
        ImGui::PopFont();
        ImGui::End();
        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(4);
    }
    if (saveNotice)
    {
        ImGui::SetNextWindowPos(ImVec2(vp->Size.x - 18, vp->Size.y - 18), ImGuiCond_Always, ImVec2(1, 1));
        ImGui::SetNextWindowBgAlpha(0.86f);
        ImGui::Begin("##save-notice", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
        ImGui::TextUnformatted(Config_SpanishInterface() ? "Partida guardada" : "Game saved");
        ImGui::End();
    }
#endif
    if (s_open)
    {
        ImVec2 size(std::min(vp->Size.x * 0.94f, 840.0f * scale), std::min(vp->Size.y * 0.90f, 600.0f * scale));
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        bool open = true;
#ifdef CASTAWAY_DXMT
        const bool spanish = Config_SpanishInterface();
        ImGui::SetNextWindowBgAlpha(0.985f);
#else
        constexpr bool spanish = false;
#endif
        if (ImGui::Begin(spanish ? "Ajustes###settings" : "Settings###settings", &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                              ImGuiWindowFlags_NoSavedSettings))
        {
            float bottom = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
            ImGui::BeginChild("panel", ImVec2(0, -bottom));
            SettingsUI_Draw(SettingsHost::InGame);
            ImGui::EndChild();
            if (ImGui::Button(spanish ? "Cerrar" : "Close")) open = false;
            ImGui::SameLine();
            if (ImGui::Button(spanish ? "Menú Wii" : "Wii HOME Menu"))
            {
                s_homePress = true;   // the game opens it on the next controller read
                open = false;
            }
            ImGui::SameLine();
            if (ImGui::Button(spanish ? "Reiniciar" : "Restart game")) App_Restart();
            ImGui::SameLine();
            if (ImGui::Button(spanish ? "Salir" : "Quit game"))
            {
                LOG_INFO("quit from the settings overlay");
                Input_StopRumble();
                fflush(nullptr);
                _Exit(0);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("F1 / Esc");
            SettingsUI_DrawVersion();
        }
        ImGui::End();
        if (!open) s_open = false;
        if (SettingsUI_TakeRestartRequest()) App_Restart();
    }

    ImGui::Render();
#ifdef CASTAWAY_DXMT
    const ImVec2 density = ImGui::GetIO().DisplayFramebufferScale;
    SDL_SetRenderScale(s_renderer, density.x, density.y);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), s_renderer);
    SDL_SetRenderScale(s_renderer, 1.0f, 1.0f);
    static int menuShotFrames = 0;
    if (showMenu && s_menuShotPath && ++menuShotFrames >= 16)
    {
        if (SDL_Surface* shot = SDL_RenderReadPixels(s_renderer, nullptr))
        {
            if (SDL_SaveBMP(shot, s_menuShotPath))
                LOG_INFO("native main menu screenshot saved to {}", s_menuShotPath);
            SDL_DestroySurface(shot);
        }
        s_menuShotPath = nullptr;
    }
#elif !defined(__APPLE__)
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#endif
    if (s_shotPath && s_shotOpenAt && SDL_GetTicks() >= s_shotOpenAt + 1000)
    {
#ifdef CASTAWAY_DXMT
        if (SDL_Surface* shot = SDL_RenderReadPixels(s_renderer, nullptr))
        {
            SDL_SaveBMP(shot, s_shotPath);
            SDL_DestroySurface(shot);
        }
#else
        UI_SaveBoundTarget(s_dev, s_ctx, s_shotPath);
#endif
        LOG_INFO("settings overlay screenshot saved to {}", s_shotPath);
        s_shotOpenAt = 0;
        s_open = false;
    }
}
