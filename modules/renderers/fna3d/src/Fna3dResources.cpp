// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Fna3d/Fna3dEnumMapping.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dRenderer.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dSurfaceFormats.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::Fna3d
{
    namespace
    {
        /** Mip dimension at @p level, floored at one texel, matching XNA's own chain. */
        int LevelExtent(int base, int level)
        {
            int extent = base;
            for (int i = 0; i < level; ++i)
            {
                extent /= 2;
            }
            return extent > 0 ? extent : 1;
        }

        /**
         * FNA3D_SupportsNoOverwrite reports whether the running driver can honour a no-overwrite
         * upload; FNA itself downgrades to the plain option when it cannot, and forwarding it
         * blindly would ask a driver for a guarantee it never made.
         */
        FNA3D_SetDataOptions ResolveSetDataOptions(FNA3D_Device* device, SetDataOptions options)
        {
            const FNA3D_SetDataOptions resolved = ToFna3dSetDataOptions(options);
            if (resolved == FNA3D_SETDATAOPTIONS_NOOVERWRITE &&
                FNA3D_SupportsNoOverwrite(device) == 0)
            {
                return FNA3D_SETDATAOPTIONS_NONE;
            }
            return resolved;
        }

        /**
         * FNA3D exposes no valid block-compressed volume/cube transfer route in CNA. Keep those
         * resources linear; each accepted format then uses its real unit size below.
         */
        void RequireLinearTransferFormatEXT(int surfaceFormat, const char* what)
        {
            if (IsBlockCompressedFormat(surfaceFormat))
            {
                throw std::runtime_error(
                    std::string("FNA3D renderer: ") + what +
                    " does not accept a block-compressed surface format -- CNA's renderer "
                    "contract has no compressed volume/cube transfer layout, so the compressed "
                    "byte layout could not be honoured.");
            }
        }

        /** Full mip chain length for a base extent, or 1 when mips were not requested. */
        int LevelCountFor(int extent, bool mipMap)
        {
            if (!mipMap)
            {
                return 1;
            }
            int levels = 1;
            while (extent > 1)
            {
                extent /= 2;
                ++levels;
            }
            return levels;
        }

        int VolumeByteCount(int surfaceFormat, int width, int height, int depth)
        {
            const int sliceBytes = FormatRegionByteCount(surfaceFormat, width, height);
            if (sliceBytes <= 0 || depth <= 0 ||
                depth > std::numeric_limits<int>::max() / sliceBytes)
            {
                return 0;
            }
            return sliceBytes * depth;
        }

        bool TryVolumeStorageSize(int width, int height, int depth, int unitBytes,
                                  std::size_t& voxelCount, std::size_t& byteCount)
        {
            if (width <= 0 || height <= 0 || depth <= 0 || unitBytes <= 0)
            {
                return false;
            }
            const std::size_t max = std::numeric_limits<std::size_t>::max();
            voxelCount = static_cast<std::size_t>(width);
            if (voxelCount > max / static_cast<std::size_t>(height))
            {
                return false;
            }
            voxelCount *= static_cast<std::size_t>(height);
            if (voxelCount > max / static_cast<std::size_t>(depth))
            {
                return false;
            }
            voxelCount *= static_cast<std::size_t>(depth);
            if (voxelCount > max / static_cast<std::size_t>(unitBytes))
            {
                return false;
            }
            byteCount = voxelCount * static_cast<std::size_t>(unitBytes);
            return true;
        }

        FNA3D_Device* LiveDevice(const std::shared_ptr<Fna3dDeviceState>& deviceState) noexcept
        {
            return deviceState != nullptr ? deviceState->device : nullptr;
        }

        int CheckedBufferByteCount(int elementCount, std::size_t elementBytes, const char* what)
        {
            if (elementCount <= 0 || elementBytes == 0)
            {
                return 0;
            }
            if (elementBytes > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
                static_cast<std::size_t>(elementCount) >
                    static_cast<std::size_t>(std::numeric_limits<int>::max()) / elementBytes)
            {
                throw std::runtime_error(
                    std::string("FNA3D renderer: ") + what +
                    " byte size exceeds FNA3D's signed 32-bit buffer limit.");
            }
            return elementCount * static_cast<int>(elementBytes);
        }
    }

    // ---------------------------------------------------------------------------------------
    // Fna3dTextureRenderer
    // ---------------------------------------------------------------------------------------

    Fna3dTextureRenderer::Fna3dTextureRenderer(
        std::shared_ptr<Fna3dDeviceState> deviceState, FNA3D_Texture* texture, int width,
        int height, int levelCount, int surfaceFormat, bool compressedReadback)
        : deviceState_(deviceState)
        , texture_(texture)
        , width_(width)
        , height_(height)
        , levelCount_(levelCount > 0 ? levelCount : 1)
        , surfaceFormat_(surfaceFormat)
        , compressedReadback_(compressedReadback)
    {
    }

    Fna3dTextureRenderer::~Fna3dTextureRenderer()
    {
        if (FNA3D_Device* device = LiveDevice(deviceState_); device != nullptr && texture_ != nullptr)
        {
            FNA3D_AddDisposeTexture(device, texture_);
        }
        texture_ = nullptr;
    }

    void Fna3dTextureRenderer::MarkLevelDefinedEXT(int level)
    {
        if (level >= 0 && level < 32)
        {
            definedLevels_ |= (1u << level);
        }
    }

    bool Fna3dTextureRenderer::HasDefinedMipLevel(int level) const noexcept
    {
        if (level < 0 || level >= 32)
        {
            return false;
        }
        return (definedLevels_ & (1u << level)) != 0;
    }

    void Fna3dTextureRenderer::UpdatePixels(const std::uint8_t* pixels, int stride)
    {
        if (pixels == nullptr || texture_ == nullptr)
        {
            return;
        }
        FNA3D_Device* device = deviceState_->RequireDevice("Texture2D upload");

        // `ImageData::surfaceFormat` is part of the renderer contract and may name a
        // block-compressed ordinal, whose raw blocks travel through this RGBA-named method
        // (SKIA-140/141's convention). Neither the row pitch nor the total byte count may then be
        // derived from `width * 4` -- doing so reads past the end of the source buffer.
        const int levelBytes = FormatRegionByteCount(surfaceFormat_, width_, height_);
        const int tightStride = FormatRowByteCount(surfaceFormat_, width_);
        const int rowCount = IsBlockCompressedFormat(surfaceFormat_)
                                 ? height_ / 4 + (height_ % 4 == 0 ? 0 : 1)
                                 : height_;
        if (levelBytes <= 0 || tightStride <= 0)
        {
            return;
        }
        if (stride > 0 && stride < tightStride)
        {
            // A source row narrower than one packed row cannot hold this level; reading it either
            // way would run off the end of the caller's buffer. Same silent-return convention the
            // null/empty guards above use.
            return;
        }

        if (stride == tightStride || stride <= 0)
        {
            FNA3D_SetTextureData2D(device, texture_, 0, 0, width_, height_, 0,
                                   const_cast<std::uint8_t*>(pixels), levelBytes);
        }
        else
        {
            std::vector<std::uint8_t> packed(static_cast<std::size_t>(levelBytes));
            for (int row = 0; row < rowCount; ++row)
            {
                std::memcpy(packed.data() + static_cast<std::size_t>(row) * tightStride,
                            pixels + static_cast<std::size_t>(row) * stride,
                            static_cast<std::size_t>(tightStride));
            }
            FNA3D_SetTextureData2D(device, texture_, 0, 0, width_, height_, 0, packed.data(),
                                   static_cast<int32_t>(packed.size()));
        }
        MarkLevelDefinedEXT(0);
    }

    void Fna3dTextureRenderer::UpdatePixelsLevel(int level, const std::uint8_t* pixels, int levelW,
                                                 int levelH)
    {
        if (pixels == nullptr || texture_ == nullptr || level < 0 || level >= levelCount_ ||
            levelW != LevelExtent(width_, level) || levelH != LevelExtent(height_, level))
        {
            return;
        }
        FNA3D_Device* device = deviceState_->RequireDevice("Texture2D mip upload");
        const int levelBytes = FormatRegionByteCount(surfaceFormat_, levelW, levelH);
        if (levelBytes <= 0)
        {
            return;
        }
        FNA3D_SetTextureData2D(device, texture_, 0, 0, levelW, levelH, level,
                               const_cast<std::uint8_t*>(pixels), levelBytes);
        MarkLevelDefinedEXT(level);
    }

    bool Fna3dTextureRenderer::GetData(int level, int x, int y, int w, int h, void* data,
                                       int dataLength) const
    {
        const int regionBytes = FormatRegionByteCount(surfaceFormat_, w, h);
        FNA3D_Device* device = LiveDevice(deviceState_);
        if (device == nullptr || texture_ == nullptr || data == nullptr || w <= 0 || h <= 0 || level < 0 ||
            level >= levelCount_ ||
            !IsValidTextureRegion2DForFormat(surfaceFormat_, LevelExtent(width_, level),
                                             LevelExtent(height_, level), x, y, w, h) ||
            regionBytes <= 0 || dataLength < regionBytes)
        {
            return false;
        }
        // FNA3D's OpenGL and Direct3D 11 drivers refuse a compressed GetTextureData2D and write
        // nothing; only its SDL_GPU driver forwards it. Reporting success there would hand the
        // caller whatever the destination already held, which is precisely what this method's
        // contract forbids, so a driver that cannot read compressed blocks answers "read nothing".
        if (!compressedReadback_ && IsBlockCompressedFormat(surfaceFormat_))
        {
            return false;
        }
        FNA3D_GetTextureData2D(device, texture_, x, y, w, h, level, data, regionBytes);
        return true;
    }

    // ---------------------------------------------------------------------------------------
    // Fna3dRenderTargetRenderer
    // ---------------------------------------------------------------------------------------

    Fna3dRenderTargetRenderer::Fna3dRenderTargetRenderer(
        std::shared_ptr<Fna3dDeviceState> deviceState, int width, int height, int depthFormat,
        bool preserveContents, bool mipMap, int multiSampleCount, int surfaceFormat)
        : deviceState_(deviceState)
        , width_(width > 0 ? width : 1)
        , height_(height > 0 ? height : 1)
        , depthFormat_(depthFormat)
        , surfaceFormat_(surfaceFormat)
        , preserveContents_(preserveContents)
    {
        FNA3D_Device* device = deviceState_->RequireDevice("RenderTarget2D creation");
        const FNA3D_SurfaceFormat format = ToFna3dSurfaceFormat(surfaceFormat_);
        levelCount_ = LevelCountFor(std::max(width_, height_), mipMap);

        texture_ = FNA3D_CreateTexture2D(device, format, width_, height_, levelCount_, 1);
        if (texture_ == nullptr)
        {
            throw std::runtime_error(
                "FNA3D renderer: FNA3D_CreateTexture2D failed for a render target.");
        }
        FNA3D_SetTextureName(device, texture_, "CNA RenderTarget2D");

        if (multiSampleCount > 1)
        {
            const int applied =
                FNA3D_GetMaxMultiSampleCount(device, format, multiSampleCount);
            if (applied > 1)
            {
                multiSampleCount_ = applied;
                colorBuffer_ = FNA3D_GenColorRenderbuffer(device, width_, height_, format,
                                                          multiSampleCount_, texture_);
                if (colorBuffer_ == nullptr)
                {
                    FNA3D_AddDisposeTexture(device, texture_);
                    texture_ = nullptr;
                    multiSampleCount_ = 0;
                    throw std::runtime_error(
                        "FNA3D renderer: MSAA color-renderbuffer creation failed for a "
                        "RenderTarget2D.");
                }
            }
        }

        if (depthFormat_ != 0)
        {
            depthBuffer_ = FNA3D_GenDepthStencilRenderbuffer(
                device, width_, height_, ToFna3dDepthFormat(depthFormat_), multiSampleCount_);
            if (depthBuffer_ == nullptr)
            {
                if (colorBuffer_ != nullptr)
                {
                    FNA3D_AddDisposeRenderbuffer(device, colorBuffer_);
                    colorBuffer_ = nullptr;
                }
                FNA3D_AddDisposeTexture(device, texture_);
                texture_ = nullptr;
                throw std::runtime_error(
                    "FNA3D renderer: depth/stencil renderbuffer creation failed for a "
                    "RenderTarget2D.");
            }
        }
    }

    Fna3dRenderTargetRenderer::~Fna3dRenderTargetRenderer()
    {
        FNA3D_Device* device = LiveDevice(deviceState_);
        if (device == nullptr)
        {
            return;
        }
        if (depthBuffer_ != nullptr)
        {
            FNA3D_AddDisposeRenderbuffer(device, depthBuffer_);
        }
        if (colorBuffer_ != nullptr)
        {
            FNA3D_AddDisposeRenderbuffer(device, colorBuffer_);
        }
        if (texture_ != nullptr)
        {
            FNA3D_AddDisposeTexture(device, texture_);
        }
        depthBuffer_ = nullptr;
        colorBuffer_ = nullptr;
        texture_ = nullptr;
    }

    void Fna3dRenderTargetRenderer::FillBindingEXT(FNA3D_RenderTargetBinding& binding) const
    {
        (void) deviceState_->RequireDevice("RenderTarget2D binding");
        std::memset(&binding, 0, sizeof(binding));
        binding.type = FNA3D_RENDERTARGET_TYPE_2D;
        binding.twod.width = width_;
        binding.twod.height = height_;
        binding.levelCount = levelCount_;
        binding.multiSampleCount = multiSampleCount_;
        binding.texture = texture_;
        binding.colorBuffer = colorBuffer_;
    }

    void Fna3dRenderTargetRenderer::BindAsRenderTarget()
    {
        deviceState_->RequireRenderer("RenderTarget2D binding").SetRenderTarget2D(this);
    }

    void Fna3dRenderTargetRenderer::UnbindAsRenderTarget()
    {
        deviceState_->RequireRenderer("RenderTarget2D unbinding").UnbindTargetsEXT();
    }

    int Fna3dRenderTargetRenderer::GetAppliedDepthStencilFormatEXT(
        int /*requestedDepthStencilFormat*/) const
    {
        // FNA3D allocates exactly the requested depth/stencil format for a render target, so the
        // applied ordinal is the requested one -- except when no depth buffer was created at all,
        // where reporting anything but DepthFormat::None would describe storage that is absent.
        return depthBuffer_ != nullptr ? depthFormat_ : 0;
    }

    bool Fna3dRenderTargetRenderer::HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const
    {
        return depthBuffer_ != nullptr;
    }

    bool Fna3dRenderTargetRenderer::HasRealStencilBuffer(bool /*stencilFormatWasRequested*/) const
    {
        // Only Depth24Stencil8 allocates a stencil plane; Depth16/Depth24 genuinely have none.
        return depthBuffer_ != nullptr && depthFormat_ == FNA3D_DEPTHFORMAT_D24S8;
    }

    bool Fna3dRenderTargetRenderer::GetData(int level, int x, int y, int w, int h, void* data,
                                            int dataLength) const
    {
        // A render target created through CreateRenderTarget2DEXT may carry any surface format,
        // so its readback is sized by that format rather than assumed to be RGBA8.
        const int regionBytes = FormatRegionByteCount(surfaceFormat_, w, h);
        FNA3D_Device* device = LiveDevice(deviceState_);
        if (device == nullptr || texture_ == nullptr || data == nullptr || w <= 0 || h <= 0 || level < 0 ||
            level >= levelCount_ ||
            !IsValidTextureRegion2D(LevelExtent(width_, level), LevelExtent(height_, level), x,
                                    y, w, h) ||
            regionBytes <= 0 || dataLength < regionBytes)
        {
            return false;
        }
        // A multisampled or mipped target only holds its final content in the destination texture
        // once FNA3D has resolved it; the renderer resolves on unbind, so reading a target that is
        // still bound would see stale content. Resolve-on-unbind plus this ordering is exactly
        // FNA's own contract for RenderTarget2D.GetData.
        FNA3D_GetTextureData2D(device, texture_, x, y, w, h, level, data, regionBytes);
        return true;
    }

    // ---------------------------------------------------------------------------------------
    // Fna3dTextureCubeRenderer
    // ---------------------------------------------------------------------------------------

    Fna3dTextureCubeRenderer::Fna3dTextureCubeRenderer(
        std::shared_ptr<Fna3dDeviceState> deviceState, FNA3D_Texture* texture, int size,
        int levelCount, int surfaceFormat)
        : deviceState_(deviceState)
        , texture_(texture)
        , size_(size)
        , levelCount_(levelCount > 0 ? levelCount : 1)
        , surfaceFormat_(surfaceFormat)
    {
    }

    Fna3dTextureCubeRenderer::~Fna3dTextureCubeRenderer()
    {
        if (FNA3D_Device* device = LiveDevice(deviceState_); device != nullptr && texture_ != nullptr)
        {
            FNA3D_AddDisposeTexture(device, texture_);
        }
        texture_ = nullptr;
    }

    bool Fna3dTextureCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                           const void* data, int dataLength)
    {
        const int regionBytes = FormatRegionByteCount(surfaceFormat_, w, h);
        FNA3D_Device* device = LiveDevice(deviceState_);
        if (device == nullptr || texture_ == nullptr || data == nullptr || face < 0 || face > 5 || level < 0 ||
            level >= levelCount_ ||
            !IsValidTextureRegion2D(LevelExtent(size_, level), LevelExtent(size_, level), x, y,
                                    w, h) ||
            regionBytes <= 0 || dataLength < regionBytes)
        {
            return false;
        }
        FNA3D_SetTextureDataCube(device, texture_, x, y, w, h,
                                 static_cast<FNA3D_CubeMapFace>(face), level,
                                 const_cast<void*>(data), regionBytes);
        return true;
    }

    bool Fna3dTextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const
    {
        const int regionBytes = FormatRegionByteCount(surfaceFormat_, w, h);
        FNA3D_Device* device = LiveDevice(deviceState_);
        if (device == nullptr || texture_ == nullptr || data == nullptr || face < 0 || face > 5 || level < 0 ||
            level >= levelCount_ ||
            !IsValidTextureRegion2D(LevelExtent(size_, level), LevelExtent(size_, level), x, y,
                                    w, h) ||
            regionBytes <= 0 || dataLength < regionBytes)
        {
            return false;
        }
        FNA3D_GetTextureDataCube(device, texture_, x, y, w, h,
                                 static_cast<FNA3D_CubeMapFace>(face), level, data, regionBytes);
        return true;
    }

    // ---------------------------------------------------------------------------------------
    // Fna3dRenderTargetCubeRenderer
    // ---------------------------------------------------------------------------------------

    Fna3dRenderTargetCubeRenderer::Fna3dRenderTargetCubeRenderer(
        std::shared_ptr<Fna3dDeviceState> deviceState, int size, int depthFormat,
        bool preserveContents, bool mipMap, int multiSampleCount)
        : deviceState_(deviceState)
        , size_(size > 0 ? size : 1)
        , depthFormat_(depthFormat)
        , preserveContents_(preserveContents)
    {
        FNA3D_Device* device = deviceState_->RequireDevice("RenderTargetCube creation");
        levelCount_ = LevelCountFor(size_, mipMap);
        texture_ = FNA3D_CreateTextureCube(device, FNA3D_SURFACEFORMAT_COLOR, size_, levelCount_,
                                           1);
        if (texture_ == nullptr)
        {
            throw std::runtime_error(
                "FNA3D renderer: FNA3D_CreateTextureCube failed for a render target.");
        }
        FNA3D_SetTextureName(device, texture_, "CNA RenderTargetCube");

        if (multiSampleCount > 1)
        {
            const int applied = FNA3D_GetMaxMultiSampleCount(device, FNA3D_SURFACEFORMAT_COLOR,
                                                             multiSampleCount);
            if (applied > 1)
            {
                multiSampleCount_ = applied;
                colorBuffer_ = FNA3D_GenColorRenderbuffer(
                    device, size_, size_, FNA3D_SURFACEFORMAT_COLOR, multiSampleCount_, texture_);
                if (colorBuffer_ == nullptr)
                {
                    FNA3D_AddDisposeTexture(device, texture_);
                    texture_ = nullptr;
                    multiSampleCount_ = 0;
                    throw std::runtime_error(
                        "FNA3D renderer: MSAA color-renderbuffer creation failed for a "
                        "RenderTargetCube.");
                }
            }
        }

        if (depthFormat_ != 0)
        {
            depthBuffer_ = FNA3D_GenDepthStencilRenderbuffer(
                device, size_, size_, ToFna3dDepthFormat(depthFormat_), multiSampleCount_);
            if (depthBuffer_ == nullptr)
            {
                if (colorBuffer_ != nullptr)
                {
                    FNA3D_AddDisposeRenderbuffer(device, colorBuffer_);
                    colorBuffer_ = nullptr;
                }
                FNA3D_AddDisposeTexture(device, texture_);
                texture_ = nullptr;
                throw std::runtime_error(
                    "FNA3D renderer: depth/stencil renderbuffer creation failed for a "
                    "RenderTargetCube.");
            }
        }
    }

    Fna3dRenderTargetCubeRenderer::~Fna3dRenderTargetCubeRenderer()
    {
        FNA3D_Device* device = LiveDevice(deviceState_);
        if (device == nullptr)
        {
            return;
        }
        if (depthBuffer_ != nullptr)
        {
            FNA3D_AddDisposeRenderbuffer(device, depthBuffer_);
        }
        if (colorBuffer_ != nullptr)
        {
            FNA3D_AddDisposeRenderbuffer(device, colorBuffer_);
        }
        if (texture_ != nullptr)
        {
            FNA3D_AddDisposeTexture(device, texture_);
        }
        depthBuffer_ = nullptr;
        colorBuffer_ = nullptr;
        texture_ = nullptr;
    }

    void Fna3dRenderTargetCubeRenderer::FillBindingEXT(FNA3D_RenderTargetBinding& binding,
                                                       int face) const
    {
        (void) deviceState_->RequireDevice("RenderTargetCube binding");
        std::memset(&binding, 0, sizeof(binding));
        binding.type = FNA3D_RENDERTARGET_TYPE_CUBE;
        binding.cube.size = size_;
        if (face < 0 || face > 5)
        {
            throw std::runtime_error(
                "FNA3D renderer: render-target cube face must be in the range [0, 5].");
        }
        binding.cube.face = static_cast<FNA3D_CubeMapFace>(face);
        binding.levelCount = levelCount_;
        binding.multiSampleCount = multiSampleCount_;
        binding.texture = texture_;
        binding.colorBuffer = colorBuffer_;
    }

    void Fna3dRenderTargetCubeRenderer::BindAsRenderTargetFace(int face)
    {
        boundFace_ = face;
        Fna3dRenderer& renderer =
            deviceState_->RequireRenderer("RenderTargetCube face binding");
        const RenderTargetBindingDescriptor descriptor =
            RenderTargetBindingDescriptor::ForRenderTargetCubeFace(this, face, size_,
                                                                   multiSampleCount_);
        renderer.SetRenderTargets(&descriptor, 1);
    }

    void Fna3dRenderTargetCubeRenderer::UnbindAsRenderTarget()
    {
        deviceState_->RequireRenderer("RenderTargetCube unbinding").UnbindTargetsEXT();
    }

    bool Fna3dRenderTargetCubeRenderer::HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const
    {
        return depthBuffer_ != nullptr;
    }

    bool Fna3dRenderTargetCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                                void* data, int dataLength) const
    {
        constexpr int kColorFormat = static_cast<int>(FNA3D_SURFACEFORMAT_COLOR);
        const int regionBytes = FormatRegionByteCount(kColorFormat, w, h);
        FNA3D_Device* device = LiveDevice(deviceState_);
        if (device == nullptr || texture_ == nullptr || data == nullptr || face < 0 || face > 5 || level < 0 ||
            level >= levelCount_ ||
            !IsValidTextureRegion2D(LevelExtent(size_, level), LevelExtent(size_, level), x, y,
                                    w, h) ||
            regionBytes <= 0 || dataLength < regionBytes)
        {
            return false;
        }
        FNA3D_GetTextureDataCube(device, texture_, x, y, w, h,
                                 static_cast<FNA3D_CubeMapFace>(face), level, data, regionBytes);
        return true;
    }

    // ---------------------------------------------------------------------------------------
    // Fna3dTexture3DRenderer
    // ---------------------------------------------------------------------------------------

    Fna3dTexture3DRenderer::Fna3dTexture3DRenderer(
        std::shared_ptr<Fna3dDeviceState> deviceState, FNA3D_Texture* texture, int width,
        int height, int depth, int levelCount, int surfaceFormat, bool keepUploadMirror)
        : deviceState_(deviceState)
        , texture_(texture)
        , width_(width)
        , height_(height)
        , depth_(depth)
        , levelCount_(levelCount > 0 ? levelCount : 1)
        , surfaceFormat_(surfaceFormat)
        , keepUploadMirror_(keepUploadMirror)
    {
        if (keepUploadMirror_)
        {
            uploadMirror_.resize(static_cast<std::size_t>(levelCount_));
            uploadMirrorDefined_.resize(static_cast<std::size_t>(levelCount_));
        }
    }

    Fna3dTexture3DRenderer::~Fna3dTexture3DRenderer()
    {
        if (FNA3D_Device* device = LiveDevice(deviceState_); device != nullptr && texture_ != nullptr)
        {
            FNA3D_AddDisposeTexture(device, texture_);
        }
        texture_ = nullptr;
    }

    bool Fna3dTexture3DRenderer::SetData(int level, int x, int y, int z, int w, int h, int depth,
                                         const void* data, int dataLength)
    {
        const bool validLevel = level >= 0 && level < levelCount_;
        const int levelWidth = validLevel ? LevelExtent(width_, level) : 0;
        const int levelHeight = validLevel ? LevelExtent(height_, level) : 0;
        const int levelDepth = validLevel ? LevelExtent(depth_, level) : 0;
        const int transferBytes = VolumeByteCount(surfaceFormat_, w, h, depth);
        FNA3D_Device* device = LiveDevice(deviceState_);
        if (device == nullptr || texture_ == nullptr || data == nullptr || !validLevel ||
            !IsValidTextureRegion3D(levelWidth, levelHeight, levelDepth, x, y, z, w, h, depth) ||
            transferBytes <= 0 || dataLength < transferBytes)
        {
            return false;
        }

        const int unitBytes = FormatUnitByteCount(surfaceFormat_);
        std::size_t voxelCount = 0;
        std::size_t mirrorByteCount = 0;
        if (keepUploadMirror_ &&
            !TryVolumeStorageSize(levelWidth, levelHeight, levelDepth, unitBytes, voxelCount,
                                  mirrorByteCount))
        {
            return false;
        }
        if (keepUploadMirror_)
        {
            std::vector<std::uint8_t>& mirror = uploadMirror_[static_cast<std::size_t>(level)];
            std::vector<std::uint8_t>& defined =
                uploadMirrorDefined_[static_cast<std::size_t>(level)];
            mirror.resize(mirrorByteCount);
            defined.resize(voxelCount);

            const auto* source = static_cast<const std::uint8_t*>(data);
            for (int slice = 0; slice < depth; ++slice)
            {
                for (int row = 0; row < h; ++row)
                {
                    const std::size_t destinationOffset =
                        ((static_cast<std::size_t>(z + slice) * levelHeight +
                          static_cast<std::size_t>(y + row)) * levelWidth +
                         static_cast<std::size_t>(x)) * unitBytes;
                    const std::size_t sourceOffset =
                        ((static_cast<std::size_t>(slice) * h + static_cast<std::size_t>(row)) *
                         static_cast<std::size_t>(w)) * unitBytes;
                    std::memcpy(mirror.data() + destinationOffset, source + sourceOffset,
                                static_cast<std::size_t>(w) * unitBytes);

                    const std::size_t firstVoxel =
                        (static_cast<std::size_t>(z + slice) * levelHeight +
                         static_cast<std::size_t>(y + row)) * levelWidth +
                        static_cast<std::size_t>(x);
                    std::memset(defined.data() + firstVoxel, 1,
                                static_cast<std::size_t>(w));
                }
            }
        }
        // Allocate and update the exact fallback mirror first. If allocation fails, no native
        // upload has happened and the CPU/GPU copies cannot silently diverge.
        FNA3D_SetTextureData3D(device, texture_, x, y, z, w, h, depth, level,
                               const_cast<void*>(data), transferBytes);
        return true;
    }

    bool Fna3dTexture3DRenderer::GetData(int level, int x, int y, int z, int w, int h, int depth,
                                         void* data, int dataLength) const
    {
        const bool validLevel = level >= 0 && level < levelCount_;
        const int levelWidth = validLevel ? LevelExtent(width_, level) : 0;
        const int levelHeight = validLevel ? LevelExtent(height_, level) : 0;
        const int levelDepth = validLevel ? LevelExtent(depth_, level) : 0;
        const int transferBytes = VolumeByteCount(surfaceFormat_, w, h, depth);
        FNA3D_Device* device = LiveDevice(deviceState_);
        if (device == nullptr || texture_ == nullptr || data == nullptr || !validLevel ||
            !IsValidTextureRegion3D(levelWidth, levelHeight, levelDepth, x, y, z, w, h, depth) ||
            transferBytes <= 0 || dataLength < transferBytes)
        {
            return false;
        }
        if (!keepUploadMirror_)
        {
            FNA3D_GetTextureData3D(device, texture_, x, y, z, w, h, depth, level, data,
                                   transferBytes);
            return true;
        }

        // The running driver implements no volume readback, so the answer comes from the exact
        // bytes the caller uploaded. A level never written has nothing to answer with, and saying
        // so is the whole point of this method's bool -- the shared layer turns it into a
        // System::NotSupportedException rather than a buffer of invented zeroes.
        const std::vector<std::uint8_t>& mirror = uploadMirror_[static_cast<std::size_t>(level)];
        const std::vector<std::uint8_t>& defined =
            uploadMirrorDefined_[static_cast<std::size_t>(level)];
        if (mirror.empty() || defined.empty())
        {
            return false;
        }

        const int unitBytes = FormatUnitByteCount(surfaceFormat_);
        auto* destination = static_cast<std::uint8_t*>(data);
        for (int slice = 0; slice < depth; ++slice)
        {
            for (int row = 0; row < h; ++row)
            {
                const std::size_t firstVoxel =
                    ((static_cast<std::size_t>(z + slice) * levelHeight +
                      static_cast<std::size_t>(y + row)) * levelWidth +
                     static_cast<std::size_t>(x));
                const std::uint8_t* coverage = defined.data() + firstVoxel;
                if (std::find(coverage, coverage + w, std::uint8_t { 0 }) != coverage + w)
                {
                    return false;
                }
                const std::size_t sourceOffset = firstVoxel * unitBytes;
                const std::size_t destinationOffset =
                    ((static_cast<std::size_t>(slice) * h + static_cast<std::size_t>(row)) *
                     static_cast<std::size_t>(w)) * unitBytes;
                std::memcpy(destination + destinationOffset, mirror.data() + sourceOffset,
                            static_cast<std::size_t>(w) * unitBytes);
            }
        }
        return true;
    }

    void Fna3dTexture3DRenderer::GetDimensionsEXT(int& width, int& height, int& depth) const noexcept
    {
        width = width_;
        height = height_;
        depth = depth_;
    }

    // ---------------------------------------------------------------------------------------
    // Fna3dVertexBufferRenderer
    // ---------------------------------------------------------------------------------------

    Fna3dVertexBufferRenderer::Fna3dVertexBufferRenderer(
        std::shared_ptr<Fna3dDeviceState> deviceState, int vertexCapacity, int maxStride)
        : deviceState_(deviceState)
    {
        const int capacity = CheckedBufferByteCount(vertexCapacity > 0 ? vertexCapacity : 1,
                                                    maxStride > 0 ? maxStride : 4,
                                                    "vertex-buffer allocation");
        EnsureCapacity(capacity);
    }

    Fna3dVertexBufferRenderer::~Fna3dVertexBufferRenderer()
    {
        if (FNA3D_Device* device = LiveDevice(deviceState_); device != nullptr && buffer_ != nullptr)
        {
            FNA3D_AddDisposeVertexBuffer(device, buffer_);
        }
        buffer_ = nullptr;
    }

    void Fna3dVertexBufferRenderer::EnsureCapacity(int byteCount)
    {
        FNA3D_Device* device = deviceState_->RequireDevice("vertex-buffer allocation");
        if (byteCount <= capacityBytes_ && buffer_ != nullptr)
        {
            return;
        }
        if (buffer_ != nullptr)
        {
            FNA3D_AddDisposeVertexBuffer(device, buffer_);
            buffer_ = nullptr;
        }
        capacityBytes_ = byteCount > 0 ? byteCount : 4;
        buffer_ = FNA3D_GenVertexBuffer(device, 1, FNA3D_BUFFERUSAGE_WRITEONLY, capacityBytes_);
        if (buffer_ == nullptr)
        {
            capacityBytes_ = 0;
            throw std::runtime_error("FNA3D renderer: FNA3D_GenVertexBuffer failed.");
        }
    }

    void Fna3dVertexBufferRenderer::SetData(const void* data, int vertex_count,
                                            std::size_t stride_in_bytes)
    {
        SetDataWithOptions(data, vertex_count, stride_in_bytes, SetDataOptions::Discard);
    }

    void Fna3dVertexBufferRenderer::SetDataWithOptions(const void* data, int vertex_count,
                                                       std::size_t stride_in_bytes,
                                                       SetDataOptions options)
    {
        if (data == nullptr || vertex_count <= 0 || stride_in_bytes == 0)
        {
            vertexCount_ = vertex_count > 0 ? vertex_count : 0;
            return;
        }

        const int byteCount =
            CheckedBufferByteCount(vertex_count, stride_in_bytes, "vertex-buffer upload");
        const bool reallocated = byteCount > capacityBytes_ || buffer_ == nullptr;
        EnsureCapacity(byteCount);

        // A freshly allocated buffer has no previous content to preserve or alias, so NoOverwrite
        // would be a lie about GPU state; Discard is the only honest hint for a first write.
        const FNA3D_SetDataOptions resolved =
            reallocated ? FNA3D_SETDATAOPTIONS_DISCARD
                        : ResolveSetDataOptions(
                              deviceState_->RequireDevice("vertex-buffer upload"), options);

        FNA3D_SetVertexBufferData(deviceState_->RequireDevice("vertex-buffer upload"), buffer_, 0,
                                  const_cast<void*>(data), vertex_count,
                                  static_cast<int32_t>(stride_in_bytes),
                                  static_cast<int32_t>(stride_in_bytes), resolved);
        vertexCount_ = vertex_count;
        stride_ = static_cast<int>(stride_in_bytes);
    }

    void Fna3dVertexBufferRenderer::SetVertexDeclaration(const VertexDeclaration& vertexDeclaration)
    {
        (void) deviceState_->RequireDevice("vertex-declaration update");
        // FNA3D binds a genuine per-stream declaration rather than deriving a layout from a byte
        // stride, so the caller's own elements are kept verbatim and used at draw time. This is
        // the whole reason this renderer can accept custom vertex layouts that a stride-keyed
        // renderer would have to reject.
        declarationElements_.clear();
        const std::vector<VertexElement>& elements = vertexDeclaration.GetVertexElements();
        declarationElements_.reserve(elements.size());
        for (const VertexElement& element : elements)
        {
            FNA3D_VertexElement converted{};
            converted.offset = element.getOffsetProperty();
            converted.vertexElementFormat =
                ToFna3dVertexElementFormat(element.getVertexElementFormatProperty());
            converted.vertexElementUsage =
                ToFna3dVertexElementUsage(element.getVertexElementUsageProperty());
            converted.usageIndex = element.getUsageIndexProperty();
            declarationElements_.push_back(converted);
        }
        declarationStride_ = vertexDeclaration.getVertexStrideProperty();
        hasDeclaration_ = !declarationElements_.empty() && declarationStride_ > 0;
    }

    // ---------------------------------------------------------------------------------------
    // Fna3dIndexBufferRenderer
    // ---------------------------------------------------------------------------------------

    Fna3dIndexBufferRenderer::Fna3dIndexBufferRenderer(
        std::shared_ptr<Fna3dDeviceState> deviceState, int indexCapacity, bool thirtyTwoBit)
        : deviceState_(deviceState)
        , thirtyTwoBit_(thirtyTwoBit)
    {
        EnsureCapacity(CheckedBufferByteCount(indexCapacity > 0 ? indexCapacity : 1,
                                              thirtyTwoBit ? 4 : 2,
                                              "index-buffer allocation"));
    }

    Fna3dIndexBufferRenderer::~Fna3dIndexBufferRenderer()
    {
        if (FNA3D_Device* device = LiveDevice(deviceState_); device != nullptr && buffer_ != nullptr)
        {
            FNA3D_AddDisposeIndexBuffer(device, buffer_);
        }
        buffer_ = nullptr;
    }

    void Fna3dIndexBufferRenderer::EnsureCapacity(int byteCount)
    {
        FNA3D_Device* device = deviceState_->RequireDevice("index-buffer allocation");
        if (byteCount <= capacityBytes_ && buffer_ != nullptr)
        {
            return;
        }
        if (buffer_ != nullptr)
        {
            FNA3D_AddDisposeIndexBuffer(device, buffer_);
            buffer_ = nullptr;
        }
        capacityBytes_ = byteCount > 0 ? byteCount : 4;
        buffer_ = FNA3D_GenIndexBuffer(device, 1, FNA3D_BUFFERUSAGE_WRITEONLY, capacityBytes_);
        if (buffer_ == nullptr)
        {
            capacityBytes_ = 0;
            throw std::runtime_error("FNA3D renderer: FNA3D_GenIndexBuffer failed.");
        }
    }

    void Fna3dIndexBufferRenderer::Upload(const void* data, int indexCount, int elementBytes,
                                          bool thirtyTwoBit, FNA3D_SetDataOptions options)
    {
        if (data == nullptr || indexCount <= 0)
        {
            indexCount_ = indexCount > 0 ? indexCount : 0;
            return;
        }
        const int byteCount =
            CheckedBufferByteCount(indexCount, static_cast<std::size_t>(elementBytes),
                                   "index-buffer upload");
        const bool reallocated = byteCount > capacityBytes_ || buffer_ == nullptr ||
                                 thirtyTwoBit != thirtyTwoBit_;
        thirtyTwoBit_ = thirtyTwoBit;
        EnsureCapacity(byteCount);
        FNA3D_SetIndexBufferData(deviceState_->RequireDevice("index-buffer upload"), buffer_, 0,
                                 const_cast<void*>(data), byteCount,
                                 reallocated ? FNA3D_SETDATAOPTIONS_DISCARD : options);
        indexCount_ = indexCount;
    }

    void Fna3dIndexBufferRenderer::SetData16(const void* data, int index_count)
    {
        Upload(data, index_count, 2, false, FNA3D_SETDATAOPTIONS_DISCARD);
    }

    void Fna3dIndexBufferRenderer::SetData32(const void* data, int index_count)
    {
        Upload(data, index_count, 4, true, FNA3D_SETDATAOPTIONS_DISCARD);
    }

    void Fna3dIndexBufferRenderer::SetData16WithOptions(const void* data, int index_count,
                                                        SetDataOptions options)
    {
        Upload(data, index_count, 2, false,
               ResolveSetDataOptions(deviceState_->RequireDevice("index-buffer upload"), options));
    }

    void Fna3dIndexBufferRenderer::SetData32WithOptions(const void* data, int index_count,
                                                        SetDataOptions options)
    {
        Upload(data, index_count, 4, true,
               ResolveSetDataOptions(deviceState_->RequireDevice("index-buffer upload"), options));
    }

    // ---------------------------------------------------------------------------------------
    // Fna3dOcclusionQueryRenderer
    // ---------------------------------------------------------------------------------------

    Fna3dOcclusionQueryRenderer::Fna3dOcclusionQueryRenderer(
        std::shared_ptr<Fna3dDeviceState> deviceState)
        : deviceState_(deviceState)
    {
        query_ = FNA3D_CreateQuery(deviceState_->RequireDevice("occlusion-query creation"));
        if (query_ == nullptr)
        {
            throw std::runtime_error("FNA3D renderer: FNA3D_CreateQuery failed.");
        }
    }

    Fna3dOcclusionQueryRenderer::~Fna3dOcclusionQueryRenderer()
    {
        if (FNA3D_Device* device = LiveDevice(deviceState_); device != nullptr && query_ != nullptr)
        {
            FNA3D_AddDisposeQuery(device, query_);
        }
        query_ = nullptr;
    }

    void Fna3dOcclusionQueryRenderer::Begin()
    {
        FNA3D_QueryBegin(deviceState_->RequireDevice("occlusion-query Begin"), query_);
    }

    void Fna3dOcclusionQueryRenderer::End()
    {
        FNA3D_QueryEnd(deviceState_->RequireDevice("occlusion-query End"), query_);
    }

    bool Fna3dOcclusionQueryRenderer::IsComplete() const
    {
        return FNA3D_QueryComplete(deviceState_->RequireDevice("occlusion-query completion check"),
                                   query_) != 0;
    }

    int Fna3dOcclusionQueryRenderer::PixelCount() const
    {
        return FNA3D_QueryPixelCount(deviceState_->RequireDevice("occlusion-query result"), query_);
    }

    // ---------------------------------------------------------------------------------------
    // Fna3dRenderer resource factories and render-target binding
    // ---------------------------------------------------------------------------------------

    std::unique_ptr<ITextureRenderer> Fna3dRenderer::CreateTexture(const ImageData& data)
    {
        const int width = data.width > 0 ? data.width : 1;
        const int height = data.height > 0 ? data.height : 1;
        const int levelCount = data.mipLevels > 0 ? data.mipLevels : 1;
        const FNA3D_SurfaceFormat format = ToFna3dSurfaceFormat(data.surfaceFormat);
        RequireTextureFormatSupportedEXT(data.surfaceFormat, "Texture2D");
        if (!data.pixels.empty())
        {
            const int expectedBytes = FormatRegionByteCount(data.surfaceFormat, width, height);
            if (expectedBytes <= 0 || data.pixels.size() < static_cast<std::size_t>(expectedBytes))
            {
                throw std::runtime_error(
                    "FNA3D renderer: ImageData level-zero pixels do not cover its declared "
                    "surface format and dimensions.");
            }
        }

        FNA3D_Texture* texture =
            FNA3D_CreateTexture2D(device_, format, width, height, levelCount, 0);
        if (texture == nullptr)
        {
            throw std::runtime_error("FNA3D renderer: FNA3D_CreateTexture2D failed.");
        }

        // FNA3D forwards this to the native API's own debug-object naming (glObjectLabel,
        // ID3D11DeviceChild::SetPrivateData, SDL_GPU's texture name), so a RenderDoc/apitrace
        // capture of a CNA frame shows recognisable resources instead of anonymous handles.
        FNA3D_SetTextureName(device_, texture, "CNA Texture2D");

        std::unique_ptr<Fna3dTextureRenderer> handle;
        try
        {
            handle = std::make_unique<Fna3dTextureRenderer>(
                deviceState_, texture, width, height, levelCount, data.surfaceFormat,
                compressedReadbackSupported_);
        }
        catch (...)
        {
            FNA3D_AddDisposeTexture(device_, texture);
            throw;
        }
        if (!data.pixels.empty())
        {
            // `ImageData::pixels` is always tightly packed, and for a block-compressed ordinal a
            // tight row is one block row -- not `width * 4`, which would send UpdatePixels down its
            // repack branch and read past the end of the vector.
            handle->UpdatePixels(data.pixels.data(),
                                 FormatRowByteCount(data.surfaceFormat, width));
        }
        return handle;
    }

    std::unique_ptr<IOcclusionQueryRenderer> Fna3dRenderer::CreateOcclusionQuery()
    {
        return std::make_unique<Fna3dOcclusionQueryRenderer>(deviceState_);
    }

    std::unique_ptr<ITexture3DRenderer> Fna3dRenderer::CreateTexture3D(int w, int h, int depth,
                                                                       bool mipMap,
                                                                       int surfaceFormat)
    {
        const int width = w > 0 ? w : 1;
        const int height = h > 0 ? h : 1;
        const int slices = depth > 0 ? depth : 1;
        // FNA3D receives a byte length for every volume upload, but CNA has no compressed
        // volume-transfer layout. Refusing compressed input is the honest answer rather than
        // mis-sizing its blocks as linear texels.
        RequireLinearTransferFormatEXT(surfaceFormat, "Texture3D");
        const int levelCount = LevelCountFor(std::max({ width, height, slices }), mipMap);
        FNA3D_Texture* texture = FNA3D_CreateTexture3D(
            device_, ToFna3dSurfaceFormat(surfaceFormat), width, height, slices, levelCount);
        if (texture == nullptr)
        {
            return nullptr;
        }
        try
        {
            return std::make_unique<Fna3dTexture3DRenderer>(
                deviceState_, texture, width, height, slices, levelCount, surfaceFormat,
                !texture3DReadbackSupported_);
        }
        catch (...)
        {
            FNA3D_AddDisposeTexture(device_, texture);
            throw;
        }
    }

    std::unique_ptr<ITextureCubeRenderer> Fna3dRenderer::CreateTextureCube(int size, bool mipMap,
                                                                           int surfaceFormat)
    {
        RequireLinearTransferFormatEXT(surfaceFormat, "TextureCube");
        const int edge = size > 0 ? size : 1;
        const int levelCount = LevelCountFor(edge, mipMap);
        FNA3D_Texture* texture = FNA3D_CreateTextureCube(
            device_, ToFna3dSurfaceFormat(surfaceFormat), edge, levelCount, 0);
        if (texture == nullptr)
        {
            return nullptr;
        }
        try
        {
            return std::make_unique<Fna3dTextureCubeRenderer>(deviceState_, texture, edge,
                                                              levelCount, surfaceFormat);
        }
        catch (...)
        {
            FNA3D_AddDisposeTexture(device_, texture);
            throw;
        }
    }

    std::unique_ptr<IRenderTargetRenderer> Fna3dRenderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        return CreateRenderTarget2DEXT(w, h, depthFormat, preserveContents, mipMap,
                                       multiSampleCount, 0);
    }

    std::unique_ptr<IRenderTargetRenderer> Fna3dRenderer::CreateRenderTarget2DEXT(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount,
        int surfaceFormat)
    {
        RequireRenderTargetFormatSupportedEXT(surfaceFormat);
        return std::make_unique<Fna3dRenderTargetRenderer>(
            deviceState_, w, h, depthFormat, preserveContents, mipMap, multiSampleCount,
            surfaceFormat);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> Fna3dRenderer::CreateRenderTargetCube(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<Fna3dRenderTargetCubeRenderer>(
            deviceState_, size, depthFormat, preserveContents, mipMap, multiSampleCount);
    }

    std::unique_ptr<IVertexBufferRenderer> Fna3dRenderer::CreateVertexBuffer(int vertex_capacity)
    {
        // 68 bytes is the widest vertex any CNA stock layout uses (position/normal/tangent/
        // texcoord/blend weights/blend indices); the buffer grows on demand for anything wider.
        return std::make_unique<Fna3dVertexBufferRenderer>(deviceState_, vertex_capacity, 68);
    }

    std::unique_ptr<IIndexBufferRenderer> Fna3dRenderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<Fna3dIndexBufferRenderer>(deviceState_, index_capacity, false);
    }

    std::unique_ptr<IIndexBufferRenderer> Fna3dRenderer::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<Fna3dIndexBufferRenderer>(deviceState_, index_capacity, true);
    }

    void Fna3dRenderer::RebindCurrentTargetsEXT()
    {
        if (boundTargets_.empty())
        {
            FNA3D_SetRenderTargets(device_, nullptr, 0, nullptr, FNA3D_DEPTHFORMAT_NONE, 0);
            return;
        }

        std::vector<FNA3D_RenderTargetBinding> bindings;
        bindings.reserve(boundTargets_.size());
        for (const BoundTarget& target : boundTargets_)
        {
            bindings.push_back(target.binding);
        }
        FNA3D_SetRenderTargets(device_, bindings.data(), static_cast<int32_t>(bindings.size()),
                               boundDepthBuffer_, boundDepthFormat_,
                               boundPreserveContents_ ? 1 : 0);
    }

    void Fna3dRenderer::UnbindTargetsEXT()
    {
        if (boundTargets_.empty())
        {
            return;
        }

        // FNA3D requires ResolveTarget after unbinding a multisampled or mipped target: that is
        // where the MSAA resolve into the destination texture and the mip-chain generation happen.
        std::vector<BoundTarget> previous;
        previous.swap(boundTargets_);
        boundDepthBuffer_ = nullptr;
        boundDepthFormat_ = FNA3D_DEPTHFORMAT_NONE;
        boundPreserveContents_ = false;

        FNA3D_SetRenderTargets(device_, nullptr, 0, nullptr, FNA3D_DEPTHFORMAT_NONE, 0);

        for (BoundTarget& target : previous)
        {
            if (target.needsResolve)
            {
                FNA3D_ResolveTarget(device_, &target.binding);
            }
        }
    }

    void Fna3dRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (rt == nullptr)
        {
            UnbindTargetsEXT();
            return;
        }
        auto* target = dynamic_cast<Fna3dRenderTargetRenderer*>(rt);
        if (target == nullptr || target->GetFna3dDeviceStateEXT() != deviceState_)
        {
            throw std::runtime_error(
                "FNA3D renderer: RenderTarget2D was not created by this graphics device.");
        }
        const RenderTargetBindingDescriptor descriptor =
            RenderTargetBindingDescriptor::ForRenderTarget2D(rt, 0, target->GetWidth(),
                                                             target->GetHeight(),
                                                             target->GetMultiSampleCount());
        SetRenderTargets(&descriptor, 1);
    }

    void Fna3dRenderer::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                                         int count)
    {
        if (renderTargets == nullptr || count <= 0)
        {
            UnbindTargetsEXT();
            return;
        }

        constexpr int kMaxRenderTargets = 4;
        if (count > kMaxRenderTargets)
        {
            throw std::runtime_error(
                "FNA3D renderer: SetRenderTargets received " + std::to_string(count) +
                " targets, but XNA and FNA3D expose at most four simultaneous color targets.");
        }

        // Validate the whole set before disturbing the currently bound targets. Apart from
        // avoiding type-confused static_casts, the shared token check distinguishes resources
        // created by another live FNA3D device.
        for (int i = 0; i < count; ++i)
        {
            const RenderTargetBindingDescriptor& descriptor = renderTargets[i];
            if (descriptor.IsRenderTarget2D())
            {
                const auto* target =
                    dynamic_cast<const Fna3dRenderTargetRenderer*>(descriptor.GetRenderTarget2D());
                if (target == nullptr || target->GetFna3dDeviceStateEXT() != deviceState_)
                {
                    throw std::runtime_error(
                        "FNA3D renderer: a RenderTarget2D in SetRenderTargets was not created by "
                        "this graphics device.");
                }
                if (descriptor.GetArraySlice() != 0)
                {
                    throw std::runtime_error(
                        "FNA3D renderer: render-target array slices are not supported; CNA render "
                        "targets expose no texture arrays.");
                }
            }
            else if (descriptor.IsRenderTargetCubeFace())
            {
                const auto* target = dynamic_cast<const Fna3dRenderTargetCubeRenderer*>(
                    descriptor.GetRenderTargetCube());
                if (target == nullptr || target->GetFna3dDeviceStateEXT() != deviceState_)
                {
                    throw std::runtime_error(
                        "FNA3D renderer: a RenderTargetCube in SetRenderTargets was not created by "
                        "this graphics device.");
                }
                if (descriptor.GetCubeFace() < 0 || descriptor.GetCubeFace() > 5)
                {
                    throw std::runtime_error(
                        "FNA3D renderer: render-target cube face must be in the range [0, 5].");
                }
            }
            else
            {
                throw std::runtime_error(
                    "FNA3D renderer: SetRenderTargets received an unknown render-target "
                    "descriptor type; a cube face is never flattened to a RenderTarget2D.");
            }
        }

        UnbindTargetsEXT();

        std::vector<FNA3D_RenderTargetBinding> bindings;
        bindings.reserve(static_cast<std::size_t>(count));
        boundTargets_.clear();
        boundTargets_.reserve(static_cast<std::size_t>(count));

        FNA3D_Renderbuffer* depthBuffer = nullptr;
        FNA3D_DepthFormat depthFormat = FNA3D_DEPTHFORMAT_NONE;
        bool preserveContents = false;

        for (int i = 0; i < count; ++i)
        {
            const RenderTargetBindingDescriptor& descriptor = renderTargets[i];
            BoundTarget bound;

            if (descriptor.IsRenderTarget2D())
            {
                auto* target =
                    dynamic_cast<Fna3dRenderTargetRenderer*>(descriptor.GetRenderTarget2D());
                target->FillBindingEXT(bound.binding);
                if (i == 0)
                {
                    depthBuffer = target->GetDepthBufferEXT();
                    depthFormat = ToFna3dDepthFormat(target->GetDepthFormatEXT());
                    preserveContents = target->GetPreserveContentsEXT();
                }
            }
            else if (descriptor.IsRenderTargetCubeFace())
            {
                auto* target = dynamic_cast<Fna3dRenderTargetCubeRenderer*>(
                    descriptor.GetRenderTargetCube());
                target->FillBindingEXT(bound.binding, descriptor.GetCubeFace());
                if (i == 0)
                {
                    depthBuffer = target->GetDepthBufferEXT();
                    depthFormat = ToFna3dDepthFormat(target->GetDepthFormatEXT());
                    preserveContents = target->GetPreserveContentsEXT();
                }
            }
            else { /* preflight above made this branch unreachable */ }

            bound.needsResolve =
                bound.binding.levelCount > 1 || bound.binding.multiSampleCount > 1;
            bindings.push_back(bound.binding);
            boundTargets_.push_back(bound);
        }

        boundDepthBuffer_ = depthBuffer;
        boundDepthFormat_ = depthFormat;
        boundPreserveContents_ = preserveContents;

        FNA3D_SetRenderTargets(device_, bindings.data(), static_cast<int32_t>(bindings.size()),
                               depthBuffer, depthFormat, preserveContents ? 1 : 0);
    }
}
