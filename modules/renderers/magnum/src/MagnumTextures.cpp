// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Magnum/MagnumTextures.hpp"

#include <Corrade/Containers/Array.h>
#include <Corrade/Containers/ArrayView.h>
#include <Corrade/Containers/StridedArrayView.h>
#include <Magnum/GL/PixelFormat.h>
#include <Magnum/GL/TextureFormat.h>
#include <Magnum/Image.h>
#include <Magnum/ImageView.h>
#include <Magnum/PixelFormat.h>

#include <algorithm>
#include <cstring>

namespace CNA::Internal::Renderers::Magnum
{
    namespace
    {
        /// Bytes one RGBA8 texel occupies, the only storage format this renderer allocates today.
        constexpr int kBytesPerPixel = 4;

        Mg::Float ClampAnisotropy(int requested)
        {
            const Mg::Float supported = Mg::GL::Sampler::maxMaxAnisotropy();
            const auto asked = static_cast<Mg::Float>(std::max(1, requested));
            return std::min(supported, asked);
        }

        Mg::GL::CubeMapCoordinate FaceCoordinate(int face)
        {
            switch (face)
            {
                case 1:  return Mg::GL::CubeMapCoordinate::NegativeX;
                case 2:  return Mg::GL::CubeMapCoordinate::PositiveY;
                case 3:  return Mg::GL::CubeMapCoordinate::NegativeY;
                case 4:  return Mg::GL::CubeMapCoordinate::PositiveZ;
                case 5:  return Mg::GL::CubeMapCoordinate::NegativeZ;
                default: return Mg::GL::CubeMapCoordinate::PositiveX;
            }
        }

        /// Extent of @p level of a chain whose level 0 is @p base, never smaller than 1.
        int LevelExtent(int base, int level)
        {
            return std::max(1, base >> level);
        }

        /**
         * @brief Copies a sub-rectangle out of a whole-level readback into a tight destination.
         *
         * @p source is the raw pointer behind `Image2D::data()`'s own `ArrayView` -- reached
         * through the view rather than through an implicit conversion, which only a deprecated
         * Corrade build provides.
         *
         * Magnum's `Texture::image()` always returns a complete level, so a partial `GetData`
         * request is served by reading the level and copying the requested rows out here rather
         * than by a second GL call.
         */
        void CopyRegion(const char* source, int sourceWidth,
                        int x, int y, int w, int h, void* destination)
        {
            auto* out = static_cast<uint8_t*>(destination);
            for (int row = 0; row < h; ++row)
            {
                const char* sourceRow =
                    source + (static_cast<std::size_t>(y + row) * sourceWidth + x) * kBytesPerPixel;
                std::memcpy(out + static_cast<std::size_t>(row) * w * kBytesPerPixel,
                            sourceRow, static_cast<std::size_t>(w) * kBytesPerPixel);
            }
        }
    }

    int FullMipLevelCount(int width, int height)
    {
        int levels = 1;
        int extent = std::max(width, height);
        while (extent > 1)
        {
            extent >>= 1;
            ++levels;
        }
        return levels;
    }

    void ApplySamplerStateTo(Mg::GL::Texture2D& texture, const MagnumSamplerState& state,
                             int levelCount)
    {
        Mg::GL::SamplerFilter minification{};
        Mg::GL::SamplerMipmap mipmap{};
        Mg::GL::SamplerFilter magnification{};
        DecomposeTextureFilter(state.filter, minification, mipmap, magnification);

        texture.setMinificationFilter(minification, mipmap)
               .setMagnificationFilter(magnification)
               .setWrapping({ToSamplerWrapping(state.addressU), ToSamplerWrapping(state.addressV)})
               .setBaseLevel(0)
               .setMaxLevel(std::max(0, levelCount - 1));

        // Anisotropy is a component of the ordinal, so it is written on every application: this
        // texture object is reused across draws, and a value raised by an earlier Anisotropic draw
        // would otherwise keep filtering anisotropically under every later Point/Linear one.
        texture.setMaxAnisotropy(state.filter == 2 ? ClampAnisotropy(state.maxAnisotropy) : 1.0f);
    }

    void ApplySamplerStateTo(Mg::GL::CubeMapTexture& texture, const MagnumSamplerState& state,
                             int levelCount)
    {
        Mg::GL::SamplerFilter minification{};
        Mg::GL::SamplerMipmap mipmap{};
        Mg::GL::SamplerFilter magnification{};
        DecomposeTextureFilter(state.filter, minification, mipmap, magnification);

        texture.setMinificationFilter(minification, mipmap)
               .setMagnificationFilter(magnification)
               .setWrapping({ToSamplerWrapping(state.addressU), ToSamplerWrapping(state.addressV)})
               .setBaseLevel(0)
               .setMaxLevel(std::max(0, levelCount - 1))
               .setMaxAnisotropy(state.filter == 2 ? ClampAnisotropy(state.maxAnisotropy) : 1.0f);
    }

    void ApplySamplerStateTo(Mg::GL::Texture3D& texture, const MagnumSamplerState& state,
                             int levelCount)
    {
        Mg::GL::SamplerFilter minification{};
        Mg::GL::SamplerMipmap mipmap{};
        Mg::GL::SamplerFilter magnification{};
        DecomposeTextureFilter(state.filter, minification, mipmap, magnification);

        const Mg::GL::SamplerWrapping wrapU = ToSamplerWrapping(state.addressU);
        const Mg::GL::SamplerWrapping wrapV = ToSamplerWrapping(state.addressV);
        texture.setMinificationFilter(minification, mipmap)
               .setMagnificationFilter(magnification)
               // XNA's SamplerState has an AddressW too, but the renderer interface carries only U
               // and V; the depth axis reuses U's mode, which is what every W-less caller means.
               .setWrapping({wrapU, wrapV, wrapU})
               .setBaseLevel(0)
               .setMaxLevel(std::max(0, levelCount - 1))
               .setMaxAnisotropy(state.filter == 2 ? ClampAnisotropy(state.maxAnisotropy) : 1.0f);
    }

    // ---- MagnumTextureRenderer ----

    MagnumTextureRenderer::MagnumTextureRenderer(const ImageData& data)
        : texture_(std::make_unique<Mg::GL::Texture2D>())
        , width_(data.width)
        , height_(data.height)
        , levelCount_(std::max(1, data.mipLevels))
    {
        levelCount_ = std::min(levelCount_, FullMipLevelCount(width_, height_));

        // Every declared level is given storage here, not only level 0: a texture that declares a
        // chain but allocates one level is mipmap-incomplete and samples as opaque black under any
        // filter carrying a mipmap term -- which, since every XNA TextureFilter ordinal carries
        // one, is all of them.
        texture_->setStorage(levelCount_, Mg::GL::TextureFormat::RGBA8, Mg::Vector2i{width_, height_});

        const std::size_t expected =
            static_cast<std::size_t>(width_) * height_ * kBytesPerPixel;
        if (!data.pixels.empty() && data.pixels.size() >= expected)
        {
            texture_->setSubImage(0, {}, Mg::ImageView2D{
                Mg::PixelFormat::RGBA8Unorm, Mg::Vector2i{width_, height_},
                Corrade::Containers::ArrayView<const void>{data.pixels.data(), expected}});
        }

        ApplySamplerStateTo(*texture_, MagnumSamplerState{}, levelCount_);
    }

    void MagnumTextureRenderer::BindGL(int /*unit*/) const
    {
        texture_->bind(0);
    }

    void MagnumTextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (rgba == nullptr || width_ <= 0 || height_ <= 0)
            return;

        const int tightStride = width_ * kBytesPerPixel;
        if (stride == tightStride)
        {
            texture_->setSubImage(0, {}, Mg::ImageView2D{
                Mg::PixelFormat::RGBA8Unorm, Mg::Vector2i{width_, height_},
                Corrade::Containers::ArrayView<const void>{
                    rgba, static_cast<std::size_t>(tightStride) * height_}});
            return;
        }

        // A padded source is repacked rather than uploaded row by row: one upload of a tight
        // buffer costs a single copy, where a per-row upload costs one GL call per row.
        std::vector<uint8_t> packed(static_cast<std::size_t>(tightStride) * height_);
        for (int row = 0; row < height_; ++row)
        {
            std::memcpy(packed.data() + static_cast<std::size_t>(row) * tightStride,
                        rgba + static_cast<std::size_t>(row) * stride,
                        static_cast<std::size_t>(tightStride));
        }
        texture_->setSubImage(0, {}, Mg::ImageView2D{
            Mg::PixelFormat::RGBA8Unorm, Mg::Vector2i{width_, height_},
            Corrade::Containers::ArrayView<const void>{packed.data(), packed.size()}});
    }

    void MagnumTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba,
                                                 int levelW, int levelH)
    {
        if (rgba == nullptr || level < 0 || level >= levelCount_ || levelW <= 0 || levelH <= 0)
            return;

        texture_->setSubImage(level, {}, Mg::ImageView2D{
            Mg::PixelFormat::RGBA8Unorm, Mg::Vector2i{levelW, levelH},
            Corrade::Containers::ArrayView<const void>{
                rgba, static_cast<std::size_t>(levelW) * levelH * kBytesPerPixel}});
    }

    bool MagnumTextureRenderer::GetData(int level, int x, int y, int w, int h,
                                       void* data, int dataLength) const
    {
        if (data == nullptr || level < 0 || level >= levelCount_ || w <= 0 || h <= 0)
            return false;
        if (dataLength < w * h * kBytesPerPixel)
            return false;

        const int levelW = LevelExtent(width_, level);
        const int levelH = LevelExtent(height_, level);
        if (x < 0 || y < 0 || x + w > levelW || y + h > levelH)
            return false;

        Mg::Image2D image{Mg::PixelFormat::RGBA8Unorm};
        texture_->image(level, image);
        if (image.size() != Mg::Vector2i{levelW, levelH})
            return false;

        CopyRegion(image.data().data(), levelW, x, y, w, h, data);
        return true;
    }

    // ---- MagnumTextureCubeRenderer ----

    MagnumTextureCubeRenderer::MagnumTextureCubeRenderer(int size, bool mipMap, int surfaceFormat)
        : texture_(std::make_unique<Mg::GL::CubeMapTexture>())
        , size_(std::max(1, size))
    {
        // SurfaceFormat is accepted and recorded but every cube face is allocated as RGBA8 -- the
        // one storage format this renderer implements, matching what its 2D textures allocate.
        (void)surfaceFormat;

        levelCount_ = mipMap ? FullMipLevelCount(size_, size_) : 1;
        texture_->setStorage(levelCount_, Mg::GL::TextureFormat::RGBA8, Mg::Vector2i{size_, size_});
        ApplySamplerStateTo(*texture_, MagnumSamplerState{}, levelCount_);
    }

    bool MagnumTextureCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                           const void* data, int dataLength)
    {
        if (data == nullptr || face < 0 || face > 5 || level < 0 || level >= levelCount_)
            return false;
        if (w <= 0 || h <= 0 || dataLength < w * h * kBytesPerPixel)
            return false;

        const int levelSize = LevelExtent(size_, level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize)
            return false;

        texture_->setSubImage(FaceCoordinate(face), level, Mg::Vector2i{x, y}, Mg::ImageView2D{
            Mg::PixelFormat::RGBA8Unorm, Mg::Vector2i{w, h},
            Corrade::Containers::ArrayView<const void>{
                data, static_cast<std::size_t>(w) * h * kBytesPerPixel}});
        return true;
    }

    bool MagnumTextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const
    {
        if (data == nullptr || face < 0 || face > 5 || level < 0 || level >= levelCount_)
            return false;
        if (w <= 0 || h <= 0 || dataLength < w * h * kBytesPerPixel)
            return false;

        const int levelSize = LevelExtent(size_, level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize)
            return false;

        Mg::Image2D image{Mg::PixelFormat::RGBA8Unorm};
        texture_->image(FaceCoordinate(face), level, image);
        if (image.size() != Mg::Vector2i{levelSize, levelSize})
            return false;

        CopyRegion(image.data().data(), levelSize, x, y, w, h, data);
        return true;
    }

    void MagnumTextureCubeRenderer::BindGL(int /*unit*/) const
    {
        texture_->bind(0);
    }

    // ---- MagnumTexture3DRenderer ----

    MagnumTexture3DRenderer::MagnumTexture3DRenderer(int width, int height, int depth,
                                                   bool mipMap, int surfaceFormat)
        : texture_(std::make_unique<Mg::GL::Texture3D>())
        , width_(std::max(1, width))
        , height_(std::max(1, height))
        , depth_(std::max(1, depth))
    {
        // See MagnumTextureCubeRenderer's constructor: RGBA8 is the one allocated storage format.
        (void)surfaceFormat;

        levelCount_ = mipMap ? FullMipLevelCount(std::max(width_, depth_), height_) : 1;
        texture_->setStorage(levelCount_, Mg::GL::TextureFormat::RGBA8,
                             Mg::Vector3i{width_, height_, depth_});
        ApplySamplerStateTo(*texture_, MagnumSamplerState{}, levelCount_);
    }

    bool MagnumTexture3DRenderer::SetData(int level, int x, int y, int z,
                                         int w, int h, int depth,
                                         const void* data, int dataLength)
    {
        if (data == nullptr || level < 0 || level >= levelCount_)
            return false;
        if (w <= 0 || h <= 0 || depth <= 0)
            return false;
        if (dataLength < w * h * depth * kBytesPerPixel)
            return false;

        const int levelW = LevelExtent(width_, level);
        const int levelH = LevelExtent(height_, level);
        const int levelD = LevelExtent(depth_, level);
        if (x < 0 || y < 0 || z < 0 || x + w > levelW || y + h > levelH || z + depth > levelD)
            return false;

        texture_->setSubImage(level, Mg::Vector3i{x, y, z}, Mg::ImageView3D{
            Mg::PixelFormat::RGBA8Unorm, Mg::Vector3i{w, h, depth},
            Corrade::Containers::ArrayView<const void>{
                data, static_cast<std::size_t>(w) * h * depth * kBytesPerPixel}});
        return true;
    }

    bool MagnumTexture3DRenderer::GetData(int level, int x, int y, int z,
                                         int w, int h, int depth,
                                         void* data, int dataLength) const
    {
        if (data == nullptr || level < 0 || level >= levelCount_)
            return false;
        if (w <= 0 || h <= 0 || depth <= 0)
            return false;
        if (dataLength < w * h * depth * kBytesPerPixel)
            return false;

        const int levelW = LevelExtent(width_, level);
        const int levelH = LevelExtent(height_, level);
        const int levelD = LevelExtent(depth_, level);
        if (x < 0 || y < 0 || z < 0 || x + w > levelW || y + h > levelH || z + depth > levelD)
            return false;

        Mg::Image3D image{Mg::PixelFormat::RGBA8Unorm};
        texture_->image(level, image);
        if (image.size() != Mg::Vector3i{levelW, levelH, levelD})
            return false;

        auto* out = static_cast<uint8_t*>(data);
        for (int slice = 0; slice < depth; ++slice)
        {
            const char* sourceSlice =
                image.data().data() + static_cast<std::size_t>(z + slice) * levelW * levelH * kBytesPerPixel;
            CopyRegion(sourceSlice, levelW, x, y, w, h,
                       out + static_cast<std::size_t>(slice) * w * h * kBytesPerPixel);
        }
        return true;
    }

    void MagnumTexture3DRenderer::BindGL(int /*unit*/) const
    {
        texture_->bind(0);
    }
}
