#include "gamepad.h"
#include "../gx/video.h"
#include "../log.h"
#include "../ui/overlay.h"
#include "bindings.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <utility>
#include <vector>

namespace
{
constexpr float kDeadzone = 0.15f;
constexpr int kStickMax = 100;          // GameCube stick gate
constexpr Uint32 kRumbleMs = 2000;      // the game switches the motor off itself; renewed while it stays on

// Gamepads are opened and closed on the window thread (SDL's events) and read on the game's
// CPU thread; SDL's gamepad calls lock internally, this lock guards our own state.
std::mutex s_lock;
bool s_ok = false;
std::vector<SDL_Gamepad*> s_pads;   // open gamepads
SDL_Gamepad* s_active = nullptr;    // the one last used
bool s_rumbling = false;
Uint64 s_rumbleSent = 0;
std::atomic<bool> s_reload{ true };                               // rebuild s_buttons from the bindings
std::vector<std::pair<SDL_GamepadButton, uint32_t>> s_buttons;   // button -> BTN_*

#ifdef __APPLE__
void SetDualSenseLight(SDL_Gamepad* gamepad)
{
    if (SDL_GetRealGamepadType(gamepad) != SDL_GAMEPAD_TYPE_PS5) return;
    if (SDL_SetGamepadLED(gamepad, 150, 220, 24))
        LOG_INFO("DualSense light set to yellow-green");
    else
        LOG_INFO("DualSense light unavailable: {}", SDL_GetError());
}
#endif

// -1..1 stick pair with a radial deadzone, scaled to the GameCube range
void Stick(SDL_Gamepad* g, SDL_GamepadAxis ax, SDL_GamepadAxis ay, int8_t& ox, int8_t& oy)
{
    float x = SDL_GetGamepadAxis(g, ax) / 32767.0f, y = -SDL_GetGamepadAxis(g, ay) / 32767.0f;
    float m = std::sqrt(x * x + y * y);
    if (m < kDeadzone)
    {
        ox = oy = 0;
        return;
    }
    float s = std::min(1.0f, (m - kDeadzone) / (1.0f - kDeadzone)) / m;
    ox = (int8_t)std::lround(std::clamp(x * s, -1.0f, 1.0f) * kStickMax);
    oy = (int8_t)std::lround(std::clamp(y * s, -1.0f, 1.0f) * kStickMax);
}

uint8_t Trigger(SDL_Gamepad* g, SDL_GamepadAxis a)
{
    int v = SDL_GetGamepadAxis(g, a);   // 0..32767
    return (uint8_t)std::clamp(v * 255 / 32767, 0, 255);
}

bool AnyInput(SDL_Gamepad* g)
{
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++)
        if (SDL_GetGamepadButton(g, (SDL_GamepadButton)b)) return true;
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++)
        if (std::abs(SDL_GetGamepadAxis(g, (SDL_GamepadAxis)a)) > 16000) return true;
    return false;
}

void StopRumbleLocked()
{
    if (s_active && s_rumbling) SDL_RumbleGamepad(s_active, 0, 0, 0);
    s_rumbling = false;
}
}   // namespace

void Gamepad_Init()
{
    std::lock_guard lk(s_lock);
    s_ok = true;
    LOG_INFO("SDL {}.{}.{} gamepad support", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
    // SDL queues GAMEPAD_ADDED events during startup, but enumerate here as well so
    // controllers already connected before the app opened are immediately available.
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count; ++i)
    {
        if (SDL_Gamepad* g = SDL_OpenGamepad(ids[i]))
        {
#ifdef __APPLE__
            SetDualSenseLight(g);
#endif
            s_pads.push_back(g);
            if (!s_active) s_active = g;
            LOG_INFO("gamepad connected at startup: {}", SDL_GetGamepadName(g) ? SDL_GetGamepadName(g) : "?");
        }
    }
    SDL_free(ids);
}

void Gamepad_HandleEvent(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_GAMEPAD_ADDED)
    {
        std::lock_guard lk(s_lock);
        for (SDL_Gamepad* existing : s_pads)
            if (SDL_GetGamepadID(existing) == e.gdevice.which) return;
        if (SDL_Gamepad* g = SDL_OpenGamepad(e.gdevice.which))
        {
#ifdef __APPLE__
            SetDualSenseLight(g);
#endif
            LOG_INFO("gamepad connected: {}", SDL_GetGamepadName(g) ? SDL_GetGamepadName(g) : "?");
            s_pads.push_back(g);
            if (!s_active) s_active = g;
        }
    }
    else if (e.type == SDL_EVENT_GAMEPAD_REMOVED)
    {
        std::lock_guard lk(s_lock);
        SDL_Gamepad* g = SDL_GetGamepadFromID(e.gdevice.which);
        if (!g) return;
        if (g == s_active)
        {
            s_active = nullptr;
            s_rumbling = false;
        }
        s_pads.erase(std::remove(s_pads.begin(), s_pads.end(), g), s_pads.end());
        SDL_CloseGamepad(g);
        if (!s_active && !s_pads.empty()) s_active = s_pads.front();
        LOG_INFO("gamepad disconnected");
    }
}

bool Gamepad_Read(PadState& p)
{
    std::lock_guard lk(s_lock);
    if (!s_ok) return false;
    // switch to whichever pad was used last
    for (SDL_Gamepad* g : s_pads)
        if (g != s_active && AnyInput(g))
        {
            StopRumbleLocked();
            s_active = g;
            break;
        }
    if (!s_active) return false;
    if (!Video_HasFocus())
    {
        StopRumbleLocked();
        return false;
    }
    SDL_Gamepad* g = s_active;
    if (s_rumbling && SDL_GetTicks() - s_rumbleSent > kRumbleMs / 2)
    {
        s_rumbleSent = SDL_GetTicks();
        SDL_RumbleGamepad(g, 0xC000, 0x8000, kRumbleMs);
    }
    if (s_reload.exchange(false))
    {
        s_buttons.clear();
        for (int i = 0; i < kControlCount; i++)
        {
            SDL_GamepadButton b = SDL_GetGamepadButtonFromString(Binding_Pad(i).c_str());
            if (b != SDL_GAMEPAD_BUTTON_INVALID) s_buttons.push_back({ b, kControls[i].bit });
        }
    }
    p = {};
    for (auto& [b, bit] : s_buttons)
        if (SDL_GetGamepadButton(g, b)) p.buttons |= bit;
    Stick(g, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, p.stickX, p.stickY);
    Stick(g, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY, p.substickX, p.substickY);
    p.triggerL = Trigger(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    p.triggerR = Trigger(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    // both stick clicks open/close the settings overlay
    static bool bothSticks = false;
    bool sticks = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_LEFT_STICK) && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    if (sticks && !bothSticks) Overlay_Toggle();
    bothSticks = sticks;
    // Back + Start together is HOME too (pads without a usable guide button)
    if ((p.buttons & (BTN_MINUS | BTN_PLUS)) == (BTN_MINUS | BTN_PLUS)) p.buttons = (p.buttons & ~(BTN_MINUS | BTN_PLUS)) | BTN_HOME;
    return true;
}

void Gamepad_ReloadBindings() { s_reload = true; }

bool Gamepad_IsPlayStation()
{
    std::lock_guard lk(s_lock);
    if (!s_active) return false;
    const auto type = SDL_GetRealGamepadType(s_active);
    return type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5;
}

void Gamepad_Rumble(bool on)
{
    std::lock_guard lk(s_lock);
    if (!s_ok || !s_active || on == s_rumbling) return;
    if (!on)
    {
        StopRumbleLocked();
        return;
    }
    s_rumbling = true;
    s_rumbleSent = SDL_GetTicks();
    SDL_RumbleGamepad(s_active, 0xC000, 0x8000, kRumbleMs);
}
