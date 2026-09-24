// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4_modern_graphics.md GL4-0012: OpenGL4's plain texture resources -- Texture2D,
// TextureCube and Texture3D in every SurfaceFormat EasyGL stores -- ported from EasyGL's texture
// layer to desktop OpenGL 4.1 core.
//
// Two ES workarounds give way to the desktop mechanism with the same observable result:
//   - EasyGL mirrors every uncompressed cube face and volume level on the CPU and answers typed
//     readback from that mirror, because ES has no glGetTexImage. Here the level is read back
//     with glGetTexImage in the exact transfer layout it was uploaded in (a lossless round trip
//     for every format in the set), and the storage is zero-initialized so a never-written region
//     reads back as the mirror's zeros.
//   - Every transfer binds the resource on the currently active unit and restores that unit's
//     previous binding and the pixel-store defaults, instead of EasyGL's rebinding of unit 0.
// DXT blocks stay retained on the CPU exactly as EasyGL keeps them: they are the exact-block
// readback store, the only source of the decoded fallback when the driver has no S3TC, and what
// HasDefinedMipLevel reports on.

#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Resources.hpp"

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::OpenGL4
{
    using namespace CNA::Internal::Renderers::OpenGL4::GL4;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    namespace
    {
        [[nodiscard]] int FormatBytesPerTexel(const int surfaceFormat)
        {
            return Microsoft::Xna::Framework::Graphics::Texture::GetFormatSizeEXT(
                static_cast<SurfaceFormat>(surfaceFormat));
        }

        [[nodiscard]] GLenum CubeFaceTarget(const int face)
        {
            return static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face);
        }

        /// The texture object's own sampling defaults. A draw's sampler object overrides them;
        /// they are a defined starting state for a bind that happens before any sampler is applied.
        void SetOrdinaryTextureDefaults(const GLenum target)
        {
            glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }

        /**
         * Uploads declared-format texels into one 2D image of the texture bound to its target,
         * converting them to the transfer layout first. @p wholeLevel (re)specifies the level;
         * otherwise the rectangle is written into existing storage.
         */
        void UploadImage2D(const GLenum imageTarget, const int level,
                           const int x, const int y, const int width, const int height,
                           const int surfaceFormat, const Detail::TextureTransferFormat& transfer,
                           const void* pixels, const bool wholeLevel)
        {
            std::vector<std::uint8_t> scratch;
            const void* upload = Detail::ExpandTexelsForTransfer(
                surfaceFormat, pixels,
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height), scratch);
            Detail::ScopedUnpackState unpack(1);
            if (wholeLevel)
            {
                glTexImage2D(imageTarget, level, static_cast<GLint>(transfer.internalFormat),
                             width, height, 0, transfer.pixelFormat, transfer.pixelType, upload);
            }
            else
            {
                glTexSubImage2D(imageTarget, level, x, y, width, height,
                                transfer.pixelFormat, transfer.pixelType, upload);
            }
        }

        /**
         * Reads one whole image of the texture bound to its target back in the transfer layout it
         * was uploaded in.
         */
        [[nodiscard]] bool ReadImageLevel(const GLenum imageTarget, const int level,
                                          const Detail::TextureTransferFormat& transfer,
                                          const std::size_t texelCount,
                                          std::vector<std::uint8_t>& image)
        {
            image.resize(texelCount * static_cast<std::size_t>(transfer.transferBytesPerTexel));
            DrainGlErrors();
            Detail::ScopedPackState pack(1);
            glGetTexImage(imageTarget, level, transfer.pixelFormat, transfer.pixelType,
                          image.data());
            return GlOperationSucceeded();
        }
    }

    // --- OpenGL4TextureRenderer ------------------------------------------------------------------

    OpenGL4TextureRenderer::OpenGL4TextureRenderer(const ImageData& data)
        : width_(data.width), height_(data.height), surfaceFormat_(data.surfaceFormat),
          mipLevels_(data.mipLevels > 0 ? data.mipLevels : 1)
    {
        const bool dxt = Detail::IsDxtFormat(surfaceFormat_);
        if (dxt)
        {
            compressedLevels_.resize(static_cast<std::size_t>(mipLevels_));
            const std::size_t levelBytes = Detail::DxtImageBytes(surfaceFormat_, width_, height_);
            if (data.pixels.size() < levelBytes)
                throw std::invalid_argument("OpenGL4 DXT texture level 0 has too few block bytes");
            compressedLevels_[0].assign(data.pixels.begin(),
                                        data.pixels.begin() + static_cast<std::ptrdiff_t>(levelBytes));
            nativeBlocks_ = Detail::ContextHasS3tc();
        }

        glGenTextures(1, &texture_);
        try
        {
            UploadLevel(0, width_, height_,
                        dxt ? static_cast<const void*>(compressedLevels_[0].data())
                            : (data.pixels.empty() ? nullptr
                                                   : static_cast<const void*>(data.pixels.data())));
            AllocateDeclaredLevels();
            // Task 924: clamp GL_TEXTURE_MAX_LEVEL to the real level count -- GL's default of 1000
            // makes a mipmap-requiring filter (e.g. Anisotropic) treat even a single-level texture
            // as an incomplete chain and render solid black.
            Detail::ScopedTextureBinding binding(GL_TEXTURE_2D, texture_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipLevels_ - 1);
        }
        catch (...)
        {
            glDeleteTextures(1, &texture_);
            texture_ = 0;
            throw;
        }
    }

    OpenGL4TextureRenderer::~OpenGL4TextureRenderer()
    {
        if (texture_ != 0)
            glDeleteTextures(1, &texture_);
    }

    void OpenGL4TextureRenderer::UploadLevel(const int level, const int levelWidth,
                                             const int levelHeight, const void* pixels)
    {
        // REMED-GFX-244: block-compressed content. Where the driver has S3TC the blocks are stored
        // as they arrived; where it does not they are decoded to RGBA8 rather than refused, because
        // GraphicsProfile.Reach promises a game that Dxt1/3/5 work. The two paths differ in
        // memory, never in what is drawn.
        if (Detail::IsDxtFormat(surfaceFormat_))
        {
            const std::size_t imageBytes =
                Detail::DxtImageBytes(surfaceFormat_, levelWidth, levelHeight);
            const void* levelPixels = pixels;
            std::vector<std::uint8_t> uninitializedStorage;
            if (levelPixels == nullptr && level >= 0 &&
                level < static_cast<int>(compressedLevels_.size()))
            {
                const auto& stored = compressedLevels_[static_cast<std::size_t>(level)];
                if (!stored.empty())
                {
                    levelPixels = stored.data();
                }
                else
                {
                    // GL requires actual bytes for the compressed allocation. They stay transient,
                    // so HasDefinedMipLevel still means caller/content-authored data.
                    uninitializedStorage.assign(imageBytes, 0u);
                    levelPixels = uninitializedStorage.data();
                }
            }

            Detail::ScopedTextureBinding binding(GL_TEXTURE_2D, texture_);
            Detail::ScopedUnpackState unpack(1);
            if (levelPixels != nullptr && nativeBlocks_)
            {
                gl4_glCompressedTexImage2D(GL_TEXTURE_2D, level,
                                           Detail::DxtInternalFormat(surfaceFormat_),
                                           levelWidth, levelHeight, 0,
                                           static_cast<GLsizei>(imageBytes), levelPixels);
            }
            else
            {
                // Storage still has to exist when there are no pixels yet, so the decode path also
                // covers the null case with an empty RGBA8 level.
                std::vector<std::uint8_t> rgba;
                if (levelPixels != nullptr)
                {
                    rgba = Detail::DecodeDxtBlocks(
                        surfaceFormat_, static_cast<const std::uint8_t*>(levelPixels), imageBytes,
                        levelWidth, levelHeight);
                }
                glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, levelWidth, levelHeight, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, rgba.empty() ? nullptr : rgba.data());
            }
            SetOrdinaryTextureDefaults(GL_TEXTURE_2D);
            return;
        }

        // Every uncompressed format: REMED-GFX-244's packed 16-bit formats (alpha rotated into GL's
        // low bits), the signed-normalized bytes, Alpha8 and the float/half/16-bit-normalized
        // formats (widened to four channels with Direct3D 9's expansion), and Color.
        Detail::TextureTransferFormat transfer{};
        (void)Detail::MapTextureTransferFormat(surfaceFormat_, true, transfer);
        std::vector<std::uint8_t> scratch;
        const void* upload = Detail::ExpandTexelsForTransfer(
            surfaceFormat_, pixels,
            static_cast<std::size_t>(levelWidth) * static_cast<std::size_t>(levelHeight), scratch);
        Detail::ScopedTextureBinding binding(GL_TEXTURE_2D, texture_);
        Detail::ScopedUnpackState unpack(1);
        glTexImage2D(GL_TEXTURE_2D, level, static_cast<GLint>(transfer.internalFormat),
                     levelWidth, levelHeight, 0, transfer.pixelFormat, transfer.pixelType, upload);
        SetOrdinaryTextureDefaults(GL_TEXTURE_2D);
    }

    // REMED-GFX-175: a Texture2D created with mipMap=true DECLARES a chain, and Task 924 widens
    // GL_TEXTURE_MAX_LEVEL to match it. Every declared level is therefore given storage at creation
    // with no pixel data, or the texture is mipmap-incomplete until the game writes every level and
    // samples as opaque black under every filter with a mipmap term. This ALLOCATES the declared
    // levels; it does not GENERATE them.
    void OpenGL4TextureRenderer::AllocateDeclaredLevels()
    {
        for (int level = 1; level < mipLevels_; ++level)
        {
            const int levelWidth = std::max(1, width_ >> level);
            const int levelHeight = std::max(1, height_ >> level);
            UploadLevel(level, levelWidth, levelHeight, nullptr);
        }
    }

    void OpenGL4TextureRenderer::BindGL(const int unit) const
    {
        gl4_glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
        glBindTexture(GL_TEXTURE_2D, texture_);
    }

    void OpenGL4TextureRenderer::UpdatePixels(const uint8_t* data, int /*stride*/)
    {
        if (Detail::IsDxtFormat(surfaceFormat_))
        {
            if (data == nullptr)
                return;
            const std::size_t byteCount = Detail::DxtImageBytes(surfaceFormat_, width_, height_);
            if (compressedLevels_.empty())
                compressedLevels_.resize(static_cast<std::size_t>(mipLevels_));
            compressedLevels_[0].assign(data, data + byteCount);
            UploadLevel(0, width_, height_, compressedLevels_[0].data());
            return;
        }
        UploadLevel(0, width_, height_, data);
    }

    void OpenGL4TextureRenderer::UpdatePixelsLevel(const int level, const uint8_t* data,
                                                   const int levelW, const int levelH)
    {
        if (Detail::IsDxtFormat(surfaceFormat_) && level >= 0 && level < mipLevels_ &&
            data != nullptr)
        {
            const std::size_t byteCount = Detail::DxtImageBytes(surfaceFormat_, levelW, levelH);
            if (compressedLevels_.empty())
                compressedLevels_.resize(static_cast<std::size_t>(mipLevels_));
            auto& stored = compressedLevels_[static_cast<std::size_t>(level)];
            stored.assign(data, data + byteCount);
            UploadLevel(level, levelW, levelH, stored.data());
            return;
        }
        UploadLevel(level, levelW, levelH, data);
    }

    bool OpenGL4TextureRenderer::HasDefinedMipLevel(const int level) const noexcept
    {
        return level >= 0 && level < static_cast<int>(compressedLevels_.size()) &&
               !compressedLevels_[static_cast<std::size_t>(level)].empty();
    }

    bool OpenGL4TextureRenderer::GetData(const int level, const int x, const int y,
                                         const int w, const int h,
                                         void* data, const int dataLength) const
    {
        if (!Detail::IsDxtFormat(surfaceFormat_))
            return false;
        if (data == nullptr || level < 0 || level >= mipLevels_ || x < 0 || y < 0 || w <= 0 ||
            h <= 0)
            return false;

        const int levelWidth = std::max(1, width_ >> level);
        const int levelHeight = std::max(1, height_ >> level);
        if (w > levelWidth || h > levelHeight || x > levelWidth - w || y > levelHeight - h)
            return false;
        const bool touchesRightEdge = x + w == levelWidth;
        const bool touchesBottomEdge = y + h == levelHeight;
        if ((x % 4) != 0 || (y % 4) != 0 ||
            ((w % 4) != 0 && !touchesRightEdge) ||
            ((h % 4) != 0 && !touchesBottomEdge))
            return false;

        if (level >= static_cast<int>(compressedLevels_.size()))
            return false;
        const auto& source = compressedLevels_[static_cast<std::size_t>(level)];
        const std::size_t blockBytes = Detail::DxtBlockBytes(surfaceFormat_);
        const std::size_t fullBlockColumns = static_cast<std::size_t>((levelWidth + 3) / 4);
        const std::size_t fullBlockRows = static_cast<std::size_t>((levelHeight + 3) / 4);
        const std::size_t copyBlockColumns = static_cast<std::size_t>((w + 3) / 4);
        const std::size_t copyBlockRows = static_cast<std::size_t>((h + 3) / 4);
        const std::size_t required = copyBlockColumns * copyBlockRows * blockBytes;
        if (source.size() < fullBlockColumns * fullBlockRows * blockBytes || dataLength < 0 ||
            static_cast<std::size_t>(dataLength) < required)
            return false;

        const std::size_t blockX = static_cast<std::size_t>(x / 4);
        const std::size_t blockY = static_cast<std::size_t>(y / 4);
        const std::size_t copyBytes = copyBlockColumns * blockBytes;
        auto* destination = static_cast<std::uint8_t*>(data);
        for (std::size_t row = 0; row < copyBlockRows; ++row)
        {
            const std::size_t sourceOffset =
                ((blockY + row) * fullBlockColumns + blockX) * blockBytes;
            std::memcpy(destination + row * copyBytes, source.data() + sourceOffset, copyBytes);
        }
        return true;
    }

    void OpenGL4TextureRenderer::ShareCpuPixels(std::shared_ptr<std::vector<uint8_t>> /*pixels*/)
    {
    }

    // --- OpenGL4TextureCubeRenderer --------------------------------------------------------------

    OpenGL4TextureCubeRenderer::OpenGL4TextureCubeRenderer(const int size, const bool mipMap,
                                                           const int surfaceFormat)
        : size_(size), surfaceFormat_(surfaceFormat),
          levelCount_(mipMap ? Detail::CalculateMipLevels(size, size, 1) : 1)
    {
        const bool dxt = Detail::IsDxtFormat(surfaceFormat_);
        Detail::TextureTransferFormat transfer{};
        if (!dxt && !Detail::MapTextureTransferFormat(surfaceFormat_, false, transfer))
        {
            throw std::runtime_error("OpenGL4: unsupported uncompressed cube SurfaceFormat ordinal " +
                                     std::to_string(surfaceFormat_));
        }
        if (dxt)
        {
            compressedLevels_.resize(static_cast<std::size_t>(6 * levelCount_));
            nativeBlocks_ = Detail::ContextHasS3tc();
        }

        glGenTextures(1, &texture_);
        try
        {
            Detail::ScopedTextureBinding binding(GL_TEXTURE_CUBE_MAP, texture_);
            // Every face and level is given storage (not just level 0): SetData's writes use
            // glTexSubImage2D, which needs the level to exist (Task 276). The storage is written
            // with zeroed declared-format texels -- zero blocks for DXT -- so a region never
            // written reads back as zeros and samples as their expansion.
            const std::vector<std::uint8_t> zeroTexels(
                dxt ? 0u
                    : static_cast<std::size_t>(size_) * static_cast<std::size_t>(size_) *
                          static_cast<std::size_t>(FormatBytesPerTexel(surfaceFormat_)),
                0u);
            for (int face = 0; face < 6; ++face)
            {
                for (int level = 0; level < levelCount_; ++level)
                {
                    const int levelSize = std::max(1, size_ >> level);
                    if (!dxt)
                    {
                        UploadImage2D(CubeFaceTarget(face), level, 0, 0, levelSize, levelSize,
                                      surfaceFormat_, transfer, zeroTexels.data(), true);
                        continue;
                    }
                    auto& blocks = compressedLevels_[LevelIndex(face, level)];
                    blocks.assign(Detail::DxtImageBytes(surfaceFormat_, levelSize, levelSize), 0u);
                    Detail::ScopedUnpackState unpack(1);
                    if (nativeBlocks_)
                    {
                        gl4_glCompressedTexImage2D(CubeFaceTarget(face), level,
                                                   Detail::DxtInternalFormat(surfaceFormat_),
                                                   levelSize, levelSize, 0,
                                                   static_cast<GLsizei>(blocks.size()),
                                                   blocks.data());
                    }
                    else
                    {
                        const std::vector<std::uint8_t> rgba = Detail::DecodeDxtBlocks(
                            surfaceFormat_, blocks.data(), blocks.size(), levelSize, levelSize);
                        glTexImage2D(CubeFaceTarget(face), level, GL_RGBA8, levelSize, levelSize,
                                     0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
                    }
                }
            }
            // REMED-GFX-174: clamp the level range to the allocated chain -- a cube sampled through
            // EnvironmentMapEffect's sampler faces the same completeness rule as a Texture2D.
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, levelCount_ - 1);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        catch (...)
        {
            glDeleteTextures(1, &texture_);
            texture_ = 0;
            throw;
        }
    }

    OpenGL4TextureCubeRenderer::~OpenGL4TextureCubeRenderer()
    {
        if (texture_ != 0)
            glDeleteTextures(1, &texture_);
    }

    void OpenGL4TextureCubeRenderer::BindGL(const int unit) const
    {
        gl4_glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
        glBindTexture(GL_TEXTURE_CUBE_MAP, texture_);
    }

    void OpenGL4TextureCubeRenderer::ShareCpuPixels(
        int /*face*/, std::shared_ptr<std::vector<uint8_t>> /*pixels*/)
    {
    }

    bool OpenGL4TextureCubeRenderer::SetData(const int face, const int level, const int x,
                                             const int y, const int w, const int h,
                                             const void* data, const int dataLength)
    {
        if (static_cast<SurfaceFormat>(surfaceFormat_) != SurfaceFormat::Color)
            return false;
        return SetDataBytesEXT(face, level, x, y, w, h, data, dataLength);
    }

    bool OpenGL4TextureCubeRenderer::SetDataBytesEXT(const int face, const int level, const int x,
                                                     const int y, const int w, const int h,
                                                     const void* data, const int dataLength)
    {
        if (Detail::IsDxtFormat(surfaceFormat_))
            return false;
        // REMED-GFX-135: every refusal is a `false` the shared layer can tell apart from a
        // completed upload.
        if (face < 0 || face >= 6 || data == nullptr || w <= 0 || h <= 0)
            return false;
        if (level < 0 || level >= levelCount_)
            return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize)
            return false;
        const int bytesPerTexel = FormatBytesPerTexel(surfaceFormat_);
        if (dataLength < w * h * bytesPerTexel)
            return false;
        Detail::TextureTransferFormat transfer{};
        if (!Detail::MapTextureTransferFormat(surfaceFormat_, false, transfer))
            return false;

        DrainGlErrors();
        Detail::ScopedTextureBinding binding(GL_TEXTURE_CUBE_MAP, texture_);
        UploadImage2D(CubeFaceTarget(face), level, x, y, w, h, surfaceFormat_, transfer, data,
                      false);
        return GlOperationSucceeded();
    }

    bool OpenGL4TextureCubeRenderer::SetCompressedDataEXT(const int face, const int level,
                                                          const int x, const int y,
                                                          const int w, const int h,
                                                          const void* data, const int dataLength)
    {
        if (!Detail::IsDxtFormat(surfaceFormat_))
            return false;
        if (face < 0 || face >= 6 || data == nullptr || w <= 0 || h <= 0)
            return false;
        if (level < 0 || level >= levelCount_)
            return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize)
            return false;
        if ((x % 4) != 0 || (y % 4) != 0 ||
            ((w % 4) != 0 && x + w != levelSize) ||
            ((h % 4) != 0 && y + h != levelSize))
            return false;
        const std::size_t imageBytes = Detail::DxtImageBytes(surfaceFormat_, w, h);
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < imageBytes)
            return false;

        // The retained stream is replaced only once GL has accepted the upload.
        const int levelBlockColumns = (levelSize + 3) / 4;
        const int regionBlockColumns = (w + 3) / 4;
        const int regionBlockRows = (h + 3) / 4;
        const std::size_t blockBytes = Detail::DxtBlockBytes(surfaceFormat_);
        auto replacement = compressedLevels_[LevelIndex(face, level)];
        for (int row = 0; row < regionBlockRows; ++row)
        {
            const std::size_t destination =
                (static_cast<std::size_t>(y / 4 + row) * static_cast<std::size_t>(levelBlockColumns) +
                 static_cast<std::size_t>(x / 4)) * blockBytes;
            const std::size_t source =
                static_cast<std::size_t>(row * regionBlockColumns) * blockBytes;
            std::memcpy(replacement.data() + destination,
                        static_cast<const std::uint8_t*>(data) + source,
                        static_cast<std::size_t>(regionBlockColumns) * blockBytes);
        }

        DrainGlErrors();
        bool succeeded = false;
        {
            Detail::ScopedTextureBinding binding(GL_TEXTURE_CUBE_MAP, texture_);
            Detail::ScopedUnpackState unpack(1);
            if (nativeBlocks_)
            {
                gl4_glCompressedTexSubImage2D(CubeFaceTarget(face), level, x, y, w, h,
                                              Detail::DxtInternalFormat(surfaceFormat_),
                                              static_cast<GLsizei>(imageBytes), data);
            }
            else
            {
                const std::vector<std::uint8_t> rgba = Detail::DecodeDxtBlocks(
                    surfaceFormat_, static_cast<const std::uint8_t*>(data), imageBytes, w, h);
                glTexSubImage2D(CubeFaceTarget(face), level, x, y, w, h, GL_RGBA,
                                GL_UNSIGNED_BYTE, rgba.data());
            }
            succeeded = GlOperationSucceeded();
        }
        if (!succeeded)
            return false;
        compressedLevels_[LevelIndex(face, level)] = std::move(replacement);
        return true;
    }

    bool OpenGL4TextureCubeRenderer::GetCompressedDataEXT(const int face, const int level,
                                                          const int x, const int y,
                                                          const int w, const int h,
                                                          void* data, const int dataLength) const
    {
        if (!Detail::IsDxtFormat(surfaceFormat_) || face < 0 || face >= 6 || data == nullptr ||
            level < 0 || level >= levelCount_ || w <= 0 || h <= 0)
            return false;

        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || w > levelSize || h > levelSize ||
            x > levelSize - w || y > levelSize - h ||
            (x % 4) != 0 || (y % 4) != 0 ||
            ((w % 4) != 0 && x + w != levelSize) ||
            ((h % 4) != 0 && y + h != levelSize))
            return false;

        const std::size_t blockBytes = Detail::DxtBlockBytes(surfaceFormat_);
        const int levelBlockColumns = (levelSize + 3) / 4;
        const int regionBlockColumns = (w + 3) / 4;
        const int regionBlockRows = (h + 3) / 4;
        const std::size_t required = static_cast<std::size_t>(regionBlockColumns) *
                                     static_cast<std::size_t>(regionBlockRows) * blockBytes;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
            return false;

        const auto& blocks = compressedLevels_[LevelIndex(face, level)];
        auto* destination = static_cast<std::uint8_t*>(data);
        for (int row = 0; row < regionBlockRows; ++row)
        {
            const std::size_t sourceOffset =
                (static_cast<std::size_t>(y / 4 + row) * static_cast<std::size_t>(levelBlockColumns) +
                 static_cast<std::size_t>(x / 4)) * blockBytes;
            const std::size_t destinationOffset =
                static_cast<std::size_t>(row) * static_cast<std::size_t>(regionBlockColumns) *
                blockBytes;
            std::memcpy(destination + destinationOffset, blocks.data() + sourceOffset,
                        static_cast<std::size_t>(regionBlockColumns) * blockBytes);
        }
        return true;
    }

    bool OpenGL4TextureCubeRenderer::GetData(const int face, const int level, const int x,
                                             const int y, const int w, const int h,
                                             void* data, const int dataLength) const
    {
        // REMED-GFX-130: every refusal is a `false`, never a fabricated transparent-black face.
        if (face < 0 || face >= 6 || data == nullptr || level < 0 || w <= 0 || h <= 0)
            return false;
        if (dataLength < w * h * 4)
            return false;
        if (level >= levelCount_)
            return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize)
            return false;

        if (Detail::IsDxtFormat(surfaceFormat_))
        {
            // The public TextureCube readback receives Color elements: decode the retained blocks.
            const auto& blocks = compressedLevels_[LevelIndex(face, level)];
            const std::vector<std::uint8_t> rgba = Detail::DecodeDxtBlocks(
                surfaceFormat_, blocks.data(), blocks.size(), levelSize, levelSize);
            auto* destination = static_cast<std::uint8_t*>(data);
            for (int row = 0; row < h; ++row)
            {
                std::memcpy(destination + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4u,
                            rgba.data() + (static_cast<std::size_t>(y + row) *
                                               static_cast<std::size_t>(levelSize) +
                                           static_cast<std::size_t>(x)) * 4u,
                            static_cast<std::size_t>(w) * 4u);
            }
            return true;
        }

        if (static_cast<SurfaceFormat>(surfaceFormat_) != SurfaceFormat::Color)
            return false;
        return GetDataBytesEXT(face, level, x, y, w, h, data, dataLength);
    }

    bool OpenGL4TextureCubeRenderer::GetDataBytesEXT(const int face, const int level, const int x,
                                                     const int y, const int w, const int h,
                                                     void* data, const int dataLength) const
    {
        if (Detail::IsDxtFormat(surfaceFormat_) || face < 0 || face >= 6 || data == nullptr ||
            level < 0 || level >= levelCount_ || w <= 0 || h <= 0)
            return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || w > levelSize || h > levelSize ||
            x > levelSize - w || y > levelSize - h)
            return false;
        const int bytesPerTexel = FormatBytesPerTexel(surfaceFormat_);
        if (dataLength < w * h * bytesPerTexel)
            return false;
        Detail::TextureTransferFormat transfer{};
        if (!Detail::MapTextureTransferFormat(surfaceFormat_, false, transfer))
            return false;

        std::vector<std::uint8_t> image;
        {
            Detail::ScopedTextureBinding binding(GL_TEXTURE_CUBE_MAP, texture_);
            if (!ReadImageLevel(CubeFaceTarget(face), level, transfer,
                                static_cast<std::size_t>(levelSize) *
                                    static_cast<std::size_t>(levelSize),
                                image))
                return false;
        }

        auto* destination = static_cast<std::uint8_t*>(data);
        const std::size_t transferBytes = static_cast<std::size_t>(transfer.transferBytesPerTexel);
        const std::size_t rowBytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(bytesPerTexel);
        for (int row = 0; row < h; ++row)
        {
            const std::size_t sourceOffset =
                (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(levelSize) +
                 static_cast<std::size_t>(x)) * transferBytes;
            Detail::CollapseTransferTexels(surfaceFormat_, image.data() + sourceOffset,
                                           static_cast<std::size_t>(w),
                                           destination + static_cast<std::size_t>(row) * rowBytes);
        }
        return true;
    }

    // --- OpenGL4Texture3DRenderer ----------------------------------------------------------------

    OpenGL4Texture3DRenderer::OpenGL4Texture3DRenderer(const int w, const int h, const int depth,
                                                       const bool mipMap, const int surfaceFormat)
        : width_(w), height_(h), depth_(depth),
          levelCount_(mipMap ? Detail::CalculateMipLevels(w, h, depth) : 1),
          surfaceFormat_(surfaceFormat)
    {
        Detail::TextureTransferFormat transfer{};
        if (!Detail::MapTextureTransferFormat(surfaceFormat_, false, transfer))
        {
            throw std::runtime_error(
                "OpenGL4: unsupported uncompressed volume SurfaceFormat ordinal " +
                std::to_string(surfaceFormat_));
        }

        glGenTextures(1, &texture_);
        try
        {
            Detail::ScopedTextureBinding binding(GL_TEXTURE_3D, texture_);
            // Every level is given storage (not just level 0): SetData's box writes use
            // glTexSubImage3D, which needs the level to exist. The storage is zeroed so a box never
            // written reads back as zeros.
            const std::vector<std::uint8_t> zeroTransfer(
                static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) *
                    static_cast<std::size_t>(depth_) *
                    static_cast<std::size_t>(transfer.transferBytesPerTexel),
                0u);
            Detail::ScopedUnpackState unpack(1);
            for (int level = 0; level < levelCount_; ++level)
            {
                gl4_glTexImage3D(GL_TEXTURE_3D, level, static_cast<GLint>(transfer.internalFormat),
                                 std::max(1, width_ >> level), std::max(1, height_ >> level),
                                 std::max(1, depth_ >> level), 0, transfer.pixelFormat,
                                 transfer.pixelType, zeroTransfer.data());
            }
            // REMED-GFX-174: the level range is what makes the texture complete whatever filter
            // the sampler object bound to this unit turns out to have.
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, levelCount_ - 1);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        catch (...)
        {
            glDeleteTextures(1, &texture_);
            texture_ = 0;
            throw;
        }
    }

    OpenGL4Texture3DRenderer::~OpenGL4Texture3DRenderer()
    {
        if (texture_ != 0)
            glDeleteTextures(1, &texture_);
    }

    void OpenGL4Texture3DRenderer::BindGL(const int unit) const
    {
        gl4_glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
        glBindTexture(GL_TEXTURE_3D, texture_);
    }

    bool OpenGL4Texture3DRenderer::SetData(const int level, const int x, const int y, const int z,
                                           const int w, const int h, const int depth,
                                           const void* data, const int dataLength)
    {
        // The public Texture3D element path is Color-only; typed data arrives through
        // SetDataBytesEXT.
        if (static_cast<SurfaceFormat>(surfaceFormat_) != SurfaceFormat::Color)
            return false;
        return SetDataBytesEXT(level, x, y, z, w, h, depth, data, dataLength);
    }

    bool OpenGL4Texture3DRenderer::SetDataBytesEXT(const int level, const int x, const int y,
                                                   const int z, const int w, const int h,
                                                   const int depth, const void* data,
                                                   const int dataLength)
    {
        if (data == nullptr || w <= 0 || h <= 0 || depth <= 0)
            return false;
        if (level < 0 || level >= levelCount_)
            return false;
        const int levelWidth = std::max(1, width_ >> level);
        const int levelHeight = std::max(1, height_ >> level);
        const int levelDepth = std::max(1, depth_ >> level);
        if (x < 0 || y < 0 || z < 0 || x + w > levelWidth || y + h > levelHeight ||
            z + depth > levelDepth)
            return false;
        const int bytesPerTexel = FormatBytesPerTexel(surfaceFormat_);
        if (dataLength < w * h * depth * bytesPerTexel)
            return false;
        Detail::TextureTransferFormat transfer{};
        if (!Detail::MapTextureTransferFormat(surfaceFormat_, false, transfer))
            return false;

        std::vector<std::uint8_t> scratch;
        const void* upload = Detail::ExpandTexelsForTransfer(
            surfaceFormat_, data,
            static_cast<std::size_t>(w) * static_cast<std::size_t>(h) *
                static_cast<std::size_t>(depth),
            scratch);
        // REMED-GFX-135: glTexSubImage3D returns nothing, so the error queue is the only signal.
        DrainGlErrors();
        Detail::ScopedTextureBinding binding(GL_TEXTURE_3D, texture_);
        Detail::ScopedUnpackState unpack(1);
        gl4_glTexSubImage3D(GL_TEXTURE_3D, level, x, y, z, w, h, depth, transfer.pixelFormat,
                            transfer.pixelType, upload);
        return GlOperationSucceeded();
    }

    bool OpenGL4Texture3DRenderer::GetData(const int level, const int x, const int y, const int z,
                                           const int w, const int h, const int depth,
                                           void* data, const int dataLength) const
    {
        if (static_cast<SurfaceFormat>(surfaceFormat_) != SurfaceFormat::Color)
            return false;
        return GetDataBytesEXT(level, x, y, z, w, h, depth, data, dataLength);
    }

    bool OpenGL4Texture3DRenderer::GetDataBytesEXT(const int level, const int x, const int y,
                                                   const int z, const int w, const int h,
                                                   const int depth, void* data,
                                                   const int dataLength) const
    {
        if (data == nullptr || level < 0 || level >= levelCount_ || w <= 0 || h <= 0 || depth <= 0)
            return false;
        const int levelWidth = std::max(1, width_ >> level);
        const int levelHeight = std::max(1, height_ >> level);
        const int levelDepth = std::max(1, depth_ >> level);
        if (x < 0 || y < 0 || z < 0 || w > levelWidth || h > levelHeight || depth > levelDepth ||
            x > levelWidth - w || y > levelHeight - h || z > levelDepth - depth)
            return false;
        const int bytesPerTexel = FormatBytesPerTexel(surfaceFormat_);
        if (dataLength < w * h * depth * bytesPerTexel)
            return false;
        Detail::TextureTransferFormat transfer{};
        if (!Detail::MapTextureTransferFormat(surfaceFormat_, false, transfer))
            return false;

        std::vector<std::uint8_t> image;
        {
            Detail::ScopedTextureBinding binding(GL_TEXTURE_3D, texture_);
            if (!ReadImageLevel(GL_TEXTURE_3D, level, transfer,
                                static_cast<std::size_t>(levelWidth) *
                                    static_cast<std::size_t>(levelHeight) *
                                    static_cast<std::size_t>(levelDepth),
                                image))
                return false;
        }

        auto* destination = static_cast<std::uint8_t*>(data);
        const std::size_t transferBytes = static_cast<std::size_t>(transfer.transferBytesPerTexel);
        const std::size_t rowBytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(bytesPerTexel);
        const std::size_t sliceBytes = rowBytes * static_cast<std::size_t>(h);
        for (int slice = 0; slice < depth; ++slice)
        {
            for (int row = 0; row < h; ++row)
            {
                const std::size_t sourceOffset =
                    ((static_cast<std::size_t>(z + slice) * static_cast<std::size_t>(levelHeight) +
                      static_cast<std::size_t>(y + row)) * static_cast<std::size_t>(levelWidth) +
                     static_cast<std::size_t>(x)) * transferBytes;
                Detail::CollapseTransferTexels(
                    surfaceFormat_, image.data() + sourceOffset, static_cast<std::size_t>(w),
                    destination + static_cast<std::size_t>(slice) * sliceBytes +
                        static_cast<std::size_t>(row) * rowBytes);
            }
        }
        return true;
    }
}
