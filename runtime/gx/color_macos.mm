#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "color_macos.h"
#include "../log.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>

namespace
{
NSString* const kShader = @R"(
#include <metal_stdlib>
using namespace metal;
struct Vertex { float2 xy; float2 uv; };
struct Raster { float4 position [[position]]; float2 uv; };
struct Color { float sat; float sharp; float hdr; float white; uint encode; uint upscale; };

vertex Raster castawayVertex(constant Vertex* vertices [[buffer(0)]], uint i [[vertex_id]])
{
    return {float4(vertices[i].xy, 0, 1), vertices[i].uv};
}

// Cubic B-spline: non-negative weights avoid overshoot and bright/dark halos.
float3 upscaleImage(texture2d<float> tex, sampler s, float2 uv)
{
    float2 size = float2(tex.get_width(), tex.get_height());
    float2 pos = uv * size - 0.5;
    float2 base = floor(pos);
    float2 f = pos - base;
    float2 f2 = f * f, f3 = f2 * f;
    float2 w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;
    float2 w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;
    float2 w2 = (1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0;
    float2 w3 = f3 / 6.0;
    float2 g0 = w0 + w1, g1 = w2 + w3;
    float2 p0 = (base - 1.0 + w1 / g0 + 0.5) / size;
    float2 p1 = (base + 1.0 + w3 / g1 + 0.5) / size;
    return tex.sample(s, float2(p0.x, p0.y)).rgb * g0.x * g0.y +
           tex.sample(s, float2(p1.x, p0.y)).rgb * g1.x * g0.y +
           tex.sample(s, float2(p0.x, p1.y)).rgb * g0.x * g1.y +
           tex.sample(s, float2(p1.x, p1.y)).rgb * g1.x * g1.y;
}

fragment float4 grade(Raster in [[stage_in]], texture2d<float> tex [[texture(0)]],
                      constant Color& color [[buffer(0)]])
{
    constexpr sampler sampleAt(coord::normalized, filter::linear, address::clamp_to_edge);
    constexpr float3 luma = float3(0.2126, 0.7152, 0.0722);
    float3 c = color.upscale ? upscaleImage(tex, sampleAt, in.uv) : tex.sample(sampleAt, in.uv).rgb;
    if (color.sharp > 0 && !color.upscale)
    {
        float2 pixel = 1.0 / float2(tex.get_width(), tex.get_height());
        float3 left  = tex.sample(sampleAt, in.uv - float2(pixel.x, 0)).rgb;
        float3 right = tex.sample(sampleAt, in.uv + float2(pixel.x, 0)).rgb;
        float3 up    = tex.sample(sampleAt, in.uv - float2(0, pixel.y)).rgb;
        float3 down  = tex.sample(sampleAt, in.uv + float2(0, pixel.y)).rgb;
        float3 lo = min(c, min(min(left, right), min(up, down)));
        float3 hi = max(c, max(max(left, right), max(up, down)));
        float contrast = dot(hi - lo, luma);
        float weight = color.sharp * (1.0 - smoothstep(0.15, 0.65, contrast));
        c = clamp(c + weight * (c - (left + right + up + down) * 0.25), lo, hi);
    }
    c = mix(float3(dot(c, luma)), c, color.sat);
    c = saturate(c);
    if (color.hdr > 1)
    {
        float highlight = smoothstep(0.5, 1.0, dot(c, luma));
        c *= 1.0 + highlight * min(color.hdr - 1.0, 0.5);
    }
    c *= color.white;
    if (color.encode)
        c = select(c * 12.92, 1.055 * pow(c, float3(1.0 / 2.4)) - 0.055, c > 0.0031308);
    return float4(c, 1);
}
)";

struct Vertex { float x, y, u, v; };
struct Color { float saturation, sharpness, headroom, white; uint32_t encode, upscale; };

id<MTLDevice> s_device = nil;
id<MTLRenderPipelineState> s_pipeline = nil;
MTLPixelFormat s_outputFormat = MTLPixelFormatInvalid;
bool s_pipelineFailed = false;

// CAMetalLayer has at most three outstanding window drawables. Retain one
// extra source texture for the frame being encoded on the main thread.
std::array<id<MTLTexture>, 4> s_textures{};
size_t s_textureIndex = 0;

bool BuildPipeline(CAMetalLayer* layer)
{
    if (s_device == layer.device && s_outputFormat == layer.pixelFormat)
        return s_pipeline != nil && !s_pipelineFailed;
    s_device = layer.device;
    s_outputFormat = layer.pixelFormat;
    s_textures = {};
    NSError* error = nil;
    id<MTLLibrary> library = [s_device newLibraryWithSource:kShader options:nil error:&error];
    if (library)
    {
        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = [library newFunctionWithName:@"castawayVertex"];
        desc.fragmentFunction = [library newFunctionWithName:@"grade"];
        desc.colorAttachments[0].pixelFormat = s_outputFormat;
        s_pipeline = [s_device newRenderPipelineStateWithDescriptor:desc error:&error];
    }
    else s_pipeline = nil;
    s_pipelineFailed = !s_pipeline;
    if (s_pipelineFailed) LOG_WARN("macOS color pipeline: {}", error.localizedDescription.UTF8String ?: "unknown error");
    return !s_pipelineFailed;
}
}

bool Video_DrawColorPass(SDL_Renderer* renderer, const uint8_t* rgba, int width, int height,
                         int outputWidth, int outputHeight, const SDL_FRect& destination,
                         float saturation, float sharpness)
{
    @autoreleasepool
    {
        auto* layer = (__bridge CAMetalLayer*)SDL_GetRenderMetalLayer(renderer);
        if (!layer || outputWidth <= 0 || outputHeight <= 0 || !BuildPipeline(layer)) return false;
        if (!SDL_FlushRenderer(renderer)) return false;
        id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)
            SDL_GetRenderMetalCommandEncoder(renderer);
        if (!encoder) return false;

        auto& texture = s_textures[s_textureIndex];
        if (!texture || texture.width != (NSUInteger)width || texture.height != (NSUInteger)height)
        {
            MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB
                                                                                          width:width height:height mipmapped:NO];
            desc.storageMode = MTLStorageModeShared;
            desc.usage = MTLTextureUsageShaderRead;
            texture = [s_device newTextureWithDescriptor:desc];
            if (!texture) return false;
        }
        [texture replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0 withBytes:rgba bytesPerRow:width * 4];

        const float left = destination.x * 2 / outputWidth - 1;
        const float right = (destination.x + destination.w) * 2 / outputWidth - 1;
        const float top = 1 - destination.y * 2 / outputHeight;
        const float bottom = 1 - (destination.y + destination.h) * 2 / outputHeight;
        const Vertex vertices[] = {{left, top, 0, 0}, {right, top, 1, 0},
                                   {left, bottom, 0, 1}, {right, bottom, 1, 1}};
        const SDL_PropertiesID properties = SDL_GetRendererProperties(renderer);
        const float hdr = SDL_GetFloatProperty(properties, SDL_PROP_RENDERER_HDR_HEADROOM_FLOAT, 1);
        const float white = SDL_GetFloatProperty(properties, SDL_PROP_RENDERER_SDR_WHITE_POINT_FLOAT, 1);
        const Color color{saturation, sharpness, std::max(1.0f, hdr), white > 0 ? white : 1.0f,
                          s_outputFormat == MTLPixelFormatBGRA8Unorm ? 1u : 0u,
                          destination.w > width && destination.h > height ? 1u : 0u};

        [encoder setRenderPipelineState:s_pipeline];
        [encoder setViewport:MTLViewport{0, 0, double(outputWidth), double(outputHeight), 0, 1}];
        [encoder setScissorRect:MTLScissorRect{0, 0, NSUInteger(outputWidth), NSUInteger(outputHeight)}];
        [encoder setVertexBytes:vertices length:sizeof(vertices) atIndex:0];
        [encoder setFragmentBytes:&color length:sizeof(color) atIndex:0];
        [encoder setFragmentTexture:texture atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        s_textureIndex = (s_textureIndex + 1) % s_textures.size();
        SDL_FlushRenderer(renderer);
        static bool logged = false;
        if (!logged)
        {
            logged = true;
            LOG_INFO("macOS color pass: saturation {:.2f}, sharpness {:.2f}, HDR headroom {:.2f}",
                     saturation, sharpness, color.headroom);
        }
        return true;
    }
}
