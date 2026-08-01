#pragma once

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRasterTarget.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRenderTargetBinding.hpp"
#include "CNA/Internal/Backends/Skia/SkiaResourceCounters.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Backends::Skia
{
    /** Six-face CPU raster target with exact readback/upload and generated RGBA8 mip storage. */
    class SkiaRenderTargetCubeBackend final : public IRenderTargetCubeBackend,
                                              public SkiaRasterTarget
    {
    public:
        SkiaRenderTargetCubeBackend(int size, bool preserveContents, bool mipMap,
                                    std::weak_ptr<SkiaRenderTargetBinding> binding,
                                    std::shared_ptr<SkiaResourceCounters> resourceCounters = {});
        ~SkiaRenderTargetCubeBackend() override;

        SkiaRenderTargetCubeBackend(const SkiaRenderTargetCubeBackend&) = delete;
        SkiaRenderTargetCubeBackend& operator=(const SkiaRenderTargetCubeBackend&) = delete;
        SkiaRenderTargetCubeBackend(SkiaRenderTargetCubeBackend&&) = delete;
        SkiaRenderTargetCubeBackend& operator=(SkiaRenderTargetCubeBackend&&) = delete;

        [[nodiscard]] int GetSize() const override { return size_; }
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

        NOXNA [[nodiscard]] int LevelCountEXT() const noexcept
        {
            return static_cast<int>(levels_.size());
        }
        NOXNA [[nodiscard]] std::size_t StorageBytesEXT() const noexcept { return storageBytes_; }
        NOXNA [[nodiscard]] bool PreservesContentsEXT() const noexcept { return preserveContents_; }

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

        int size_ = 0;
        bool preserveContents_ = false;
        int boundFace_ = -1;
        std::array<bool, 6> mipDirty_{};
        std::vector<Level> levels_;
        std::size_t storageBytes_ = 0;
        std::weak_ptr<SkiaRenderTargetBinding> binding_;
        std::shared_ptr<SkiaResourceCounters> resourceCounters_;
        bool resourceRegistered_ = false;
    };
} // namespace CNA::Internal::Backends::Skia
