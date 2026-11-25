#pragma once
#include <cstdint>

struct SDL_Renderer;
struct SDL_FRect;

bool Video_DrawColorPass(SDL_Renderer* renderer, const uint8_t* rgba, int width, int height,
                         int outputWidth, int outputHeight, const SDL_FRect& destination,
                         float saturation, float sharpness);
