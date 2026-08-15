// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/MouseCursor.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Microsoft::Xna::Framework::Input
{
    MouseCursor MouseCursor::MakeSystem(const std::uint8_t shape)
    {
        return MouseCursor(shape, true);
    }

    MouseCursor MouseCursor::FromTexture2D(const Graphics::Texture2D& texture, const int originX, const int originY)
    {
        const auto format = texture.getFormatProperty();
        if (format != Graphics::SurfaceFormat::Color && format != Graphics::SurfaceFormat::ColorSrgbEXT)
        {
            throw std::invalid_argument(
                "MouseCursor::FromTexture2D: only SurfaceFormat::Color or ColorSrgbEXT textures are accepted for mouse cursors");
        }

        const int width  = texture.getWidthProperty();
        const int height = texture.getHeightProperty();
        if (width <= 0 || height <= 0
            || originX < 0 || originX >= width || originY < 0 || originY >= height)
        {
            // Keep the established public exception category: this path used to surface the
            // native cursor creator's invalid-hot-spot failure as std::runtime_error.
            throw std::runtime_error("MouseCursor::FromTexture2D: cursor hot spot is outside the texture");
        }
        if (static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max()
                                                  / static_cast<std::size_t>(height))
        {
            throw std::runtime_error("MouseCursor::FromTexture2D: texture dimensions overflow");
        }

        std::vector<Color> pixels(
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height), Color::Transparent);
        texture.GetData(pixels.data(), static_cast<int>(pixels.size()));

        // Color carries a vtable pointer (IPackedVectorT), so it is not a tightly packed RGBA8
        // buffer. Extract each pixel's packed value into the contract representation instead.
        // PackedValue is the contract's exact 0xAABBGGRR representation (see Color.cpp).
        std::vector<uint32_t> rgba(pixels.size());
        for (std::size_t i = 0; i < pixels.size(); ++i)
        {
            rgba[i] = pixels[i].getPackedValueProperty();
        }

        return MouseCursor(width, height, originX, originY, std::move(rgba));
    }

    // Stock cursors are lazily constructed function-local statics (Meyer's singleton), matching
    // MonoGame's static-constructor-triggered lazy initialization. They hold descriptions only;
    // native cursor creation belongs to the selected platform service.
    MouseCursor& MouseCursor::getArrowProperty()
    {
        static MouseCursor instance = MakeSystem(ShapeArrow);
        return instance;
    }

    MouseCursor& MouseCursor::getCrosshairProperty()
    {
        static MouseCursor instance = MakeSystem(ShapeCrosshair);
        return instance;
    }

    MouseCursor& MouseCursor::getHandProperty()
    {
        static MouseCursor instance = MakeSystem(ShapeHand);
        return instance;
    }

    MouseCursor& MouseCursor::getIBeamProperty()
    {
        static MouseCursor instance = MakeSystem(ShapeIBeam);
        return instance;
    }

    MouseCursor& MouseCursor::getNoProperty()
    {
        static MouseCursor instance = MakeSystem(ShapeNo);
        return instance;
    }

    MouseCursor& MouseCursor::getSizeAllProperty()
    {
        static MouseCursor instance = MakeSystem(ShapeSizeAll);
        return instance;
    }

    MouseCursor& MouseCursor::getSizeNESWProperty()
    {
        static MouseCursor instance = MakeSystem(ShapeSizeNESW);
        return instance;
    }

    MouseCursor& MouseCursor::getSizeNSProperty()
    {
        static MouseCursor instance = MakeSystem(ShapeSizeNS);
        return instance;
    }

    MouseCursor& MouseCursor::getSizeNWSEProperty()
    {
        static MouseCursor instance = MakeSystem(ShapeSizeNWSE);
        return instance;
    }

    MouseCursor& MouseCursor::getSizeWEProperty()
    {
        static MouseCursor instance = MakeSystem(ShapeSizeWE);
        return instance;
    }

    MouseCursor& MouseCursor::getWaitProperty()
    {
        static MouseCursor instance = MakeSystem(ShapeWait);
        return instance;
    }

    MouseCursor& MouseCursor::getWaitArrowProperty()
    {
        static MouseCursor instance = MakeSystem(ShapeWaitArrow);
        return instance;
    }

    MouseCursor::MouseCursor() = default;

    MouseCursor::MouseCursor(const std::uint8_t shape, const bool systemSingleton) noexcept
        : systemShape_(shape)
        , isSystemSingleton_(systemSingleton)
    {
    }

    MouseCursor::MouseCursor(const int width, const int height, const int originX, const int originY,
                             std::vector<std::uint32_t> rgba) noexcept
        : rgba_(std::move(rgba))
        , width_(width)
        , height_(height)
        , originX_(originX)
        , originY_(originY)
        , isCustom_(true)
    {
    }

    MouseCursor::MouseCursor(MouseCursor&& other) noexcept
        : systemShape_(other.systemShape_)
        , rgba_(std::move(other.rgba_))
        , width_(other.width_)
        , height_(other.height_)
        , originX_(other.originX_)
        , originY_(other.originY_)
        , isCustom_(other.isCustom_)
        , isDisposed_(other.isDisposed_)
        , isSystemSingleton_(other.isSystemSingleton_)
    {
        other.isDisposed_        = true;
        other.isSystemSingleton_ = false;
    }

    MouseCursor& MouseCursor::operator=(MouseCursor&& other) noexcept
    {
        if (this != &other)
        {
            Dispose();
            systemShape_       = other.systemShape_;
            rgba_              = std::move(other.rgba_);
            width_             = other.width_;
            height_            = other.height_;
            originX_           = other.originX_;
            originY_           = other.originY_;
            isCustom_          = other.isCustom_;
            isDisposed_        = other.isDisposed_;
            isSystemSingleton_ = other.isSystemSingleton_;
            other.isDisposed_        = true;
            other.isSystemSingleton_ = false;
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
        // Stock system-cursor singletons are shared for the process lifetime. Leave their
        // descriptions intact and usable for every caller.
        if (isSystemSingleton_)
        {
            return;
        }
        std::vector<std::uint32_t>().swap(rgba_);
        isDisposed_ = true;
    }
}
