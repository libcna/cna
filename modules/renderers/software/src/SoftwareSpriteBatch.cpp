// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#if defined(CNA_SOFTWARE_COMPILED_EFFECTS)
#include "CNA/Internal/Renderers/Software/SoftwareCompiledEffect.hpp"
#include "Fna3dStockEffectBlobs.hpp"
#endif

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace CNA::Internal::Renderers::Software
{
    using Vector3 = Microsoft::Xna::Framework::Vector3;
    using Vector4 = Microsoft::Xna::Framework::Vector4;

    // Phase S6 (SOFTWARE-51): SpriteBatch owns the public quad geometry and transform path; its
    // four prepared corners then enter SoftwareRenderer::RasterizeSpriteQuad(), where they
    // reuse the same shared CPU triangle fragment rasterizer as the complete Software 3D renderer.

    SoftwareSpriteBatchRenderer::SoftwareSpriteBatchRenderer(SoftwareRenderer& owner) : owner_(owner)
    {
#if defined(CNA_SOFTWARE_COMPILED_EFFECTS)
        const auto& bytes =
            CNA::Internal::Renderers::Fna3d::StockEffectBlobs::kSpriteEffectFxb;
        compiledSpriteEffect_ = owner_.CreateCompiledEffect(bytes, sizeof(bytes));
        if (compiledSpriteEffect_ == nullptr)
            throw std::runtime_error(
                "Software SpriteBatch could not create its embedded XNA SpriteEffect.");
        const auto& parameters = compiledSpriteEffect_->GetDescription().parameters;
        const auto matrix = std::find_if(
            parameters.begin(), parameters.end(),
            [](const CompiledEffectParameterDescription& parameter)
            {
                return parameter.name == "MatrixTransform";
            });
        if (matrix == parameters.end())
            throw std::runtime_error(
                "Software embedded XNA SpriteEffect has no MatrixTransform parameter.");
        compiledSpriteMatrixParameter_ = matrix->runtimeIndex;
#endif
    }

    void SoftwareSpriteBatchRenderer::Begin()
    {
        if (begun_)
            throw std::runtime_error("SoftwareSpriteBatchRenderer::Begin: Begin() called without a matching End()");
        pendingCompiledSprites_.clear();
        compiledTexture_ = nullptr;
        begun_ = true;
    }

    void SoftwareSpriteBatchRenderer::End()
    {
        if (!begun_)
            throw std::runtime_error("SoftwareSpriteBatchRenderer::End: End() called without a matching Begin()");
        FlushCompiledBatch();
        begun_ = false;
    }

    bool SoftwareSpriteBatchRenderer::UsesCompiledEffect() const noexcept
    {
#if defined(CNA_SOFTWARE_COMPILED_EFFECTS)
        return customEffect_ != nullptr && customEffect_->GetCompiledRuntimePtr() != nullptr;
#else
        return false;
#endif
    }

    void SoftwareSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (customEffect_ == effect)
            return;
        FlushCompiledBatch();
        customEffect_ = effect;
    }

    void SoftwareSpriteBatchRenderer::ApplyCompiledSpriteEffect()
    {
#if defined(CNA_SOFTWARE_COMPILED_EFFECTS)
        if (compiledSpriteEffect_ == nullptr)
            throw System::InvalidOperationException(
                "Software SpriteBatch has no internal compiled SpriteEffect.");
        int viewportX = 0;
        int viewportY = 0;
        int viewportWidth = 0;
        int viewportHeight = 0;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
        owner_.GetActiveViewportRaster(
            viewportX, viewportY, viewportWidth, viewportHeight, minDepth, maxDepth);
        if (viewportWidth <= 0 || viewportHeight <= 0)
            throw System::InvalidOperationException(
                "Software SpriteBatch cannot apply its SpriteEffect to an empty viewport.");
        const Matrix projection = Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(viewportWidth),
            static_cast<float>(viewportHeight), 0.0f, 0.0f, -1.0f);
        const Matrix combined = transformMatrix_ * projection;
        const float values[16] = {
            combined.M11, combined.M21, combined.M31, combined.M41,
            combined.M12, combined.M22, combined.M32, combined.M42,
            combined.M13, combined.M23, combined.M33, combined.M43,
            combined.M14, combined.M24, combined.M34, combined.M44,
        };
        compiledSpriteEffect_->SetParameterValue(
            compiledSpriteMatrixParameter_, values, sizeof(values));
        compiledSpriteEffect_->SetTechnique(0);
        CompiledEffectPassStateChanges ignoredChanges;
        compiledSpriteEffect_->ApplyPass(0, {}, ignoredChanges);
#else
        throw System::NotSupportedException(
            "Software compiled-effect support was not enabled in this build.");
#endif
    }

    void SoftwareSpriteBatchRenderer::FlushCompiledBatch()
    {
        if (pendingCompiledSprites_.empty())
            return;
#if defined(CNA_SOFTWARE_COMPILED_EFFECTS)
        if (customEffect_ == nullptr || customEffect_->GetCompiledRuntimePtr() == nullptr)
            throw System::InvalidOperationException(
                "Software SpriteBatch lost its compiled Effect before flushing.");
        Microsoft::Xna::Framework::Graphics::EffectTechnique* technique =
            customEffect_->getCurrentTechniqueProperty();
        const int passCount =
            technique != nullptr ? technique->getPassesProperty().getCountProperty() : 0;
        if (passCount <= 0)
            throw System::InvalidOperationException(
                "Software SpriteBatch compiled Effect has no current pass.");

        ApplyCompiledSpriteEffect();
        auto* spriteRuntime =
            dynamic_cast<SoftwareCompiledEffect*>(compiledSpriteEffect_.get());
        if (spriteRuntime == nullptr || spriteRuntime->GetVertexProgramEXT() == nullptr ||
            spriteRuntime->GetPixelProgramEXT() == nullptr)
        {
            throw System::InvalidOperationException(
                "Software SpriteBatch internal SpriteEffect did not select both shader stages.");
        }
        for (int pass = 0; pass < passCount; ++pass)
        {
            technique->getPassesProperty()[pass]->Apply();
            GpuDrawParams effectParams;
            customEffect_->FillGpuDrawParams(effectParams);
            if (effectParams.compiledEffectRuntime == nullptr)
                throw System::InvalidOperationException(
                    "Software SpriteBatch compiled Effect did not publish its runtime.");
            auto* customRuntime =
                dynamic_cast<SoftwareCompiledEffect*>(effectParams.compiledEffectRuntime);
            if (customRuntime == nullptr)
                throw System::InvalidOperationException(
                    "Software SpriteBatch received a compiled Effect from another renderer.");
            effectParams.compiledVertexEffectRuntime =
                customRuntime->GetVertexProgramEXT() != nullptr ? customRuntime : spriteRuntime;
            effectParams.compiledPixelEffectRuntime =
                customRuntime->GetPixelProgramEXT() != nullptr ? customRuntime : spriteRuntime;
            for (const PendingCompiledSprite& sprite : pendingCompiledSprites_)
            {
                owner_.RasterizeCompiledSpriteQuad(
                    *sprite.texture, sprite.corners, sprite.color,
                    sprite.textureCoordinates, effectParams);
            }
        }
        pendingCompiledSprites_.clear();
        compiledTexture_ = nullptr;
#else
        throw System::NotSupportedException(
            "Software compiled-effect support was not enabled in this build.");
#endif
    }

    void SoftwareSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        Draw(texture, x, y, static_cast<float>(texture.GetWidth()),
             static_cast<float>(texture.GetHeight()),
             Rectangle(0, 0, texture.GetWidth(), texture.GetHeight()),
             Color(255, 255, 255, 255), 0.0f, Vector2(0.0f, 0.0f),
             SpriteEffects::None, 0.0f);
    }

    void SoftwareSpriteBatchRenderer::Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle, const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0.0f, 0.0f),
             SpriteEffects::None, 0.0f);
    }

    void SoftwareSpriteBatchRenderer::Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle, const Color& color, float rotation,
                                          const Vector2& origin, SpriteEffects effects, float layerDepth)
    {
        Draw(texture,
             static_cast<float>(destinationRectangle.X),
             static_cast<float>(destinationRectangle.Y),
             static_cast<float>(destinationRectangle.Width),
             static_cast<float>(destinationRectangle.Height),
             sourceRectangle, color, rotation, origin, effects, layerDepth);
    }

    void SoftwareSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                          float destinationX,
                                          float destinationY,
                                          float destinationWidth,
                                          float destinationHeight,
                                          const Rectangle& sourceRectangle,
                                          const Color& color,
                                          float rotation,
                                          const Vector2& origin,
                                          SpriteEffects effects,
                                          float layerDepth)
    {
        if (!begun_)
            throw std::runtime_error("SoftwareSpriteBatchRenderer::Draw: Draw() called before Begin()");

        const float texW = static_cast<float>(std::max(1, texture.GetWidth()));
        const float texH = static_cast<float>(std::max(1, texture.GetHeight()));
        float u1 = static_cast<float>(sourceRectangle.X) / texW;
        float v1 = static_cast<float>(sourceRectangle.Y) / texH;
        float u2 = u1 + static_cast<float>(sourceRectangle.Width) / texW;
        float v2 = v1 + static_cast<float>(sourceRectangle.Height) / texH;
        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0) std::swap(u1, u2);
        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0) std::swap(v1, v2);

        const float r = color.getRProperty() / 255.0f;
        const float g = color.getGProperty() / 255.0f;
        const float b = color.getBProperty() / 255.0f;
        const float a = color.getAProperty() / 255.0f;

        const float dx = destinationX;
        const float dy = destinationY;
        const float dw = destinationWidth;
        const float dh = destinationHeight;
        // Source extents remain signed through XNA's sprite calculation. Their sign participates
        // in the origin transform, and a zero extent must not be expanded into one texel.
        const float sw = static_cast<float>(sourceRectangle.Width);
        const float sh = static_cast<float>(sourceRectangle.Height);
        const float ox = origin.X;
        const float oy = origin.Y;
        const float scaleX = dw / sw;
        const float scaleY = dh / sh;

        const float p0x = (0.0f - ox) * scaleX, p0y = (0.0f - oy) * scaleY;
        const float p1x = (sw - ox) * scaleX, p1y = (0.0f - oy) * scaleY;
        const float p2x = (sw - ox) * scaleX, p2y = (sh - oy) * scaleY;
        const float p3x = (0.0f - ox) * scaleX, p3y = (sh - oy) * scaleY;

        const float cosR = std::cos(rotation);
        const float sinR = std::sin(rotation);

        const auto placeRawCorner = [&](float px, float py) -> Vector4 {
            const float rx = dx + px * cosR - py * sinR;
            const float ry = dy + px * sinR + py * cosR;
            return Vector4(rx, ry, layerDepth, 1.0f);
        };

        const std::array<Vector4, 4> rawCorners = {
            placeRawCorner(p0x, p0y), placeRawCorner(p1x, p1y),
            placeRawCorner(p2x, p2y), placeRawCorner(p3x, p3y)};
        const auto finite = [](const Vector4& value) {
            return std::isfinite(value.X) && std::isfinite(value.Y) &&
                   std::isfinite(value.Z) && std::isfinite(value.W);
        };
        if (!finite(rawCorners[0]) || !finite(rawCorners[1]) ||
            !finite(rawCorners[2]) || !finite(rawCorners[3]))
            return;

        if (UsesCompiledEffect())
        {
            if (compiledTexture_ != nullptr && compiledTexture_ != &texture)
                FlushCompiledBatch();
            compiledTexture_ = &texture;
            pendingCompiledSprites_.push_back(PendingCompiledSprite{
                &texture, rawCorners, {r, g, b, a}, {u1, v1, u2, v2}});
            if (immediateMode_)
                FlushCompiledBatch();
            return;
        }

        // SOFTWARE-338: preserve FNA's complete `(x,y,layerDepth,1) * transformMatrix` result.
        // RasterizeSpriteQuad applies the SpriteBatch projection and viewport after homogeneous
        // clipping, so Viewport.X/Y remain outside the caller's transform (REMED-GFX-073).
        const Vector4 c0 = Vector4::Transform(
            Vector3(rawCorners[0].X, rawCorners[0].Y, rawCorners[0].Z), transformMatrix_);
        const Vector4 c1 = Vector4::Transform(
            Vector3(rawCorners[1].X, rawCorners[1].Y, rawCorners[1].Z), transformMatrix_);
        const Vector4 c2 = Vector4::Transform(
            Vector3(rawCorners[2].X, rawCorners[2].Y, rawCorners[2].Z), transformMatrix_);
        const Vector4 c3 = Vector4::Transform(
            Vector3(rawCorners[3].X, rawCorners[3].Y, rawCorners[3].Z), transformMatrix_);

        // A non-finite transform cannot cover a defined framebuffer pixel. Reject it before both
        // damage calculation and raster edge math, keeping huge/invalid matrices deterministic.
        if (!finite(c0) || !finite(c1) || !finite(c2) || !finite(c3))
            return;

        owner_.RasterizeSpriteQuad(texture, c0, c1, c2, c3, r, g, b, a, u1, v1, u2, v2,
                                   customEffect_, GetSamplerState());
    }
}
