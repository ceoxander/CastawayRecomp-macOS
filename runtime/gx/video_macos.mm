#import <AppKit/AppKit.h>

#include <SDL3/SDL.h>
#include <array>

SDL_Texture* Video_CreateAppleLogo(SDL_Renderer* renderer)
{
    @autoreleasepool
    {
        NSImage* logo = [NSImage imageWithSystemSymbolName:@"apple.logo" accessibilityDescription:nil];
        logo = [logo imageWithSymbolConfiguration:[NSImageSymbolConfiguration
            configurationWithPointSize:150 weight:NSFontWeightRegular]];
        CGImageRef mask = [logo CGImageForProposedRect:nullptr context:nil hints:nil];
        if (!mask) return nullptr;

        constexpr size_t size = 192;
        std::array<uint8_t, size * size * 4> pixels{};
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = CGBitmapContextCreate(pixels.data(), size, size, 8, size * 4, space,
            CGBitmapInfo(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big);
        CGColorSpaceRelease(space);
        if (!context) return nullptr;

        const CGFloat ratio = CGFloat(CGImageGetWidth(mask)) / CGImageGetHeight(mask);
        const CGFloat w = ratio < 1 ? size * ratio : size;
        const CGFloat h = w / ratio;
        CGContextDrawImage(context, CGRectMake((size - w) / 2, (size - h) / 2, w, h), mask);
        CGContextRelease(context);

        // SF Symbols are templates. Preserve coverage and use white foreground on black.
        for (size_t i = 0; i < pixels.size(); i += 4)
            pixels[i] = pixels[i + 1] = pixels[i + 2] = 255;
        SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                                SDL_TEXTUREACCESS_STATIC, size, size);
        if (texture)
        {
            SDL_UpdateTexture(texture, nullptr, pixels.data(), size * 4);
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        }
        return texture;
    }
}

SDL_Texture* Video_CreateStartupCredit(SDL_Renderer* renderer)
{
    @autoreleasepool
    {
        constexpr int width = 608, height = 92;
        NSImage* image = [NSImage imageWithSize:NSMakeSize(width, height) flipped:NO drawingHandler:^BOOL(NSRect bounds) {
            NSString* credit = @"RECOMPILED BY HOODRICH X";
            NSDictionary* attributes = @{
                NSFontAttributeName: [NSFont systemFontOfSize:34 weight:NSFontWeightSemibold],
                NSForegroundColorAttributeName: NSColor.whiteColor,
                NSKernAttributeName: @2.0,
            };
            const NSSize textSize = [credit sizeWithAttributes:attributes];
            [credit drawAtPoint:NSMakePoint((bounds.size.width - textSize.width) / 2,
                                           (bounds.size.height - textSize.height) / 2)
                 withAttributes:attributes];
            return YES;
        }];
        CGImageRef bitmap = [image CGImageForProposedRect:nullptr context:nil hints:nil];
        if (!bitmap) return nullptr;
        std::array<uint8_t, width * height * 4> pixels{};
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        const CGBitmapInfo layout = CGBitmapInfo(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big;
        CGContextRef context = CGBitmapContextCreate(pixels.data(), width, height, 8, width * 4, space, layout);
        CGColorSpaceRelease(space);
        if (!context) return nullptr;
        CGContextDrawImage(context, CGRectMake(0, 0, width, height), bitmap);
        CGContextRelease(context);

        SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                                SDL_TEXTUREACCESS_STATIC, width, height);
        if (texture)
        {
            SDL_UpdateTexture(texture, nullptr, pixels.data(), width * 4);
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        }
        return texture;
    }
}

SDL_Texture* Video_CreateStartPrompt(SDL_Renderer* renderer, bool spanish, bool playstation)
{
    @autoreleasepool
    {
        constexpr int width = 580, height = 76;
        NSImage* image = [NSImage imageWithSize:NSMakeSize(width, height) flipped:NO drawingHandler:^BOOL(NSRect bounds) {
            NSString* text = spanish ? (playstation ? @"PRESIONA X" : @"PRESIONA EL BOTÓN A")
                                     : (playstation ? @"PRESS X" : @"PRESS A");
            NSDictionary* attributes = @{
                NSFontAttributeName: [NSFont systemFontOfSize:30 weight:NSFontWeightSemibold],
                NSForegroundColorAttributeName: NSColor.whiteColor,
                NSKernAttributeName: @1.4,
            };
            const NSSize textSize = [text sizeWithAttributes:attributes];
            [text drawAtPoint:NSMakePoint((bounds.size.width - textSize.width) / 2,
                                         (bounds.size.height - textSize.height) / 2)
                withAttributes:attributes];
            return YES;
        }];
        CGImageRef bitmap = [image CGImageForProposedRect:nullptr context:nil hints:nil];
        if (!bitmap) return nullptr;
        std::array<uint8_t, width * height * 4> pixels{};
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        const CGBitmapInfo layout = CGBitmapInfo(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big;
        CGContextRef context = CGBitmapContextCreate(pixels.data(), width, height, 8, width * 4, space, layout);
        CGColorSpaceRelease(space);
        if (!context) return nullptr;
        CGContextDrawImage(context, CGRectMake(0, 0, width, height), bitmap);
        CGContextRelease(context);
        SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                                SDL_TEXTUREACCESS_STATIC, width, height);
        if (texture)
        {
            SDL_UpdateTexture(texture, nullptr, pixels.data(), width * 4);
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        }
        return texture;
    }
}
