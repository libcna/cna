#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaRasterTarget.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaRenderTargetBinding.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaResourceCounters.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaSurface.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Renderers::Skia
{
    /** Six-face CPU raster target with exact readback/upload and generated RGBA8 mip storage. */
    class SkiaRenderTargetCubeRenderer final : public IRenderTargetCubeRenderer,
                                              public SkiaRasterTarget
    {
    public:
        SkiaRenderTargetCubeRenderer(int size, bool preserveContents, bool mipMap,
                                    std::weak_ptr<SkiaRenderTargetBinding> binding,
                                    std::shared_ptr<SkiaResourceCounters> resourceCounters = {});
        ~SkiaRenderTargetCubeRenderer() override;

        SkiaRenderTargetCubeRenderer(const SkiaRenderTargetCubeRenderer&) = delete;
        SkiaRenderTargetCubeRenderer& operator=(const SkiaRenderTargetCubeRenderer&) = delete;
        SkiaRenderTargetCubeRenderer(SkiaRenderTargetCubeRenderer&&) = delete;
        SkiaRenderTargetCubeRenderer& operator=(SkiaRenderTargetCubeRenderer&&) = delete;

        [[nodiscard]] int GetSize() const override { return size_; }
        [[nodiscard]] int GetSizeEXT() const noexcept override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] int GetMultiSampleCount() const override { return 0; }
        [[nodiscard]] bool HasRealDepthBuffer(bool) const override { return false; }

        [[nodiscard]] bool SetData(int face, int level, int x, int y, int width, int height,
                                   const void* data, int dataLength) override;
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int width, int height,
                                   void* data, int dataLength) const override;

        [[nodiscard]] SkiaSurface* BoundSurfaceEXT() noexcept override;
        [[nodiscard]] const SkiaSurface* BoundSurfaceEXT() const noexcept override;
        void BeforeWriteEXT() noexcept override;
        void FinalizeWriteEXT() override;

        CNAEXT [[nodiscard]] int LevelCountEXT() const noexcept
        {
            return static_cast<int>(levels_.size());
        }
        CNAEXT [[nodiscard]] std::size_t StorageBytesEXT() const noexcept { return storageBytes_; }
        CNAEXT [[nodiscard]] bool PreservesContentsEXT() const noexcept { return preserveContents_; }

        /**
         * SKIA-146: returns an immutable sampling snapshot of one face/level, synchronizing that
         * face first if a prior draw left it dirty (mirroring `SkiaRenderTargetRenderer`'s existing
         * single-target snapshot cache, but one independent cache per face -- cube sampling binds
         * all six faces as simultaneous `cnaCubeFace0`-`5` children, so any of the six may be read
         * by a given draw's fragment evaluation, not just whichever face was most recently bound).
         * Returns nullptr for an out-of-range face/level. The cached snapshot for a face is
         * dropped as soon as that face is next written to, never on an unrelated face's write.
         */
        CNAEXT [[nodiscard]] sk_sp<SkImage> SnapshotFaceEXT(int face, int level) const;
        /// Drops every face's cached sampling snapshot; called from the destructor and available
        /// for callers that need every cache reset without writing to any face.
        CNAEXT void InvalidateAllFaceSnapshotsEXT() noexcept;

    private:
        struct Level final
        {
            int dimension = 0;
            std::array<std::unique_ptr<SkiaSurface>, 6> faces;
            // Canonical straight-RGBA bytes preserve SetData exactly; SkSurface itself stores
            // premultiplied pixels and cannot round-trip every 8-bit translucent value.
            std::array<std::vector<std::uint8_t>, 6> pixels;
        };

        [[nodiscard]] SkiaSurface* FaceSurface(int face, int level) noexcept;
        [[nodiscard]] const SkiaSurface* FaceSurface(int face, int level) const noexcept;
        void SynchronizeRenderedFace(int face);
        void GenerateMipChain(int face);
        void InvalidateFaceSnapshotEXT(int face) const noexcept;

        int size_ = 0;
        bool preserveContents_ = false;
        int boundFace_ = -1;
        std::array<bool, 6> mipDirty_{};
        std::vector<Level> levels_;
        std::size_t storageBytes_ = 0;
        std::weak_ptr<SkiaRenderTargetBinding> binding_;
        std::shared_ptr<SkiaResourceCounters> resourceCounters_;
        bool resourceRegistered_ = false;
        mutable std::array<sk_sp<SkImage>, 6> faceSnapshots_;
        mutable std::array<int, 6> faceSnapshotLevel_{-1, -1, -1, -1, -1, -1};
    };
} // namespace CNA::Internal::Renderers::Skia
