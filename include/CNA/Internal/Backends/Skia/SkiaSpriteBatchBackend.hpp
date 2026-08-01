#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"

namespace CNA::Internal::Backends::Skia
{
    /** Immediate SkCanvas implementation of the level-0 SpriteBatch path. */
    class SkiaSpriteBatchBackend final : public ISpriteBatchBackend
    {
    public:
        explicit SkiaSpriteBatchBackend(SkiaSurface*& activeSurface) : activeSurface_(&activeSurface) {}

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& matrix) override { transformMatrix_ = matrix; }
        void SetCustomEffect(Effect* effect) override;
        void SetSamplerFilter(int textureFilter) override;
        void SetSamplerAddressMode(int addressU, int addressV) override;

        void Draw(const ITextureBackend& texture, float x, float y) override;
        void Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Color& color) override;
        void Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Color& color, float rotation,
                  const Vector2& origin, SpriteEffects effects, float layerDepth) override;

    private:
        SkiaSurface** activeSurface_ = nullptr;
        bool begun_ = false;
        Matrix transformMatrix_ = Matrix::getIdentityProperty();
        int textureFilter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
    };
} // namespace CNA::Internal::Backends::Skia
