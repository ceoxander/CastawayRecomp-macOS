// Window (SDL3), D3D11 device and presentation. The window lives on its own thread, which
// also runs SDL's event loop (window, keyboard, mouse and gamepad events); the D3D11 swap
// chain presents into it from the render thread.
#include "video.h"

#ifdef __APPLE__
#include <SDL3/SDL.h>
#include <atomic>
#ifdef CASTAWAY_DXMT
#include "color_macos.h"
#include <algorithm>
#include <array>
#include <functional>
#include <mutex>
#include <vector>
#endif
#include "../config.h"
#include "../input/gamepad.h"
#include "../input/bindings.h"
#include "../kernel/host.h"
#include "../log.h"
#include "../ui/overlay.h"
#include "../ios/save_backup.h"

#ifdef CASTAWAY_DXMT
SDL_Texture* Video_CreateAppleLogo(SDL_Renderer* renderer);
SDL_Texture* Video_CreateStartupCredit(SDL_Renderer* renderer);
SDL_Texture* Video_CreateStartPrompt(SDL_Renderer* renderer, bool spanish, bool playstation);
#endif

VideoConfig g_videoConfig;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;

namespace {
SDL_Window* s_window = nullptr;
std::atomic<bool> s_closed{false};
std::atomic<bool> s_focused{true};

#ifdef CASTAWAY_DXMT
SDL_Renderer* s_renderer = nullptr;
SDL_Texture* s_texture = nullptr;
SDL_Texture* s_appleLogo = nullptr;
SDL_Texture* s_startupCredit = nullptr;
uint64_t s_introUntil = 0;
// Auto-confirm the platform-specific Wii setup warnings.
enum class Startup { Off, WiiPrompt };
std::atomic<Startup> s_startup{Startup::Off};
std::atomic<bool> s_startScreen{false};
SDL_Texture* s_startPrompt = nullptr;
Uint32 s_frameEvent = 0;
std::mutex s_frameMutex;

struct Frame
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};
Frame s_pendingFrame;
bool s_framePending = false;
float s_frameTimeMs = 0.0f;
std::array<float, 512> s_presentTimes{};
size_t s_presentCount = 0, s_presentIndex = 0;
uint64_t s_lastPresent = 0;
bool s_appliedVsync = false;

void PresentOnMainThread(Frame& frame)
{
    if (s_appliedVsync != g_videoConfig.vsync)
    {
        s_appliedVsync = g_videoConfig.vsync;
        if (!SDL_SetRenderVSync(s_renderer, s_appliedVsync ? 1 : 0))
            LOG_WARN("SDL VSync: {}", SDL_GetError());
    }
    if (SDL_GetTicks() < s_introUntil)
    {
        int width = 0, height = 0;
        SDL_GetRenderOutputSize(s_renderer, &width, &height);
        const float size = SDL_min(width, height) * 0.18f;
        const SDL_FRect logo{(width - size) * 0.5f, (height - size) * 0.5f, size, size};
        SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
        SDL_RenderClear(s_renderer);
        if (s_appleLogo) SDL_RenderTexture(s_renderer, s_appleLogo, nullptr, &logo);
        if (s_startupCredit)
        {
            const float cw = SDL_min(width * 0.72f, size * 2.65f);
            const float ch = cw * s_startupCredit->h / s_startupCredit->w;
            const SDL_FRect credit{(width - cw) * 0.5f, logo.y + size * 1.20f, cw, ch};
            SDL_RenderTexture(s_renderer, s_startupCredit, nullptr, &credit);
        }
        static const char* shotPath = getenv("BKR_UI_SHOT_STARTUP");
        if (shotPath)
        {
            if (SDL_Surface* shot = SDL_RenderReadPixels(s_renderer, nullptr))
            {
                SDL_SaveBMP(shot, shotPath);
                SDL_DestroySurface(shot);
            }
            shotPath = nullptr;
        }
        SDL_RenderPresent(s_renderer);
        return;
    }
    {
        std::lock_guard lock(s_frameMutex);
        if (!s_framePending) return;
        std::swap(frame, s_pendingFrame);
        s_framePending = false;
    }

    int width = 0, height = 0;
    SDL_GetRenderOutputSize(s_renderer, &width, &height);
    const float aspect = g_videoConfig.widescreen ? 16.0f / 9.0f : 4.0f / 3.0f;
    const float w = SDL_min(float(width), float(height) * aspect);
    const float h = w / aspect;
    const SDL_FRect dest{(width - w) * 0.5f, (height - h) * 0.5f, w, h};
    SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
    SDL_RenderClear(s_renderer);
    if (!Video_DrawColorPass(s_renderer, frame.pixels.data(), frame.width, frame.height, width, height,
                             dest, g_config.saturation / 100.0f, g_config.sharpness / 100.0f))
    {
        if (s_texture && (s_texture->w != frame.width || s_texture->h != frame.height))
        {
            SDL_DestroyTexture(s_texture);
            s_texture = nullptr;
        }
        if (!s_texture)
        {
            s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                          frame.width, frame.height);
            if (!s_texture) FATAL("SDL frame texture creation failed: {}", SDL_GetError());
            SDL_SetTextureBlendMode(s_texture, SDL_BLENDMODE_NONE);
            SDL_SetTextureScaleMode(s_texture, SDL_SCALEMODE_LINEAR);
        }

        SDL_UpdateTexture(s_texture, nullptr, frame.pixels.data(), frame.width * 4);
        SDL_RenderTexture(s_renderer, s_texture, nullptr, &dest);
    }
    Overlay_Render(width, height);
    const bool playstationPrompt = s_startScreen && !Input_ScriptActive() && Gamepad_IsPlayStation() &&
        Host_StrICmp(Binding_Pad(0).c_str(), "a") == 0;
    const bool spanishPrompt = g_config.language == 4;
    const bool showStartPrompt = s_startScreen && !Input_ScriptActive() && (spanishPrompt || playstationPrompt);
    if (showStartPrompt)
    {
        const int mode = (spanishPrompt ? 1 : 0) | (playstationPrompt ? 2 : 0);
        static int previousMode = -1;
        if (mode != previousMode)
        {
            if (s_startPrompt) SDL_DestroyTexture(s_startPrompt);
            s_startPrompt = Video_CreateStartPrompt(s_renderer, spanishPrompt, playstationPrompt);
            previousMode = mode;
        }
        const float panelWidth = std::min(590.0f, width * 0.72f);
        const float panelHeight = 68.0f;
        const SDL_FRect panel{(width - panelWidth) * 0.5f, height * 0.625f, panelWidth, panelHeight};
        SDL_SetRenderDrawBlendMode(s_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(s_renderer, 5, 27, 37, 246);
        SDL_RenderFillRect(s_renderer, &panel);
        const SDL_FRect label{panel.x + 12, panel.y + 5, panel.w - 24, panel.h - 10};
        if (s_startPrompt) SDL_RenderTexture(s_renderer, s_startPrompt, nullptr, &label);
        SDL_SetRenderDrawBlendMode(s_renderer, SDL_BLENDMODE_NONE);
        static const char* startShot = getenv("BKR_UI_SHOT_START_PROMPT");
        if (startShot)
        {
            if (SDL_Surface* shot = SDL_RenderReadPixels(s_renderer, nullptr))
            {
                SDL_SaveBMP(shot, startShot);
                SDL_DestroySurface(shot);
                LOG_INFO("Spanish title prompt screenshot saved to {}", startShot);
            }
            startShot = nullptr;
        }
    }
    if (SDL_RenderPresent(s_renderer))
    {
        const uint64_t now = SDL_GetTicksNS();
        if (s_lastPresent && now - s_lastPresent < 5000000000ull)
        {
            const float elapsed = float(now - s_lastPresent) / 1000000.0f;
            if (elapsed < 250.0f)
                s_frameTimeMs = s_frameTimeMs > 0.0f ? s_frameTimeMs * 0.9f + elapsed * 0.1f : elapsed;
            s_presentTimes[s_presentIndex++ % s_presentTimes.size()] = elapsed;
            s_presentCount = std::min(s_presentCount + 1, s_presentTimes.size());
        }
        s_lastPresent = now;
    }
}
#endif
}

const char* Video_DeviceHelp() { return "Cannot initialize the macOS video subsystem."; }

bool Video_Init()
{
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        LOG_ERROR("SDL video init failed: {}", SDL_GetError());
        return false;
    }
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (g_videoConfig.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    s_window = SDL_CreateWindow("CastawayRecomp", g_videoConfig.windowWidth,
                                g_videoConfig.windowHeight, flags);
    if (!s_window)
    {
        LOG_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }
    s_focused = (SDL_GetWindowFlags(s_window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    Gamepad_Init();
#ifdef CASTAWAY_DXMT
    s_frameEvent = SDL_RegisterEvents(1);
    if (!s_frameEvent) return false;
    const bool dummy = strcmp(SDL_GetCurrentVideoDriver(), "dummy") == 0;
    if (g_config.macHdr && !dummy)
    {
        const SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetPointerProperty(properties, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, s_window);
        SDL_SetStringProperty(properties, SDL_PROP_RENDERER_CREATE_NAME_STRING, "metal");
        SDL_SetNumberProperty(properties, SDL_PROP_RENDERER_CREATE_OUTPUT_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB_LINEAR);
        s_renderer = SDL_CreateRendererWithProperties(properties);
        SDL_DestroyProperties(properties);
        if (!s_renderer) LOG_WARN("HDR renderer unavailable, using SDR: {}", SDL_GetError());
    }
    if (!s_renderer) s_renderer = SDL_CreateRenderer(s_window, dummy ? "software" : "metal");
    if (!s_renderer)
    {
        LOG_ERROR("SDL renderer creation failed: {}", SDL_GetError());
        return false;
    }
    s_appliedVsync = g_videoConfig.vsync;
    if (s_appliedVsync) SDL_SetRenderVSync(s_renderer, 1);
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    const HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        &level, 1, D3D11_SDK_VERSION, &g_device, nullptr, &g_ctx);
    if (FAILED(hr))
    {
        LOG_ERROR("DXMT D3D11CreateDevice failed {:08x}", static_cast<uint32_t>(hr));
        return false;
    }
    Overlay_InitSDL(s_window, s_renderer);
    if (!dummy && (!Input_ScriptActive() || getenv("BKR_UI_SHOT_STARTUP")))
    {
        s_appleLogo = Video_CreateAppleLogo(s_renderer);
        s_startupCredit = Video_CreateStartupCredit(s_renderer);
        s_introUntil = SDL_GetTicks() + 2500;
    }
    LOG_INFO("GX: DXMT device, SDL {} presentation", SDL_GetRendererName(s_renderer));
#endif
    return true;
}

void Video_RunEventLoop()
{
    SDL_Event e;
#ifdef CASTAWAY_DXMT
    Frame frame;
    bool overlayOpen = false;
#endif
    while (!s_closed)
    {
        const bool received = SDL_WaitEventTimeout(&e, 100);
#ifdef CASTAWAY_DXMT
        if (const bool open = Overlay_IsOpen(); open != overlayOpen)
        {
            overlayOpen = open;
            if (open) SDL_StartTextInput(s_window);
            else SDL_StopTextInput(s_window);
        }
        PresentOnMainThread(frame);
        if (received && (e.type == s_frameEvent || Overlay_HandleEvent(e))) continue;
#endif
        if (!received) continue;
        Gamepad_HandleEvent(e);
        switch (e.type)
        {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            s_closed = true;
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        {
            bool focused = e.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
            s_focused = focused;
            if (!focused) Input_StopRumble();
            extern std::atomic<bool> g_pauseRequested;
            g_pauseRequested = g_config.pauseOnFocusLoss && !focused;
            break;
        }
        case SDL_EVENT_KEY_DOWN:
            if (!e.key.repeat && e.key.scancode == SDL_SCANCODE_F5) SaveBackup_Request();
            if (!e.key.repeat && (e.key.scancode == SDL_SCANCODE_F11 ||
                (e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT))))
                Video_SetFullscreen(!(SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN));
            break;
        }
    }
    Input_StopRumble();
}

#ifdef CASTAWAY_DXMT
float Video_PresentationFrameTimeMs() { return s_frameTimeMs; }
float Video_PresentationOnePercentLow()
{
    if (s_presentCount < 100) return 0.0f;
    auto samples = s_presentTimes;
    std::sort(samples.begin(), samples.begin() + s_presentCount, std::greater<float>());
    const size_t slowCount = std::max<size_t>(1, (s_presentCount + 99) / 100);
    float total = 0.0f;
    for (size_t i = 0; i < slowCount; ++i) total += samples[i];
    return total > 0 ? 1000.0f * slowCount / total : 0.0f;
}

void Video_NotifyGuestState(uint32_t machine, uint32_t vtable)
{
    static uint32_t menuMachine = 0;
    constexpr uint32_t mainMenu = 0x8042d530;  // R42E69 TheSimsMainMenuState
    if (vtable == mainMenu && Overlay_UseNativeMenu())
    {
        menuMachine = machine;
        Overlay_NotifyGuestMainMenu(true);
    }
    else if (machine && machine == menuMachine)
    {
        menuMachine = 0;
        Overlay_NotifyGuestMainMenu(false);
    }
    // R42E69 boot state vtables, as identified in the game's linker symbols.
    switch (vtable)
    {
    case 0x8042d62c:  // TheSimsNGCHealthWarningState
    case 0x8042d608:  // TheSimsWiiNunchuckRequiredState
        s_startup = Startup::WiiPrompt;
        s_startScreen = false;
        break;
    case 0x8042d16c:  // StartScreenStartState; the USA movie leaves this prompt in English
        s_startup = Startup::Off;
        s_startScreen = true;
        break;
    default:
        s_startup = Startup::Off;
        s_startScreen = false;
        break;
    }
}

bool Video_StartupConfirms() { return s_startup == Startup::WiiPrompt; }

void Video_Present(ID3D11ShaderResourceView* view, float u0, float v0, float u1, float v1)
{
    ID3D11Resource* resource = nullptr;
    view->GetResource(&resource);
    auto* texture = static_cast<ID3D11Texture2D*>(resource);
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    const UINT x = static_cast<UINT>(u0 * desc.Width);
    const UINT y = static_cast<UINT>(v0 * desc.Height);
    const UINT right = static_cast<UINT>(u1 * desc.Width);
    const UINT bottom = static_cast<UINT>(v1 * desc.Height);
    desc.Width = right - x;
    desc.Height = bottom - y;

    // Keep staging copies in flight and only map completed work. GX and guest
    // interrupt delivery should never wait for the Metal presentation readback.
    static std::array<ID3D11Texture2D*, 3> staging{};
    static UINT stagedWidth = 0, stagedHeight = 0;
    static size_t oldest = 0, queued = 0;
    if (!staging.front() || stagedWidth != desc.Width || stagedHeight != desc.Height)
    {
        oldest = queued = 0;
        desc.BindFlags = 0;
        desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.Usage = D3D11_USAGE_STAGING;
        for (auto*& target : staging)
        {
            if (target) target->Release();
            target = nullptr;
            if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &target)))
                FATAL("DXMT could not allocate an XFB staging texture");
        }
        stagedWidth = desc.Width;
        stagedHeight = desc.Height;
    }

    if (queued < staging.size())
    {
        const D3D11_BOX box{x, y, 0, right, bottom, 1};
        g_ctx->CopySubresourceRegion(staging[(oldest + queued) % staging.size()], 0,
                                     0, 0, 0, texture, 0, &box);
        ++queued;
        g_ctx->Flush();
    }
    resource->Release();

    auto* ready = staging[oldest];
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT result = g_ctx->Map(ready, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (result == DXGI_ERROR_WAS_STILL_DRAWING) return;
    if (FAILED(result)) FATAL("DXMT could not read the XFB staging texture ({:08x})", (uint32_t)result);
    oldest = (oldest + 1) % staging.size();
    --queued;

    static Frame frame;
    frame.width = static_cast<int>(desc.Width);
    frame.height = static_cast<int>(desc.Height);
    const size_t pitch = size_t(desc.Width) * 4;
    frame.pixels.resize(pitch * desc.Height);
    auto* pixels = static_cast<const uint8_t*>(mapped.pData);
    for (UINT row = 0; row < desc.Height; ++row)
        memcpy(frame.pixels.data() + row * pitch, pixels + row * mapped.RowPitch, pitch);
    g_ctx->Unmap(ready, 0);

    bool wake;
    {
        std::lock_guard lock(s_frameMutex);
        std::swap(frame, s_pendingFrame);
        wake = !s_framePending;
        s_framePending = true;
    }
    if (wake)
    {
        SDL_Event event{};
        event.type = s_frameEvent;
        SDL_PushEvent(&event);
    }
}
#else
void Video_Present(ID3D11ShaderResourceView*, float, float, float, float) {}
#endif
bool Video_WindowClosed() { return s_closed; }
void Video_SetFullscreen(bool on)
{
    if (s_window) SDL_SetWindowFullscreen(s_window, on);
}
void Video_SetWindowSize(int width, int height)
{
    if (s_window && !(SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN))
        SDL_SetWindowSize(s_window, width, height);
}
bool Video_HasFocus() { return s_focused; }
void* Video_Window() { return s_window; }
#else
#include "../config.h"
#include "../log.h"
#include "../input/gamepad.h"
#include "../ui/overlay.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

VideoConfig g_videoConfig;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;

static SDL_Window* s_window = nullptr;
static void* s_nativeWindow = nullptr;   // what the swap chain presents to (HWND on Windows)
static IDXGISwapChain1* s_swapchain = nullptr;
static ID3D11RenderTargetView* s_backbufferRTV = nullptr;
static std::atomic<bool> s_closed{ false };
static std::atomic<bool> s_resized{ false };
static std::atomic<bool> s_focused{ false };
static std::atomic<bool> s_minimized{ false };
static std::atomic<int> s_clientW{ 1 }, s_clientH{ 1 };
static ID3D11VertexShader* s_blitVS = nullptr;
static ID3D11PixelShader* s_blitPS = nullptr;
static ID3D11SamplerState* s_linearSampler = nullptr;
static ID3D11Buffer* s_blitCB = nullptr;
static ID3D11RasterizerState* s_blitRS = nullptr;
static ID3D11BlendState* s_blitBS = nullptr;
static ID3D11DepthStencilState* s_blitDS = nullptr;
// UI screenshots of a minimized test window (BKR_UI_SHOT_OVERLAY): drawn off screen
static ID3D11Texture2D* s_shotTex = nullptr;
static ID3D11RenderTargetView* s_shotRTV = nullptr;
// requests from other threads, handled on the window thread
static Uint32 s_evtFullscreen = 0, s_evtWindowSize = 0;

static void Quit()
{
    // Closing the window quits. Saves are written to the host NAND as the game issues
    // them, so there is nothing to flush; exit directly rather than running teardown while
    // guest/GX/audio threads are mid-operation.
    s_closed = true;
    LOG_INFO("window closed, exiting");
    Input_StopRumble();
    fflush(nullptr);
    _Exit(0);
}

// false while minimized (no client area): the swap chain keeps its size then
static bool UpdateClientSize()
{
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(s_window, &w, &h);
    s_minimized = (SDL_GetWindowFlags(s_window) & SDL_WINDOW_MINIMIZED) || w <= 0 || h <= 0;
    if (s_minimized) return false;
    s_clientW = w;
    s_clientH = h;
    return true;
}

static void HandleEvent(const SDL_Event& e)
{
    if (Overlay_HandleEvent(e)) return;   // the settings overlay has the input
    Gamepad_HandleEvent(e);
    switch (e.type)
    {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        Quit();
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
        if (UpdateClientSize()) s_resized = true;
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        s_focused = e.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        if (!s_focused) Input_StopRumble();
        if (g_config.pauseOnFocusLoss)
        {
            extern std::atomic<bool> g_pauseRequested;
            g_pauseRequested = !s_focused;
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        if (e.key.repeat) break;
        if (e.key.scancode == SDL_SCANCODE_F11 || (e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)))
        {
            bool on = !(SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN);
            SDL_SetWindowFullscreen(s_window, on);
            g_config.fullscreen = on;
        }
        else if (e.key.scancode == SDL_SCANCODE_F1)
            Overlay_Toggle();
        break;
    default:
        if (e.type == s_evtFullscreen) SDL_SetWindowFullscreen(s_window, e.user.code != 0);
        else if (e.type == s_evtWindowSize && !(SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN))
            SDL_SetWindowSize(s_window, e.user.code >> 16, e.user.code & 0xFFFF);
        break;
    }
}

static void WindowThread(std::atomic<int>* ready)
{
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");   // focus is checked by the input code
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        LOG_ERROR("SDL video init failed: {}", SDL_GetError());
        *ready = -1;
        return;
    }
    s_evtFullscreen = SDL_RegisterEvents(2);
    s_evtWindowSize = s_evtFullscreen + 1;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (g_videoConfig.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    s_window = SDL_CreateWindow("The Sims 2: Castaway - Recompiled", g_videoConfig.windowWidth, g_videoConfig.windowHeight, flags);
    if (!s_window)
    {
        LOG_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
        *ready = -1;
        return;
    }
#ifdef _WIN32
    s_nativeWindow = SDL_GetPointerProperty(SDL_GetWindowProperties(s_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    s_nativeWindow = s_window;   // DXVK's native build presents to SDL windows
#endif
    UpdateClientSize();
    Gamepad_Init();
    *ready = 1;
    for (;;)
    {
        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, 100)) continue;
        do
        {
            HandleEvent(e);
        } while (SDL_PollEvent(&e));
        // the game draws its own Wii Remote pointer; the overlay needs the mouse cursor
        bool showCursor = Overlay_IsOpen();
        if (showCursor != SDL_CursorVisible()) showCursor ? SDL_ShowCursor() : SDL_HideCursor();
    }
}

static ID3DBlob* Compile(const char* src, const char* entry, const char* target)
{
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &err);
    if (FAILED(hr))
    {
        LOG_ERROR("shader compile failed: {}", err ? (const char*)err->GetBufferPointer() : "?");
        if (err) err->Release();
        return nullptr;
    }
    if (err) err->Release();
    return blob;
}

static const char* kBlitHLSL = R"(
cbuffer C : register(b0) { float4 uvRect; float4 dstRect; };
Texture2D tex : register(t0);
SamplerState samp : register(s0);
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
V vs(uint id : SV_VertexID) {
    V o;
    float2 t = float2((id << 1) & 2, id & 2) * 0.5;   // 0..1 quad corners (triangle strip of 4)
    t = float2(id & 1, id >> 1);
    o.pos = float4(lerp(dstRect.x, dstRect.z, t.x), lerp(dstRect.y, dstRect.w, t.y), 0, 1);
    o.uv = lerp(uvRect.xy, uvRect.zw, t);
    return o;
}
float4 ps(V i) : SV_Target { return float4(tex.Sample(samp, i.uv).rgb, 1.0); }
)";

static void CreateBackbufferRTV()
{
    ID3D11Texture2D* bb = nullptr;
    s_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    g_device->CreateRenderTargetView(bb, nullptr, &s_backbufferRTV);
    bb->Release();
}

// What a bug report needs to tell graphics problems apart: Wine/Proton (its D3D11 is DXVK
// or wined3d, its HLSL compiler vkd3d-shader) and which d3dcompiler DLL is loaded.
static void LogPlatform()
{
#ifdef _WIN32
    using WineVersionFn = const char*(__cdecl*)();
    if (auto wineVersion = (WineVersionFn)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version"))
        LOG_INFO("running under Wine {}", wineVersion());
    if (HMODULE dc = GetModuleHandleW(L"d3dcompiler_47.dll"))
    {
        wchar_t path[MAX_PATH];
        char utf8[MAX_PATH * 3];
        GetModuleFileNameW(dc, path, MAX_PATH);
        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr);
        LOG_INFO("HLSL compiler: {}", utf8);
    }
#endif
}

const char* Video_DeviceHelp()
{
#ifdef _WIN32
    return "Could not start Direct3D 11. Check that your graphics driver is installed and up to date.";
#else
    return "Could not start Direct3D 11 on Vulkan (DXVK). Check that a Vulkan driver for your GPU is "
           "installed (vulkaninfo lists it), e.g. mesa-vulkan-drivers or the NVIDIA driver.";
#endif
}

bool Video_Init()
{
    LogPlatform();
    std::atomic<int> ready{ 0 };
    std::thread(WindowThread, &ready).detach();
    while (ready == 0) std::this_thread::yield();
    if (ready < 0) return false;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, &fl, 1, D3D11_SDK_VERSION, &g_device, nullptr, &g_ctx);
    if (FAILED(hr))
    {
        LOG_ERROR("D3D11CreateDevice failed {:08x}", (uint32_t)hr);
        return false;
    }
    IDXGIDevice* dxgiDev = nullptr;
    g_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev);
    IDXGIAdapter* adapter = nullptr;
    dxgiDev->GetAdapter(&adapter);
    IDXGIFactory2* factory = nullptr;
    adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory);
    DXGI_ADAPTER_DESC ad;
    adapter->GetDesc(&ad);
    char name[128] = {};
    for (int i = 0; i < 127 && ad.Description[i]; i++) name[i] = ad.Description[i] < 128 ? (char)ad.Description[i] : '?';
    LOG_INFO("D3D11 adapter: {} (feature level {:x})", name, (uint32_t)g_device->GetFeatureLevel());

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = 0;
    sd.Height = 0;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = factory->CreateSwapChainForHwnd(g_device, (HWND)s_nativeWindow, &sd, nullptr, nullptr, &s_swapchain);
    if (FAILED(hr))
    {
        LOG_ERROR("CreateSwapChainForHwnd failed {:08x}", (uint32_t)hr);
        return false;
    }
    factory->MakeWindowAssociation((HWND)s_nativeWindow, DXGI_MWA_NO_ALT_ENTER);
    factory->Release();
    adapter->Release();
    dxgiDev->Release();
    CreateBackbufferRTV();

    ID3DBlob* vs = Compile(kBlitHLSL, "vs", "vs_5_0");
    ID3DBlob* ps = Compile(kBlitHLSL, "ps", "ps_5_0");
    if (!vs || !ps)
    {
        LOG_ERROR("present shaders failed to compile");
        return false;
    }
    g_device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &s_blitVS);
    g_device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &s_blitPS);
    vs->Release();
    ps->Release();
    D3D11_SAMPLER_DESC smp{};
    smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    smp.MaxLOD = D3D11_FLOAT32_MAX;
    g_device->CreateSamplerState(&smp, &s_linearSampler);
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 32;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_device->CreateBuffer(&bd, nullptr, &s_blitCB);
    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE;
    g_device->CreateRasterizerState(&rs, &s_blitRS);
    D3D11_BLEND_DESC bs{};
    bs.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_device->CreateBlendState(&bs, &s_blitBS);
    D3D11_DEPTH_STENCIL_DESC ds{};
    g_device->CreateDepthStencilState(&ds, &s_blitDS);
    Overlay_Init(s_window, g_device, g_ctx);
    return true;
}

void Video_Present(ID3D11ShaderResourceView* srv, float u0, float v0, float u1, float v1)
{
    if (s_resized.exchange(false))
    {
        g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
        if (s_backbufferRTV) s_backbufferRTV->Release();
        s_swapchain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
        CreateBackbufferRTV();
    }
    float w = (float)s_clientW, h = (float)s_clientH;
    ID3D11RenderTargetView* target = s_backbufferRTV;
    if (Overlay_ShotPending() && (s_minimized || w < 64))
    {
        if (!s_shotTex)
        {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = 1280; td.Height = 720; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.BindFlags = D3D11_BIND_RENDER_TARGET;
            g_device->CreateTexture2D(&td, nullptr, &s_shotTex);
            g_device->CreateRenderTargetView(s_shotTex, nullptr, &s_shotRTV);
        }
        target = s_shotRTV;
        w = 1280;
        h = 720;
    }
    D3D11_VIEWPORT vp{ 0, 0, w, h, 0, 1 };
    g_ctx->RSSetViewports(1, &vp);
    float black[4] = { 0, 0, 0, 1 };
    g_ctx->OMSetRenderTargets(1, &target, nullptr);
    g_ctx->ClearRenderTargetView(target, black);
    if (srv)
    {
        // letterbox to target aspect
        float aspect = g_videoConfig.widescreen ? 16.0f / 9.0f : 4.0f / 3.0f;
        float dw = w, dh = h;
        if (w / h > aspect) dw = h * aspect;
        else dh = w / aspect;
        float x0 = (w - dw) / 2, y0 = (h - dh) / 2;
        float cb[8] = { u0, v0, u1, v1,
                        x0 / w * 2 - 1, 1 - y0 / h * 2, (x0 + dw) / w * 2 - 1, 1 - (y0 + dh) / h * 2 };
        D3D11_MAPPED_SUBRESOURCE m;
        g_ctx->Map(s_blitCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
        memcpy(m.pData, cb, sizeof(cb));
        g_ctx->Unmap(s_blitCB, 0);
        g_ctx->IASetInputLayout(nullptr);
        g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        g_ctx->VSSetShader(s_blitVS, nullptr, 0);
        g_ctx->VSSetConstantBuffers(0, 1, &s_blitCB);
        g_ctx->PSSetShader(s_blitPS, nullptr, 0);
        g_ctx->PSSetShaderResources(0, 1, &srv);
        g_ctx->PSSetSamplers(0, 1, &s_linearSampler);
        g_ctx->RSSetState(s_blitRS);
        g_ctx->OMSetBlendState(s_blitBS, nullptr, 0xFFFFFFFF);
        g_ctx->OMSetDepthStencilState(s_blitDS, 0);
        g_ctx->GSSetShader(nullptr, nullptr, 0);
        g_ctx->Draw(4, 0);
        ID3D11ShaderResourceView* nullSrv = nullptr;
        g_ctx->PSSetShaderResources(0, 1, &nullSrv);
    }
    Overlay_Render(w, h);   // in-game settings, drawn over the game while it keeps running
    HRESULT hr = s_swapchain->Present(g_videoConfig.vsync ? 1 : 0, 0);
    if (FAILED(hr))
    {
        static int failures = 0;
        if (failures++ < 5)
            LOG_ERROR("Present failed {:08x} (device removed reason {:08x})", (uint32_t)hr, (uint32_t)g_device->GetDeviceRemovedReason());
    }
}

bool Video_WindowClosed() { return s_closed; }
bool Video_HasFocus() { return s_focused; }

static void PushUserEvent(Uint32 type, Sint32 code)
{
    if (!s_window || !type) return;
    SDL_Event e{};
    e.type = type;
    e.user.code = code;
    SDL_PushEvent(&e);
}
void Video_SetFullscreen(bool on) { PushUserEvent(s_evtFullscreen, on ? 1 : 0); }
void Video_SetWindowSize(int w, int h) { PushUserEvent(s_evtWindowSize, (Sint32)((std::min(w, 0x7FFF) << 16) | std::min(h, 0xFFFF))); }
void* Video_Window() { return s_window; }

#endif
