// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/MouseCursor.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Input
{
    MouseCursor MouseCursor::MakeSystem(SDL_SystemCursor id)
    {
        SDL_Cursor* c = SDL_CreateSystemCursor(id);
        return MouseCursor(c, /*owning=*/true);
    }

    MouseCursor MouseCursor::FromTexture2D(const Graphics::Texture2D& texture, const int originX, const int originY)
    {
        const auto format = texture.getFormatProperty();
        if (format != Graphics::SurfaceFormat::Color && format != Graphics::SurfaceFormat::ColorSrgb)
        {
            throw std::invalid_argument(
                "MouseCursor::FromTexture2D: only SurfaceFormat::Color or ColorSrgb textures are accepted for mouse cursors");
        }

        const int width  = texture.getWidthProperty();
        const int height = texture.getHeightProperty();

        std::vector<Color> pixels(
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height), Color::Transparent);
        texture.GetData(pixels.data(), static_cast<int>(pixels.size()));

        // Color carries a vtable pointer (IPackedVectorT), so it is not a tightly
        // packed RGBA8 buffer — extract each pixel's packed value into a raw array
        // before handing it to SDL. PackedValue's byte layout (R,G,B,A; see
        // Color.cpp) matches SDL_PIXELFORMAT_RGBA32 exactly.
        std::vector<uint32_t> rgba(pixels.size());
        for (std::size_t i = 0; i < pixels.size(); ++i)
        {
            rgba[i] = pixels[i].getPackedValueProperty();
        }

        SDL_Surface* surface = SDL_CreateSurfaceFrom(
            width, height, SDL_PIXELFORMAT_RGBA32,
            rgba.data(), width * static_cast<int>(sizeof(uint32_t)));
        if (surface == nullptr)
        {
            throw std::runtime_error(
                std::string("MouseCursor::FromTexture2D: SDL_CreateSurfaceFrom failed: ") + SDL_GetError());
        }

        // Lifetime (verified against SDL3 src/events/SDL_mouse.c, task 831): SDL_CreateSurfaceFrom
        // above does NOT copy — `surface` only references `rgba`. SDL_CreateColorCursor, however,
        // copies the pixels: our RGBA32 surface differs from the cursor's required ARGB8888, so SDL
        // makes an independent converted copy (SDL_ConvertSurface), the platform builds its own
        // cursor from it, and SDL destroys that copy before returning. So the cursor owns its
        // pixels and it is safe to destroy `surface` and let `rgba` go out of scope right after.
        SDL_Cursor* cursor = SDL_CreateColorCursor(surface, originX, originY);
        SDL_DestroySurface(surface);
        if (cursor == nullptr)
        {
            throw std::runtime_error(
                std::string("MouseCursor::FromTexture2D: SDL_CreateColorCursor failed: ") + SDL_GetError());
        }

        return MouseCursor(cursor, /*owning=*/true);
    }

    // Stock cursors are lazily constructed function-local statics (Meyer's singleton),
    // matching MonoGame's `static MouseCursor() { PlatformInitalize(); }` lazy static
    // constructor. Building them as plain static member initializers instead would run
    // SDL_CreateSystemCursor at static-init time, before SDL_Init() has necessarily run.
    MouseCursor& MouseCursor::getArrowProperty()
    {
        static MouseCursor instance = MakeSystem(SDL_SYSTEM_CURSOR_DEFAULT);
        return instance;
    }

    MouseCursor& MouseCursor::getCrosshairProperty()
    {
        static MouseCursor instance = MakeSystem(SDL_SYSTEM_CURSOR_CROSSHAIR);
        return instance;
    }

    MouseCursor& MouseCursor::getHandProperty()
    {
        static MouseCursor instance = MakeSystem(SDL_SYSTEM_CURSOR_POINTER);
        return instance;
    }

    MouseCursor& MouseCursor::getIBeamProperty()
    {
        static MouseCursor instance = MakeSystem(SDL_SYSTEM_CURSOR_TEXT);
        return instance;
    }

    MouseCursor& MouseCursor::getNoProperty()
    {
        static MouseCursor instance = MakeSystem(SDL_SYSTEM_CURSOR_NOT_ALLOWED);
        return instance;
    }

    MouseCursor& MouseCursor::getSizeAllProperty()
    {
        static MouseCursor instance = MakeSystem(SDL_SYSTEM_CURSOR_MOVE);
        return instance;
    }

    MouseCursor& MouseCursor::getSizeNESWProperty()
    {
        static MouseCursor instance = MakeSystem(SDL_SYSTEM_CURSOR_NESW_RESIZE);
        return instance;
    }

    MouseCursor& MouseCursor::getSizeNSProperty()
    {
        static MouseCursor instance = MakeSystem(SDL_SYSTEM_CURSOR_NS_RESIZE);
        return instance;
    }

    MouseCursor& MouseCursor::getSizeNWSEProperty()
    {
        static MouseCursor instance = MakeSystem(SDL_SYSTEM_CURSOR_NWSE_RESIZE);
        return instance;
    }

    MouseCursor& MouseCursor::getSizeWEProperty()
    {
        static MouseCursor instance = MakeSystem(SDL_SYSTEM_CURSOR_EW_RESIZE);
        return instance;
    }

    MouseCursor& MouseCursor::getWaitProperty()
    {
        static MouseCursor instance = MakeSystem(SDL_SYSTEM_CURSOR_WAIT);
        return instance;
    }

    MouseCursor& MouseCursor::getWaitArrowProperty()
    {
        static MouseCursor instance = MakeSystem(SDL_SYSTEM_CURSOR_PROGRESS);
        return instance;
    }

    MouseCursor::MouseCursor()
        : sdlCursor_(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT))
        , owning_(true)
    {
    }

    MouseCursor::MouseCursor(SDL_Cursor* sdlCursor, bool owning)
        : sdlCursor_(sdlCursor)
        , owning_(owning)
    {
    }

    MouseCursor::MouseCursor(MouseCursor&& other) noexcept
        : sdlCursor_(other.sdlCursor_)
        , owning_(other.owning_)
        , isDisposed_(other.isDisposed_)
    {
        other.sdlCursor_  = nullptr;
        other.owning_     = false;
        other.isDisposed_ = true;
    }

    MouseCursor& MouseCursor::operator=(MouseCursor&& other) noexcept
    {
        if (this != &other)
        {
            Dispose();
            sdlCursor_  = other.sdlCursor_;
            owning_     = other.owning_;
            isDisposed_ = other.isDisposed_;
            other.sdlCursor_  = nullptr;
            other.owning_     = false;
            other.isDisposed_ = true;
        }
        return *this;
    }

    MouseCursor::~MouseCursor()
    {
        Dispose();
    }

    void MouseCursor::Dispose()
    {
        if (isDisposed_)
        {
            return;
        }
        if (owning_ && sdlCursor_ != nullptr)
        {
            SDL_DestroyCursor(sdlCursor_);
        }
        sdlCursor_  = nullptr;
        isDisposed_ = true;
    }
}
