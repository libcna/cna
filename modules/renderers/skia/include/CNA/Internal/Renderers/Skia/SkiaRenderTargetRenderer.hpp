#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaImageSource.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaMipChain2D.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaRasterTarget.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaRenderTargetBinding.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaResourceCounters.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaSurface.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <memory>
#include <vector>

namespace CNA::Internal::Renderers::Skia
{
    /**
     * CPU raster off-screen target with a bindable level zero and stable sampleable mip levels.
     *
     * SKIA-142: @p format selects the native Skia pixel type each level's SkiaSurface is created
     * with (matching the exact format contract from docs/skia-surface-format-matrix.md's "FNA/Skia
     * RT decision" column: only the formats FNA itself reports renderable are promoted). For every
     * promoted format except Single/Vector2, the surface's native bytes ARE the public XNA
     * GetData/SetData bytes -- no shadow duality is needed because Skia has no lossy layout
     * mismatch for these types, unlike some Texture2D formats. Single/Vector2 have no 1/2-channel
     * 32-bit-float SkColorType, so their surface is kRGBA_F32 with only R (or R,G) meaningful;
     * mipChain_ still stores the narrow public bytes, and the extra channels are expanded/dropped
     * at the surface boundary only.
     */
    class SkiaRenderTargetRenderer final : public IRenderTargetRenderer,
                                          public SkiaImageSource,
                                          public SkiaRasterTarget
    {
    public:
        SkiaRenderTargetRenderer(
            int width, int height, bool preserveContents,
            std::weak_ptr<SkiaRenderTargetBinding> binding,
            std::shared_ptr<SkiaResourceCounters> resourceCounters = {}, bool mipMap = false,
            Microsoft::Xna::Framework::Graphics::SurfaceFormat format =
                Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color);
        ~SkiaRenderTargetRenderer() override;

        [[nodiscard]] int GetWidth() const override { return Surface().Width(); }
        [[nodiscard]] int GetHeight() const override { return Surface().Height(); }

        void UpdatePixels(const std::uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const std::uint8_t* rgba,
                               int levelWidth, int levelHeight) override;
        [[nodiscard]] bool HasDefinedMipLevel(int level) const noexcept override
        {
            return mipChain_ && level >= 0 && level < mipChain_->LevelCount();
        }
        [[nodiscard]] bool GetData(int level, int x, int y, int width, int height,
                                   void* data, int dataLength) const override;
        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] bool HasRealDepthBuffer(bool) const override { return false; }
        [[nodiscard]] SkiaSourceStorageAlpha StorageAlphaEXT() const noexcept override
        {
            return SkiaSourceStorageAlpha::PremultipliedSurface;
        }
        [[nodiscard]] sk_sp<SkImage> SnapshotImage(
            SkiaSourceAlphaConvention alphaConvention) const override;
        [[nodiscard]] sk_sp<SkImage> SnapshotMipLevelEXT(
            int level, SkiaSourceAlphaConvention alphaConvention) const override;
        [[nodiscard]] int MipLevelCountEXT() const noexcept override
        {
            return mipChain_ ? mipChain_->LevelCount() : 0;
        }

        [[nodiscard]] SkiaSurface& Surface() noexcept { return *surfaces_[0]; }
        [[nodiscard]] const SkiaSurface& Surface() const noexcept { return *surfaces_[0]; }
        [[nodiscard]] SkiaSurface* BoundSurfaceEXT() noexcept override { return &Surface(); }
        [[nodiscard]] const SkiaSurface* BoundSurfaceEXT() const noexcept override { return &Surface(); }
        void BeforeWriteEXT() noexcept override;
        void FinalizeWriteEXT() override;
        void PrepareForBind();
        /// Drops this target's one-entry sampling cache before a canvas or upload write.
        void InvalidateSnapshot() noexcept;

        CNAEXT [[nodiscard]] std::size_t SurfaceStorageBytesEXT() const noexcept
        {
            return surfaceStorageBytes_;
        }
        CNAEXT [[nodiscard]] const SkiaMipChain2D& MipChainEXT() const noexcept
        {
            return *mipChain_;
        }
        CNAEXT [[nodiscard]] std::uint64_t MipGenerationCountEXT(int level) const;
        CNAEXT [[nodiscard]] bool MipLevelDirtyEXT(int level) const;
        [[nodiscard]] bool BelongsToBindingEXT(
            const std::shared_ptr<SkiaRenderTargetBinding>& binding) const noexcept;
        CNAEXT [[nodiscard]] Microsoft::Xna::Framework::Graphics::SurfaceFormat FormatEXT() const
            noexcept
        {
            return format_;
        }

    private:
        [[nodiscard]] SkiaSurface* LevelSurface(int level) noexcept;
        [[nodiscard]] const SkiaSurface* LevelSurface(int level) const noexcept;
        void SynchronizeRenderedLevelZero();
        void InvalidateDescendants(int level) noexcept;
        void GenerateDirtyMipLevels();
        void InvalidateSnapshot(int level) noexcept;

        Microsoft::Xna::Framework::Graphics::SurfaceFormat format_ =
            Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color;
        std::size_t bytesPerTexel_ = 4u;
        std::unique_ptr<SkiaMipChain2D> mipChain_;
        std::vector<std::unique_ptr<SkiaSurface>> surfaces_;
        bool preserveContents_ = false;
        bool levelZeroDirty_ = false;
        std::vector<bool> dirtyMipLevels_;
        std::vector<std::uint64_t> mipGenerationCounts_;
        std::size_t surfaceStorageBytes_ = 0u;
        std::weak_ptr<SkiaRenderTargetBinding> binding_;
        std::shared_ptr<SkiaResourceCounters> resourceCounters_;
        mutable sk_sp<SkImage> snapshot_;
        mutable int snapshotLevel_ = -1;
        bool resourceRegistered_ = false;
    };
} // namespace CNA::Internal::Renderers::Skia
