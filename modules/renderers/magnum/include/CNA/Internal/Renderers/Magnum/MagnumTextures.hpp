// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "MagnumStateMapping.hpp"

#include <Magnum/GL/CubeMapTexture.h>
#include <Magnum/GL/Texture.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Renderers::Magnum
{
    /**
     * @brief Applies a slot's sampler state to a 2D texture object.
     *
     * Magnum has no sampler-object wrapper, so `SamplerState` is expressed on the texture itself
     * -- which is why the whole state is written on every application rather than only the fields
     * that changed: a texture object is long-lived and shared between draws, so a value left
     * behind by an earlier draw's ordinal would otherwise persist into every later one.
     *
     * @param texture     Texture to configure.
     * @param state       Requested sampler state.
     * @param levelCount  Mip levels the texture really owns, so the sampled level range is clamped
     *                    to storage that exists.
     */
    void ApplySamplerStateTo(Mg::GL::Texture2D& texture, const MagnumSamplerState& state,
                             int levelCount);

    /**
     * @brief Applies a slot's sampler state to a cube-map texture object.
     *
     * @param texture    Cube map to configure.
     * @param state      Requested sampler state.
     * @param levelCount Mip levels the cube map really owns.
     */
    void ApplySamplerStateTo(Mg::GL::CubeMapTexture& texture, const MagnumSamplerState& state,
                             int levelCount);

    /**
     * @brief Applies a slot's sampler state to a 3D texture object.
     *
     * @param texture    Volume texture to configure.
     * @param state      Requested sampler state.
     * @param levelCount Mip levels the volume texture really owns.
     */
    void ApplySamplerStateTo(Mg::GL::Texture3D& texture, const MagnumSamplerState& state,
                             int levelCount);

    /**
     * @brief Number of mip levels a texture of the given extent can hold.
     *
     * @param width  Level-0 width in pixels.
     * @param height Level-0 height in pixels.
     * @return The full chain length, at least 1.
     */
    [[nodiscard]] int FullMipLevelCount(int width, int height);

    /** @brief Magnum-backed 2D texture. */
    class MagnumTextureRenderer : public ITextureRenderer
    {
    public:
        /**
         * @brief Allocates GL storage for every declared level and uploads level 0.
         *
         * @param data Source image; `mipLevels` declares how many levels are allocated.
         */
        explicit MagnumTextureRenderer(const ImageData& data);
        ~MagnumTextureRenderer() override = default;

        /** @brief Level-0 width in pixels. */
        [[nodiscard]] int GetWidth() const override { return width_; }
        /** @brief Level-0 height in pixels. */
        [[nodiscard]] int GetHeight() const override { return height_; }
        /** @brief Binds this texture to texture unit 0. */
        void BindGL(int /*unit*/) const override;
        /**
         * @brief Replaces the whole of level 0.
         *
         * @param rgba   Tightly packed RGBA8 rows, top row first.
         * @param stride Row pitch in bytes; rows are repacked when it exceeds `width * 4`.
         */
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        /**
         * @brief Replaces the whole of one mip level.
         *
         * @param rgba   Tightly packed RGBA8 rows for that level, top row first.
         * @param level  Mip level to write.
         * @param levelW Width of that level in pixels.
         * @param levelH Height of that level in pixels.
         */
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;
        /**
         * @brief Reads a sub-rectangle of a mip level back to the CPU.
         *
         * @param level      Mip level to read.
         * @param x          Left edge of the requested region, in pixels.
         * @param y          Top edge of the requested region, in pixels.
         * @param w          Width of the requested region, in pixels.
         * @param h          Height of the requested region, in pixels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes.
         * @return True once the whole region was read; false for an out-of-range level or region.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief The underlying Magnum texture, so a draw path can bind and configure it. */
        [[nodiscard]] Mg::GL::Texture2D& GetTexture() const { return *texture_; }
        /** @brief Mip levels this texture really allocated storage for. */
        [[nodiscard]] int GetLevelCount() const { return levelCount_; }

    private:
        // Held indirectly because Magnum's GL object wrappers are move-only and this renderer is
        // handed out through a unique_ptr to the interface; an indirection keeps the class itself
        // trivially destructible in the presence of a possibly-absent GL context.
        std::unique_ptr<Mg::GL::Texture2D> texture_;
        int width_ = 0;
        int height_ = 0;
        int levelCount_ = 1;
    };

    /** @brief Magnum-backed cube-map texture. */
    class MagnumTextureCubeRenderer : public ITextureCubeRenderer
    {
    public:
        /**
         * @brief Allocates GL storage for the whole cube.
         *
         * @param size          Edge length of each face in pixels.
         * @param mipMap        Whether a full mip chain is allocated.
         * @param surfaceFormat Raw `SurfaceFormat` ordinal; only `Color` storage is allocated
         *                      today, so the value is recorded rather than honoured.
         */
        MagnumTextureCubeRenderer(int size, bool mipMap, int surfaceFormat);
        ~MagnumTextureCubeRenderer() override = default;

        /**
         * @brief Uploads RGBA8 pixels into a sub-rectangle of a single face's mip level.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to write.
         * @param x          Left edge of the requested region, in texels.
         * @param y          Top edge of the requested region, in texels.
         * @param w          Width of the requested region, in texels.
         * @param h          Height of the requested region, in texels.
         * @param data       Source pixels, tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes.
         * @return True if the whole region was stored; false if nothing was stored.
         */
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;
        /**
         * @brief Reads a sub-rectangle of a single face's mip level back to the CPU.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to read.
         * @param x          Left edge of the requested region, in texels.
         * @param y          Top edge of the requested region, in texels.
         * @param w          Width of the requested region, in texels.
         * @param h          Height of the requested region, in texels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes.
         * @return True once the whole region was read; false if nothing was read.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;
        /** @brief Binds this cube map to texture unit 0. */
        void BindGL(int /*unit*/) const override;

        /** @brief The underlying Magnum cube map, so a draw path can bind and configure it. */
        [[nodiscard]] Mg::GL::CubeMapTexture& GetTexture() const { return *texture_; }
        /** @brief Mip levels this cube map really allocated storage for. */
        [[nodiscard]] int GetLevelCount() const { return levelCount_; }
        /** @brief Edge length of each face in pixels. */
        [[nodiscard]] int GetSize() const { return size_; }

    private:
        std::unique_ptr<Mg::GL::CubeMapTexture> texture_;
        int size_ = 0;
        int levelCount_ = 1;
    };

    /** @brief Magnum-backed 3D (volume) texture. */
    class MagnumTexture3DRenderer : public ITexture3DRenderer
    {
    public:
        /**
         * @brief Allocates GL storage for the whole volume.
         *
         * @param width         Level-0 width in voxels.
         * @param height        Level-0 height in voxels.
         * @param depth         Level-0 depth in voxels.
         * @param mipMap        Whether a full mip chain is allocated.
         * @param surfaceFormat Raw `SurfaceFormat` ordinal; only `Color` storage is allocated
         *                      today, so the value is recorded rather than honoured.
         */
        MagnumTexture3DRenderer(int width, int height, int depth, bool mipMap, int surfaceFormat);
        ~MagnumTexture3DRenderer() override = default;

        /**
         * @brief Uploads RGBA8 voxels into a sub-volume of a mip level.
         *
         * @param level      Mip level to write.
         * @param x          Left edge of the requested box, in voxels.
         * @param y          Top edge of the requested box, in voxels.
         * @param z          Front edge of the requested box, in voxels.
         * @param w          Width of the requested box, in voxels.
         * @param h          Height of the requested box, in voxels.
         * @param depth      Depth of the requested box, in voxels.
         * @param data       Source voxels, tightly packed RGBA8.
         * @param dataLength Size of @p data in bytes.
         * @return True if the whole box was stored; false if nothing was stored.
         */
        [[nodiscard]] bool SetData(int level, int x, int y, int z,
                                   int w, int h, int depth,
                                   const void* data, int dataLength) override;
        /**
         * @brief Reads a sub-volume of a mip level back to the CPU.
         *
         * @param level      Mip level to read.
         * @param x          Left edge of the requested box, in voxels.
         * @param y          Top edge of the requested box, in voxels.
         * @param z          Front edge of the requested box, in voxels.
         * @param w          Width of the requested box, in voxels.
         * @param h          Height of the requested box, in voxels.
         * @param depth      Depth of the requested box, in voxels.
         * @param data       Destination for the tightly packed RGBA8 box.
         * @param dataLength Size of @p data in bytes.
         * @return True once the whole box was read; false if nothing was read.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int z,
                                   int w, int h, int depth,
                                   void* data, int dataLength) const override;
        /** @brief Binds this volume texture to texture unit 0. */
        void BindGL(int /*unit*/) const override;

        /** @brief The underlying Magnum volume texture, so a draw path can bind and configure it. */
        [[nodiscard]] Mg::GL::Texture3D& GetTexture() const { return *texture_; }
        /** @brief Mip levels this volume texture really allocated storage for. */
        [[nodiscard]] int GetLevelCount() const { return levelCount_; }

    private:
        std::unique_ptr<Mg::GL::Texture3D> texture_;
        int width_ = 0;
        int height_ = 0;
        int depth_ = 0;
        int levelCount_ = 1;
    };
}
