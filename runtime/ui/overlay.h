#pragma once
#if defined(__APPLE__) && !defined(CASTAWAY_DXMT)
struct ID3D11Device;
struct ID3D11DeviceContext;
#else
#include <d3d11.h>
#endif

struct SDL_Window;
#ifdef CASTAWAY_DXMT
struct SDL_Renderer;
void Overlay_InitSDL(SDL_Window* window, SDL_Renderer* renderer);
void Overlay_NotifyGuestMainMenu(bool active);
void Overlay_NewGameDialogClosed(bool accepted);
void Overlay_SelectNewGameForTest();
bool Overlay_UseNativeMenu();
#endif
union SDL_Event;

// In-game settings overlay (Dear ImGui), opened with F1 or both gamepad stick clicks.
// The game keeps running underneath; while the overlay is open the game sees no input.
void Overlay_Init(SDL_Window* window, ID3D11Device* device, ID3D11DeviceContext* ctx);
// Window thread: forwards input to the overlay (F1 / Esc toggle it). True if the event was
// consumed.
bool Overlay_HandleEvent(const SDL_Event& e);
// Render thread, once per presented frame, with the target (w x h) bound.
void Overlay_Render(float w, float h);
// Test aid: a BKR_UI_SHOT_OVERLAY screenshot is due (the frame may be drawn off screen).
bool Overlay_ShotPending();
void Overlay_Toggle();
bool Overlay_IsOpen();
// Asks for a Wii HOME button press (overlay button); read by the gamepad code.
bool Overlay_TakeHomePress();
