#pragma once

#include <blend2d/blend2d.h>

#include <cstdint>

namespace CNA::Internal::Renderers::Blend2D
{
    /**
     * @brief Owns one Blend2D raster target: a premultiplied BL_FORMAT_PRGB32 BLImage plus a
     * synchronous BLContext attached to it for the whole surface lifetime.
     *
     * Used for both the backbuffer and every RenderTarget2D -- CNA's genuine 2D drawing surface.
     * The context stays attached between draws (Blend2D's default, threadCount=0, context is
     * synchronous: every recorded command applies immediately, so BLImage::get_data()/
     * make_mutable() safely observe already-completed draws without a separate flush/resolve
     * step). Presentation and CPU readback both go through ReadPixelsRgba(), which converts the
     * native premultiplied/byte-swapped storage to CNA's straight top-row-first RGBA8 contract.
     */
    class Blend2DSurface final
    {
    public:
        Blend2DSurface(int width, int height);
        ~Blend2DSurface();

        Blend2DSurface(const Blend2DSurface&) = delete;
        Blend2DSurface& operator=(const Blend2DSurface&) = delete;
        Blend2DSurface(Blend2DSurface&&) = delete;
        Blend2DSurface& operator=(Blend2DSurface&&) = delete;

        /// Replaces the backing image with a new, transparent-black surface of the given size.
        void Resize(int width, int height);

        /// Overwrites every pixel with @p r/@p g/@p b/@p a (0..1 floats), ignoring the current
        /// composition operator -- matches XNA GraphicsDevice::Clear's unconditional replace
        /// semantics, not a blended draw.
        void Clear(float r, float g, float b, float a);

        [[nodiscard]] int Width() const noexcept { return width_; }
        [[nodiscard]] int Height() const noexcept { return height_; }
        [[nodiscard]] BLContext& Context() noexcept { return context_; }
        [[nodiscard]] BLImage& Image() noexcept { return image_; }
        [[nodiscard]] const BLImage& Image() const noexcept { return image_; }

        /// Reads a complete rectangular region into tightly packed, top-row-first straight RGBA8
        /// bytes. Returns false when the requested rectangle falls outside the surface.
        [[nodiscard]] bool ReadPixelsRgba(int x, int y, int w, int h, std::uint8_t* destination) const;

    private:
        int width_;
        int height_;
        BLImage image_;
        BLContext context_;
    };
} // namespace CNA::Internal::Renderers::Blend2D
