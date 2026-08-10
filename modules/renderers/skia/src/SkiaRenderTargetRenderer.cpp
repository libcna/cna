#include "CNA/Internal/Renderers/Skia/SkiaRenderTargetRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaMipGeneration.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaResourcePolicy.hpp"

#include "System/NotSupportedException.hpp"

#include "include/core/SkColorType.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace CNA::Internal::Renderers::Skia
{
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    namespace
    {
        struct RenderTargetFormatInfo final
        {
            SkColorType colorType;
            SkAlphaType alphaType;
            std::size_t bytesPerTexel; // public XNA GetData/SetData byte size for one texel
            bool extractSubset; // true only for Single/Vector2: surface is wider (kRGBA_F32)
        };

        // SKIA-142: only the formats FNA itself reports renderable are promoted here (see
        // docs/skia-surface-format-matrix.md's "FNA/Skia RT decision" column and
        // RenderTarget2D.cpp's IsRenderableSkiaFormatEXT, which is the actual pre-allocation
        // gate -- this function must never be reachable with a format that gate refused).
        // Every entry except Single/Vector2 maps to a Skia SkColorType whose native byte layout
        // already equals the public XNA payload exactly (verified against
        // Texture::GetFormatSizeEXT); Single/Vector2 have no native 1/2-channel 32-bit-float
        // colour type, so they use kRGBA_F32 with only R (or R,G) meaningful.
        [[nodiscard]] RenderTargetFormatInfo ResolveRenderTargetFormat(SurfaceFormat format)
        {
            switch (format)
            {
                case SurfaceFormat::Color:
                    return {kRGBA_8888_SkColorType, kPremul_SkAlphaType, 4u, false};
                case SurfaceFormat::Rgba1010102:
                    return {kRGBA_1010102_SkColorType, kPremul_SkAlphaType, 4u, false};
                case SurfaceFormat::Rg32:
                    return {kR16G16_unorm_SkColorType, kOpaque_SkAlphaType, 4u, false};
                case SurfaceFormat::Rgba64:
                    return {kR16G16B16A16_unorm_SkColorType, kPremul_SkAlphaType, 8u, false};
                case SurfaceFormat::Vector4:
                    return {kRGBA_F32_SkColorType, kPremul_SkAlphaType, 16u, false};
                case SurfaceFormat::Single:
                    return {kRGBA_F32_SkColorType, kOpaque_SkAlphaType, 4u, true};
                case SurfaceFormat::Vector2:
                    return {kRGBA_F32_SkColorType, kOpaque_SkAlphaType, 8u, true};
                case SurfaceFormat::HalfSingle:
                    return {kR16_float_SkColorType, kOpaque_SkAlphaType, 2u, false};
                case SurfaceFormat::HalfVector2:
                    return {kR16G16_float_SkColorType, kOpaque_SkAlphaType, 4u, false};
                case SurfaceFormat::HalfVector4:
                case SurfaceFormat::HdrBlendable:
                    return {kRGBA_F16_SkColorType, kPremul_SkAlphaType, 8u, false};
                case SurfaceFormat::ColorSrgbEXT:
                    return {kSRGBA_8888_SkColorType, kPremul_SkAlphaType, 4u, false};
                case SurfaceFormat::ByteEXT:
                    return {kR8_unorm_SkColorType, kOpaque_SkAlphaType, 1u, false};
                case SurfaceFormat::UShortEXT:
                    return {kR16_unorm_SkColorType, kOpaque_SkAlphaType, 2u, false};
                default:
                    throw System::NotSupportedException(
                        "Skia RenderTarget2D has no native representation for this SurfaceFormat.");
            }
        }

        // Single/Vector2 only: expand the narrow public R(-G) float(s) into a full straight
        // kRGBA_F32 texel (missing G/B exactly 0, A exactly 1.0f) for writing to the native
        // surface. `publicBytesPerTexel` is 4 for Single (R only) or 8 for Vector2 (R,G).
        [[nodiscard]] std::vector<std::uint8_t> ExpandToRgbaF32(
            const std::uint8_t* publicBytes, int width, int height,
            std::size_t publicBytesPerTexel)
        {
            std::vector<std::uint8_t> native(static_cast<std::size_t>(width) * height * 16u, 0u);
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const std::uint8_t* input = publicBytes
                        + (static_cast<std::size_t>(y) * width + x) * publicBytesPerTexel;
                    std::uint8_t* output = native.data()
                        + (static_cast<std::size_t>(y) * width + x) * 16u;
                    std::memcpy(output, input, publicBytesPerTexel);
                    WriteU32Le(output + 12u, 0x3F800000u); // A = 1.0f; G/B stay zero-initialized
                }
            }
            return native;
        }

        // Single/Vector2 only: the reverse of ExpandToRgbaF32 -- keeps only R (or R,G).
        void ExtractFromRgbaF32(const std::uint8_t* nativeBytes, std::uint8_t* publicBytesOut,
                                int width, int height, std::size_t publicBytesPerTexel)
        {
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const std::uint8_t* input = nativeBytes
                        + (static_cast<std::size_t>(y) * width + x) * 16u;
                    std::uint8_t* output = publicBytesOut
                        + (static_cast<std::size_t>(y) * width + x) * publicBytesPerTexel;
                    std::memcpy(output, input, publicBytesPerTexel);
                }
            }
        }
    }

    SkiaRenderTargetRenderer::SkiaRenderTargetRenderer(
        int width, int height, bool preserveContents,
        std::weak_ptr<SkiaRenderTargetBinding> binding,
        std::shared_ptr<SkiaResourceCounters> resourceCounters, bool mipMap, SurfaceFormat format)
        : format_(format)
        , preserveContents_(preserveContents)
        , binding_(std::move(binding))
        , resourceCounters_(std::move(resourceCounters))
    {
        const RenderTargetFormatInfo info = ResolveRenderTargetFormat(format);
        bytesPerTexel_ = info.bytesPerTexel;

        std::vector<SkiaMipLevel2D> layout;
        SkiaMipChain2DLayoutError layoutError = SkiaMipChain2DLayoutError::None;
        std::size_t chainBytes = 0u;
        if (!TryBuildSkiaMipChain2DLayout(
                width, height, mipMap, bytesPerTexel_, layout, chainBytes, layoutError))
        {
            throw System::NotSupportedException(
                "Skia RenderTarget2D cannot allocate the requested checked mip chain.");
        }

        // Every level owns both a native SkSurface and an exact public-format transfer shadow.
        // Preflight their complete retained size before either representation allocates. When the
        // native colour type's byte layout matches the public format exactly (every promoted
        // format except Single/Vector2), the native chain is exactly `chainBytes` and this second
        // layout pass is redundant but harmless; Single/Vector2 widen to kRGBA_F32 (16 bytes/texel)
        // at the surface only, so their true native total must be computed separately.
        const std::size_t nativeBytesPerPixel =
            static_cast<std::size_t>(SkColorTypeBytesPerPixel(info.colorType));
        std::size_t surfaceChainBytes = chainBytes;
        if (nativeBytesPerPixel != bytesPerTexel_)
        {
            std::vector<SkiaMipLevel2D> nativeLayout;
            SkiaMipChain2DLayoutError nativeError = SkiaMipChain2DLayoutError::None;
            if (!TryBuildSkiaMipChain2DLayout(width, height, mipMap, nativeBytesPerPixel,
                                              nativeLayout, surfaceChainBytes, nativeError))
            {
                throw System::NotSupportedException(
                    "Skia RenderTarget2D cannot allocate the requested checked native surface "
                    "chain.");
            }
        }
        std::size_t retainedBytes = 0u;
        if (!CheckedSizeAdd(chainBytes, surfaceChainBytes, retainedBytes)
            || retainedBytes > kSkiaCpuTextureStorageLimitBytes)
        {
            throw System::NotSupportedException(
                "Skia RenderTarget2D surfaces and exact mip shadows exceed the checked "
                "256 MiB per-resource limit.");
        }

        mipChain_ = std::make_unique<SkiaMipChain2D>(
            width, height, mipMap, bytesPerTexel_, resourceCounters_);
        dirtyMipLevels_.assign(static_cast<std::size_t>(mipChain_->LevelCount()), false);
        mipGenerationCounts_.assign(static_cast<std::size_t>(mipChain_->LevelCount()), 0u);
        surfaceStorageBytes_ = surfaceChainBytes;
        surfaces_.reserve(layout.size());
        for (const SkiaMipLevel2D& level : layout)
        {
            auto surface = std::make_unique<SkiaSurface>(
                level.width, level.height, info.colorType, info.alphaType);
            surface->Clear(0.0f, 0.0f, 0.0f, 0.0f);
            surfaces_.push_back(std::move(surface));
        }
        if (resourceCounters_)
        {
            resourceCounters_->AddRenderTarget(surfaceStorageBytes_);
            resourceRegistered_ = true;
        }
    }

    SkiaRenderTargetRenderer::~SkiaRenderTargetRenderer()
    {
        // `RenderTarget2D::Dispose()` rejects an explicitly disposed bound target, but a C++
        // destructor cannot throw. Detach here before the level surfaces are released so the next
        // Skia clear or SpriteBatch draw never observes a dangling active-surface pointer. The weak
        // binding intentionally expires when the graphics renderer has already been destroyed.
        if (const auto binding = binding_.lock())
            binding->Detach(this);
        InvalidateSnapshot();
        if (resourceRegistered_)
            resourceCounters_->RemoveRenderTarget(surfaceStorageBytes_);
    }

    void SkiaRenderTargetRenderer::UpdatePixels(const std::uint8_t* rgba, int stride)
    {
        const int minStride = GetWidth() * static_cast<int>(bytesPerTexel_);
        if (!rgba || stride < minStride)
            throw std::runtime_error("Skia RenderTarget2D received an invalid level-0 upload.");
        const bool extractSubset = ResolveRenderTargetFormat(format_).extractSubset;
        if (extractSubset)
        {
            // Public rows may be padded (stride > minStride); repack tight before expanding.
            std::vector<std::uint8_t> tight(
                static_cast<std::size_t>(GetWidth()) * GetHeight() * bytesPerTexel_);
            for (int row = 0; row < GetHeight(); ++row)
            {
                std::memcpy(tight.data() + static_cast<std::size_t>(row) * GetWidth() * bytesPerTexel_,
                            rgba + static_cast<std::size_t>(row) * stride,
                            static_cast<std::size_t>(GetWidth()) * bytesPerTexel_);
            }
            const std::vector<std::uint8_t> native =
                ExpandToRgbaF32(tight.data(), GetWidth(), GetHeight(), bytesPerTexel_);
            if (!Surface().WritePixels(0, 0, GetWidth(), GetHeight(), native.data(),
                                       GetWidth() * 16))
            {
                throw std::runtime_error("Skia RenderTarget2D failed to replace level-0 pixels.");
            }
            std::memcpy(mipChain_->LevelData(0), tight.data(), tight.size());
        }
        else
        {
            if (!Surface().WritePixels(0, 0, GetWidth(), GetHeight(), rgba, stride))
                throw std::runtime_error("Skia RenderTarget2D failed to replace level-0 pixels.");
            for (int row = 0; row < GetHeight(); ++row)
            {
                std::memcpy(
                    mipChain_->LevelData(0) + static_cast<std::size_t>(row) * GetWidth() * bytesPerTexel_,
                    rgba + static_cast<std::size_t>(row) * stride,
                    static_cast<std::size_t>(GetWidth()) * bytesPerTexel_);
            }
        }
        levelZeroDirty_ = false;
        InvalidateDescendants(0);
        InvalidateSnapshot();
        GenerateDirtyMipLevels();
    }

    void SkiaRenderTargetRenderer::UpdatePixelsLevel(
        int level, const std::uint8_t* rgba, int levelWidth, int levelHeight)
    {
        if (level == 0)
        {
            if (levelWidth != GetWidth() || levelHeight != GetHeight())
            {
                throw std::runtime_error(
                    "Skia RenderTarget2D level-zero upload dimensions do not match the target.");
            }
            UpdatePixels(rgba, levelWidth * static_cast<int>(bytesPerTexel_));
            return;
        }
        SkiaSurface* surface = LevelSurface(level);
        if (!surface)
            throw std::out_of_range("Skia RenderTarget2D mip upload level is outside the chain.");
        if (!rgba)
            throw std::runtime_error("Skia RenderTarget2D mip upload received null data.");
        const SkiaMipLevel2D& target = mipChain_->Level(level);
        if (levelWidth != target.width || levelHeight != target.height)
        {
            throw std::runtime_error(
                "Skia RenderTarget2D mip upload dimensions do not match the requested level.");
        }

        // Order a higher-level upload after any preceding level-zero canvas work. Otherwise the
        // eventual pass resolve would silently overwrite a SetData that happened later in time.
        FinalizeWriteEXT();
        const bool extractSubset = ResolveRenderTargetFormat(format_).extractSubset;
        if (extractSubset)
        {
            const std::vector<std::uint8_t> native =
                ExpandToRgbaF32(rgba, target.width, target.height, bytesPerTexel_);
            if (!surface->WritePixels(0, 0, target.width, target.height, native.data(),
                                      target.width * 16))
            {
                throw std::runtime_error("Skia RenderTarget2D failed to replace mip pixels.");
            }
        }
        else if (!surface->WritePixels(0, 0, target.width, target.height, rgba,
                                       target.width * static_cast<int>(bytesPerTexel_)))
        {
            throw std::runtime_error("Skia RenderTarget2D failed to replace mip pixels.");
        }
        std::memcpy(mipChain_->LevelData(level), rgba, target.bytes);
        dirtyMipLevels_[static_cast<std::size_t>(level)] = false;
        InvalidateDescendants(level);
        InvalidateSnapshot();
        GenerateDirtyMipLevels();
    }

    bool SkiaRenderTargetRenderer::GetData(int level, int x, int y, int width, int height,
                                          void* data, int dataLength) const
    {
        std::size_t requiredBytes = 0;
        const SkiaSurface* surface = LevelSurface(level);
        if (!surface || data == nullptr || width <= 0 || height <= 0
            || x < 0 || y < 0 || width > surface->Width() || height > surface->Height()
            || x > surface->Width() - width || y > surface->Height() - height
            || !CheckedTexelBytes2D(static_cast<std::size_t>(width),
                                    static_cast<std::size_t>(height), bytesPerTexel_, requiredBytes)
            || dataLength < 0 || static_cast<std::size_t>(dataLength) < requiredBytes)
        {
            return false;
        }
        // Any valid target readback is a deterministic resolve barrier. This mirrors sampling and
        // unbinding, so observing level zero first cannot leave descendants stale for a later read.
        const_cast<SkiaRenderTargetRenderer*>(this)->FinalizeWriteEXT();

        const SkiaMipLevel2D& source = mipChain_->Level(level);
        auto* destination = static_cast<std::uint8_t*>(data);
        for (int row = 0; row < height; ++row)
        {
            const std::size_t sourceOffset =
                (static_cast<std::size_t>(y + row) * source.width + x) * bytesPerTexel_;
            std::memcpy(destination + static_cast<std::size_t>(row) * width * bytesPerTexel_,
                        mipChain_->LevelData(level) + sourceOffset,
                        static_cast<std::size_t>(width) * bytesPerTexel_);
        }
        return true;
    }

    void SkiaRenderTargetRenderer::BindAsRenderTarget()
    {
        // IGraphicsRenderer::SetRenderTarget2D owns the active-surface switch. This method exists
        // solely for the common renderer interface; it must not select an unrelated global target.
    }

    void SkiaRenderTargetRenderer::UnbindAsRenderTarget()
    {
        // See BindAsRenderTarget(). Raster surfaces require no resolve or deferred submission.
    }

    sk_sp<SkImage> SkiaRenderTargetRenderer::SnapshotImage(
        SkiaSourceAlphaConvention alphaConvention) const
    {
        return SnapshotMipLevelEXT(0, alphaConvention);
    }

    sk_sp<SkImage> SkiaRenderTargetRenderer::SnapshotMipLevelEXT(
        int level, SkiaSourceAlphaConvention alphaConvention) const
    {
        if (ResolveSkiaWorkingSourceRoute(StorageAlphaEXT(), alphaConvention)
            != SkiaWorkingSourceRoute::ReusePremultipliedSurface)
        {
            return nullptr;
        }
        const SkiaSurface* surface = LevelSurface(level);
        if (!surface)
            return nullptr;
        const_cast<SkiaRenderTargetRenderer*>(this)->FinalizeWriteEXT();
        // Keep exactly one immutable target snapshot across the complete chain. Mip-linear draws
        // retain the first returned image locally while this cache switches to the adjacent one,
        // so the renderer cannot grow one retained cache entry per level.
        if (!snapshot_ || snapshotLevel_ != level)
        {
            const_cast<SkiaRenderTargetRenderer*>(this)->InvalidateSnapshot();
            snapshot_ = surface->SnapshotImage();
            snapshotLevel_ = snapshot_ ? level : -1;
            if (snapshot_ && resourceCounters_)
                resourceCounters_->AddTargetSnapshot(surface->Width(), surface->Height());
        }
        return snapshot_;
    }

    void SkiaRenderTargetRenderer::BeforeWriteEXT() noexcept
    {
        levelZeroDirty_ = true;
        InvalidateDescendants(0);
        InvalidateSnapshot();
    }

    void SkiaRenderTargetRenderer::FinalizeWriteEXT()
    {
        SynchronizeRenderedLevelZero();
        GenerateDirtyMipLevels();
    }

    void SkiaRenderTargetRenderer::PrepareForBind()
    {
        if (!preserveContents_)
        {
            BeforeWriteEXT();
            Surface().Clear(0.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    SkiaSurface* SkiaRenderTargetRenderer::LevelSurface(int level) noexcept
    {
        return level >= 0 && level < static_cast<int>(surfaces_.size())
            ? surfaces_[static_cast<std::size_t>(level)].get() : nullptr;
    }

    const SkiaSurface* SkiaRenderTargetRenderer::LevelSurface(int level) const noexcept
    {
        return level >= 0 && level < static_cast<int>(surfaces_.size())
            ? surfaces_[static_cast<std::size_t>(level)].get() : nullptr;
    }

    void SkiaRenderTargetRenderer::SynchronizeRenderedLevelZero()
    {
        if (!levelZeroDirty_)
            return;
        const SkiaMipLevel2D& levelZero = mipChain_->Level(0);
        if (ResolveRenderTargetFormat(format_).extractSubset)
        {
            std::vector<std::uint8_t> native(
                static_cast<std::size_t>(levelZero.width) * levelZero.height * 16u);
            if (!Surface().ReadPixels(0, 0, levelZero.width, levelZero.height, native.data(),
                                      levelZero.width * 16))
            {
                throw std::runtime_error(
                    "Skia RenderTarget2D failed to synchronize rendered level-zero pixels.");
            }
            ExtractFromRgbaF32(native.data(), mipChain_->LevelData(0), levelZero.width,
                               levelZero.height, bytesPerTexel_);
        }
        else if (!Surface().ReadPixels(0, 0, levelZero.width, levelZero.height,
                                       mipChain_->LevelData(0),
                                       static_cast<int>(levelZero.rowBytes)))
        {
            throw std::runtime_error(
                "Skia RenderTarget2D failed to synchronize rendered level-zero pixels.");
        }
        levelZeroDirty_ = false;
    }

    void SkiaRenderTargetRenderer::InvalidateDescendants(int level) noexcept
    {
        for (int descendant = level + 1; descendant < mipChain_->LevelCount(); ++descendant)
            dirtyMipLevels_[static_cast<std::size_t>(descendant)] = true;
    }

    void SkiaRenderTargetRenderer::GenerateDirtyMipLevels()
    {
        const bool extractSubset = ResolveRenderTargetFormat(format_).extractSubset;
        for (int level = 1; level < mipChain_->LevelCount(); ++level)
        {
            if (!dirtyMipLevels_[static_cast<std::size_t>(level)])
                continue;

            GenerateFormattedSkiaMipLevel(format_, bytesPerTexel_, *mipChain_, level);
            const SkiaMipLevel2D& generated = mipChain_->Level(level);
            SkiaSurface* surface = LevelSurface(level);
            bool wrote = false;
            if (surface)
            {
                if (extractSubset)
                {
                    const std::vector<std::uint8_t> native = ExpandToRgbaF32(
                        mipChain_->LevelData(level), generated.width, generated.height,
                        bytesPerTexel_);
                    wrote = surface->WritePixels(0, 0, generated.width, generated.height,
                                                 native.data(), generated.width * 16);
                }
                else
                {
                    wrote = surface->WritePixels(
                        0, 0, generated.width, generated.height, mipChain_->LevelData(level),
                        static_cast<int>(generated.rowBytes));
                }
            }
            if (!wrote)
            {
                throw std::runtime_error(
                    "Skia RenderTarget2D failed to materialize a generated mip surface.");
            }
            InvalidateSnapshot(level);
            dirtyMipLevels_[static_cast<std::size_t>(level)] = false;
            ++mipGenerationCounts_[static_cast<std::size_t>(level)];
        }
    }

    std::uint64_t SkiaRenderTargetRenderer::MipGenerationCountEXT(int level) const
    {
        if (level < 0 || level >= static_cast<int>(mipGenerationCounts_.size()))
            throw std::out_of_range(
                "Skia RenderTarget2D mip generation level is out of range.");
        return mipGenerationCounts_[static_cast<std::size_t>(level)];
    }

    bool SkiaRenderTargetRenderer::MipLevelDirtyEXT(int level) const
    {
        if (level < 0 || level >= static_cast<int>(dirtyMipLevels_.size()))
            throw std::out_of_range("Skia RenderTarget2D dirty mip level is out of range.");
        return level == 0 ? levelZeroDirty_
                          : dirtyMipLevels_[static_cast<std::size_t>(level)];
    }

    bool SkiaRenderTargetRenderer::BelongsToBindingEXT(
        const std::shared_ptr<SkiaRenderTargetBinding>& binding) const noexcept
    {
        return binding && binding_.lock() == binding;
    }

    void SkiaRenderTargetRenderer::InvalidateSnapshot(int level) noexcept
    {
        if (snapshotLevel_ == level)
            InvalidateSnapshot();
    }

    void SkiaRenderTargetRenderer::InvalidateSnapshot() noexcept
    {
        if (!snapshot_)
            return;
        const SkiaSurface* surface = LevelSurface(snapshotLevel_);
        snapshot_.reset();
        if (resourceCounters_)
        {
            if (surface)
                resourceCounters_->RemoveTargetSnapshot(surface->Width(), surface->Height());
        }
        snapshotLevel_ = -1;
    }
} // namespace CNA::Internal::Renderers::Skia
