#pragma once

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaImageSource.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRasterState.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRenderTargetBinding.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"

#include "include/core/SkBlender.h"
#include "include/core/SkBlendMode.h"

#include <cstdint>
#include <memory>

namespace CNA::Internal::Backends::Skia
{
    class SkiaEffectBackend;

    /** Immediate SkCanvas implementation of the bounded 2D SpriteBatch and mip-sampling path. */
    class SkiaSpriteBatchBackend final : public ISpriteBatchBackend
    {
    public:
        SkiaSpriteBatchBackend(SkiaSurface*& activeSurface, const SkBlendMode& blendMode,
                               const sk_sp<SkBlender>& customBlender,
                               const SkiaSourceAlphaConvention& sourceAlphaConvention,
                               const SkiaRasterState& rasterState,
                               const std::shared_ptr<SkiaRenderTargetBinding>& targetBinding)
            : activeSurface_(&activeSurface), blendMode_(&blendMode)
            , customBlender_(&customBlender), sourceAlphaConvention_(&sourceAlphaConvention)
            , rasterState_(&rasterState), targetBinding_(targetBinding) {}

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& matrix) override;
        void SetCustomEffect(Effect* effect) override;
        void SetSamplerFilter(int textureFilter) override;
        void SetSamplerAddressMode(int addressU, int addressV) override;

        void Draw(const ITextureBackend& texture, float x, float y) override;
        void Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Color& color) override;
        void Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Color& color, float rotation,
                  const Vector2& origin, SpriteEffects effects, float layerDepth) override;

        void DrawMeshEXT(Effect& effect, const Vector2* positions, const Color* colors,
                         const Vector2* uvs, int vertexCount, const std::uint16_t* indices,
                         int indexCount) override;

    private:
        [[nodiscard]] std::shared_ptr<SkiaRenderTargetBinding> LockBinding(
            const char* operation) const;

        SkiaSurface** activeSurface_ = nullptr;
        const SkBlendMode* blendMode_ = nullptr;
        const sk_sp<SkBlender>* customBlender_ = nullptr;
        const SkiaSourceAlphaConvention* sourceAlphaConvention_ = nullptr;
        const SkiaRasterState* rasterState_ = nullptr;
        std::weak_ptr<SkiaRenderTargetBinding> targetBinding_;
        bool begun_ = false;
        Matrix transformMatrix_ = Matrix::getIdentityProperty();
        int textureFilter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
        SkiaEffectBackend* customEffect_ = nullptr;
    };
} // namespace CNA::Internal::Backends::Skia
