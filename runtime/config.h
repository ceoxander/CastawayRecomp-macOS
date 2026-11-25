#pragma once
#include <string>

#ifdef __APPLE__
inline constexpr int kDefaultInternalScale = 2;
#else
inline constexpr int kDefaultInternalScale = 3;
#endif

// User settings loaded from config.ini next to the executable (created with
// defaults on first run). Command line options override them.
struct Config
{
    // [Video]
    bool macHdr = true;
    int saturation = 135;
    int sharpness = 20;
    int uiScale = 100;
    int internalScale = kDefaultInternalScale;       // EFB resolution multiplier (1 = 640x528)
    int windowWidth = 1280;
    int windowHeight = 720;
    bool fullscreen = false;
    bool vsync = false;
    bool widescreen = true;
    int anisotropy = 1;          // forced anisotropic filtering (1 = as the game sets it)
    bool asyncShaders = true;    // compile new shaders in the background (no hitches)
    bool gpuThread = true;       // GX processing/rendering on its own thread
    bool texturePack = true;     // load Dolphin-format custom textures if present
    std::string texturePackDir;  // empty: <exe>/textures, game/textures, Dolphin's Load/Textures
    // [System]
#ifdef __APPLE__
    int interfaceLanguage = 4;      // macOS launcher, overlay and notifications
#else
    int interfaceLanguage = 1;
#endif
    int language = 1;                // Wii IPL.LNG: 0 ja, 1 en, 2 de, 3 fr, 4 es, 5 it, 6 nl
    bool showSaveNotice = true;
    bool pauseOnFocusLoss = false;   // freeze the game while the window is in the background
    // [Audio]
    bool musicFront = true;          // fix: the music streamer's mixer arguments put it in the rear
    // [Input]
    bool rumble = true;
    // [Gamepad] / [Keyboard]: per game button (input/bindings.cpp kControls), a gamepad button
    // name and a comma-separated key list; empty means the default
    std::string padBind[13];
    std::string keyBind[13];
    // [Paths]
    std::string dataDir;
    std::string isoPath;
};

extern Config g_config;


// Loads config.ini (writing the defaults if it does not exist) and remembers its path.
void Config_Load(const std::string& path);
// Writes the current g_config back to config.ini (with comments).
bool Config_Save();
int Config_ParseLanguage(const std::string& value);
bool Config_SpanishInterface();
const std::string& Config_Path();
std::string Config_Text(const Config& c);
