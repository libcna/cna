#include "CNA/Internal/Backends/Skia/SkiaSpriteBatchBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaEffectBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaImageSource.hpp"
#include "CNA/Internal/Backends/Skia/SkiaMeshEffectBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaMipSampling.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRasterTarget.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSourceAlphaPolicy.hpp"
#include "CNA/Internal/Backends/Skia/SkiaStateTrace.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkData.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkVertices.h"
#include "include/effects/SkRuntimeEffect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Backends::Skia
{
    namespace
    {
        [[nodiscard]] SkSamplingOptions ToSampling(SkiaWithinLevelFilter filter)
        {
            return SkSamplingOptions(filter == SkiaWithinLevelFilter::Point
                ? SkFilterMode::kNearest
                : SkFilterMode::kLinear);
        }

        [[nodiscard]] const char* SamplerFilterName(int textureFilter)
        {
            switch (textureFilter)
            {
                case 0: return "Linear";
                case 1: return "Point";
                case 2: return "Anisotropic";
                case 3: return "LinearMipPoint";
                case 4: return "PointMipLinear";
                case 5: return "MinLinearMagPointMipLinear";
                case 6: return "MinLinearMagPointMipPoint";
                case 7: return "MinPointMagLinearMipLinear";
                case 8: return "MinPointMagLinearMipPoint";
                default: return "invalid";
            }
        }

        struct MipBlendUniforms final
        {
            float lowerScaleOffset[4] {};
            float upperScaleOffset[4] {};
            float lowerBounds[4] {};
            float upperBounds[4] {};
            float upperWeight = 0.0f;
        };

        [[nodiscard]] const sk_sp<SkRuntimeEffect>& MipBlendEffect()
        {
            static const sk_sp<SkRuntimeEffect> effect = []
            {
                auto result = SkRuntimeEffect::MakeForShader(SkString(R"(
                    uniform shader lowerLevel;
                    uniform shader upperLevel;
                    uniform float4 lowerScaleOffset;
                    uniform float4 upperScaleOffset;
                    uniform float4 lowerBounds;
                    uniform float4 upperBounds;
                    uniform float upperWeight;
                    half4 main(float2 sourcePixel) {
                        float2 lowerCoord = clamp(
                            sourcePixel * lowerScaleOffset.xy + lowerScaleOffset.zw,
                            lowerBounds.xy, lowerBounds.zw);
                        float2 upperCoord = clamp(
                            sourcePixel * upperScaleOffset.xy + upperScaleOffset.zw,
                            upperBounds.xy, upperBounds.zw);
                        half4 lower = lowerLevel.eval(lowerCoord);
                        half4 upper = upperLevel.eval(upperCoord);
                        return lower + (upper - lower) * half(upperWeight);
                    }
                )"));
                if (!result.effect || result.effect->uniformSize() != sizeof(MipBlendUniforms))
                    throw std::runtime_error("Skia failed to compile the bounded mip blend shader.");
                return std::move(result.effect);
            }();
            return effect;
        }

        void FillMipCoordinates(float output[4], const SkImage& image,
                                int baseWidth, int baseHeight,
                                const Rectangle& sourceRectangle, const Vector2& origin)
        {
            const float scaleX = static_cast<float>(image.width()) / baseWidth;
            const float scaleY = static_cast<float>(image.height()) / baseHeight;
            output[0] = scaleX;
            output[1] = scaleY;
            output[2] = (static_cast<float>(sourceRectangle.X) + origin.X) * scaleX;
            output[3] = (static_cast<float>(sourceRectangle.Y) + origin.Y) * scaleY;
        }

        void FillMipBounds(float output[4], const SkImage& image,
                           int baseWidth, int baseHeight,
                           const Rectangle& sourceRectangle, bool strictSource)
        {
            if (!strictSource)
            {
                constexpr float kUnbounded = 1.0e20f;
                output[0] = -kUnbounded;
                output[1] = -kUnbounded;
                output[2] = kUnbounded;
                output[3] = kUnbounded;
                return;
            }

            const float scaleX = static_cast<float>(image.width()) / baseWidth;
            const float scaleY = static_cast<float>(image.height()) / baseHeight;
            const float leftEdge = static_cast<float>(sourceRectangle.X) * scaleX;
            const float topEdge = static_cast<float>(sourceRectangle.Y) * scaleY;
            const float rightEdge = static_cast<float>(sourceRectangle.X + sourceRectangle.Width) * scaleX;
            const float bottomEdge = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) * scaleY;
            output[0] = std::min(leftEdge + 0.5f, (leftEdge + rightEdge) * 0.5f);
            output[1] = std::min(topEdge + 0.5f, (topEdge + bottomEdge) * 0.5f);
            output[2] = std::max(rightEdge - 0.5f, (leftEdge + rightEdge) * 0.5f);
            output[3] = std::max(bottomEdge - 0.5f, (topEdge + bottomEdge) * 0.5f);
        }

        [[nodiscard]] SkMatrix MipLocalMatrix(const SkImage& image,
                                              int baseWidth, int baseHeight,
                                              const Rectangle& sourceRectangle,
                                              const Vector2& origin)
        {
            SkMatrix result;
            result.setAll(
                static_cast<float>(baseWidth) / image.width(), 0.0f,
                -static_cast<float>(sourceRectangle.X) - origin.X,
                0.0f, static_cast<float>(baseHeight) / image.height(),
                -static_cast<float>(sourceRectangle.Y) - origin.Y,
                0.0f, 0.0f, 1.0f);
            return result;
        }

        [[nodiscard]] SkRect MipSourceRect(const SkImage& image,
                                           int baseWidth, int baseHeight,
                                           const Rectangle& sourceRectangle)
        {
            const float scaleX = static_cast<float>(image.width()) / baseWidth;
            const float scaleY = static_cast<float>(image.height()) / baseHeight;
            return SkRect::MakeXYWH(
                static_cast<float>(sourceRectangle.X) * scaleX,
                static_cast<float>(sourceRectangle.Y) * scaleY,
                static_cast<float>(sourceRectangle.Width) * scaleX,
                static_cast<float>(sourceRectangle.Height) * scaleY);
        }

        [[nodiscard]] sk_sp<SkShader> MakeMipBlendShader(
            const sk_sp<SkImage>& lowerImage, const sk_sp<SkImage>& upperImage,
            int baseWidth, int baseHeight, const Rectangle& sourceRectangle,
            const Vector2& origin, SkTileMode tileU, SkTileMode tileV,
            const SkSamplingOptions& sampling, float upperWeight, bool strictSource)
        {
            if (!lowerImage || !upperImage)
                return nullptr;
            std::array<sk_sp<SkShader>, 2> children = {
                lowerImage->makeShader(tileU, tileV, sampling),
                upperImage->makeShader(tileU, tileV, sampling),
            };
            if (!children[0] || !children[1])
                return nullptr;

            MipBlendUniforms uniforms;
            FillMipCoordinates(uniforms.lowerScaleOffset, *lowerImage,
                               baseWidth, baseHeight, sourceRectangle, origin);
            FillMipCoordinates(uniforms.upperScaleOffset, *upperImage,
                               baseWidth, baseHeight, sourceRectangle, origin);
            FillMipBounds(uniforms.lowerBounds, *lowerImage,
                          baseWidth, baseHeight, sourceRectangle, strictSource);
            FillMipBounds(uniforms.upperBounds, *upperImage,
                          baseWidth, baseHeight, sourceRectangle, strictSource);
            uniforms.upperWeight = upperWeight;
            return MipBlendEffect()->makeShader(
                SkData::MakeWithCopy(&uniforms, sizeof(uniforms)),
                children.data(), children.size());
        }

        [[nodiscard]] bool IsFlipped(SpriteEffects effects, SpriteEffects flag)
        {
            return (static_cast<int>(effects) & static_cast<int>(flag)) != 0;
        }

        [[nodiscard]] SkTileMode ToTileMode(int textureAddressMode)
        {
            switch (textureAddressMode)
            {
                case 0: return SkTileMode::kRepeat; // TextureAddressMode::Wrap
                case 1: return SkTileMode::kClamp;  // TextureAddressMode::Clamp
                case 2: return SkTileMode::kMirror; // TextureAddressMode::Mirror
                default: throw std::runtime_error("Skia SpriteBatch received an invalid texture address mode.");
            }
        }

        [[nodiscard]] const char* AddressModeName(int textureAddressMode)
        {
            switch (textureAddressMode)
            {
                case 0: return "Wrap";
                case 1: return "Clamp";
                case 2: return "Mirror";
                default: return "invalid";
            }
        }

        [[nodiscard]] SkMatrix ToSkMatrix(const Matrix& matrix)
        {
            // CNA/XNA uses row vectors: Vector2::Transform(x, y, matrix) computes
            // (x * M11 + y * M21 + M41, x * M12 + y * M22 + M42). Skia stores the
            // equivalent affine transform for column vectors in its 3x3 matrix. This is
            // deliberately limited to the same six members that the public Vector2 helper
            // uses, so unrelated 3D members cannot change a SpriteBatch's 2D geometry.
            SkMatrix result;
            result.setAll(matrix.M11, matrix.M21, matrix.M41,
                          matrix.M12, matrix.M22, matrix.M42,
                          0.0f,       0.0f,       1.0f);
            return result;
        }

        [[nodiscard]] const sk_sp<SkRuntimeEffect>& TintEffect()
        {
            // SkColorFilters::Blend(..., kModulate) uses premultiplied tint RGB, which would
            // multiply a premultiplied XNA source by tint alpha a second time.  This filter works
            // directly in Skia's premultiplied pipeline and keeps component-wise XNA tint math.
            static const sk_sp<SkRuntimeEffect> effect = []
            {
                auto result = SkRuntimeEffect::MakeForColorFilter(SkString(R"(
                    uniform float4 tintScale;
                    half4 main(half4 source) {
                        return half4(source.rgb * tintScale.rgb, source.a * tintScale.a);
                    }
                )"));
                if (!result.effect)
                    throw std::runtime_error("Skia failed to compile the SpriteBatch tint color filter.");
                return std::move(result.effect);
            }();
            return effect;
        }

        [[nodiscard]] sk_sp<SkColorFilter> MakeTintFilter(const Color& color,
                                                            SkiaSourceAlphaConvention alphaConvention)
        {
            const SkiaTintScale scale = MakeSkiaTintScale(color, alphaConvention);
            return TintEffect()->makeColorFilter(SkData::MakeWithCopy(&scale, sizeof(scale)));
        }
    }

    std::shared_ptr<SkiaRenderTargetBinding> SkiaSpriteBatchBackend::LockBinding(
        const char* operation) const
    {
        std::shared_ptr<SkiaRenderTargetBinding> binding = targetBinding_.lock();
        if (!binding)
        {
            throw std::runtime_error(
                std::string("Skia ownership violation: ") + operation
                + " was attempted after graphics backend destruction.");
        }
        binding->AssertOwnerAccess(operation);
        return binding;
    }

    void SkiaSpriteBatchBackend::Begin()
    {
        (void)LockBinding("SpriteBatch::Begin");
        if (begun_)
            throw std::runtime_error("Skia SpriteBatch Begin() was called without a matching End().");
        begun_ = true;
    }

    void SkiaSpriteBatchBackend::End()
    {
        (void)LockBinding("SpriteBatch::End");
        if (!begun_)
            throw std::runtime_error("Skia SpriteBatch End() was called without a matching Begin().");
        begun_ = false;
    }

    void SkiaSpriteBatchBackend::SetTransformMatrix(const Matrix& matrix)
    {
        (void)LockBinding("SpriteBatch::SetTransformMatrix");
        transformMatrix_ = matrix;
    }

    void SkiaSpriteBatchBackend::SetCustomEffect(Effect* effect)
    {
        (void)LockBinding("SpriteBatch::SetCustomEffect");
        customEffect_ = nullptr;
        if (!effect || effect->IsExactStockSpriteEffectEXT())
        {
            // CNA's exact stock SpriteEffect owns no backend program; EasyGL also falls back to
            // its built-in sprite shader when this type is supplied explicitly. The Skia paint
            // path already implements that output. Exact type identity is intentional: silently
            // accepting a derived effect could discard an overridden OnApply() contract.
            return;
        }

        auto* skiaEffect = dynamic_cast<SkiaEffectBackend*>(effect->GetEffectBackendPtr());
        if (skiaEffect && skiaEffect->IsValid())
        {
            skiaEffect->ValidateSpriteBindingsEXT();
            customEffect_ = skiaEffect;
            return;
        }
        if (skiaEffect)
        {
            throw std::runtime_error(
                "Skia raster SpriteBatch rejected an invalid explicit SkSL effect: "
                + skiaEffect->GetCompileError());
        }
        throw std::runtime_error(
            "Skia raster SpriteBatch supports only the exact stock SpriteEffect or an explicit "
            "tagged SkSL effect; stock 3D Effects require the unsupported 3D primitive route, "
            "and other custom Effects require CNA_SKIA_SKSL_V1.");
    }

    void SkiaSpriteBatchBackend::SetSamplerFilter(int textureFilter)
    {
        (void)LockBinding("SpriteBatch::SetSamplerFilter");
        (void)DecodeSkiaMipFilter(textureFilter);
        textureFilter_ = textureFilter;
        TraceSkiaState("sampler filter=%s", SamplerFilterName(textureFilter));
    }

    void SkiaSpriteBatchBackend::SetSamplerAddressMode(int addressU, int addressV)
    {
        (void)LockBinding("SpriteBatch::SetSamplerAddressMode");
        // Validate up front because the address modes are carried as raw enum ordinals across the
        // shared backend seam. A shader is selected lazily by Draw only when its tiled path is
        // needed, so ordinary in-bounds Clamp sprites retain Skia's stricter source-rect path.
        (void)ToTileMode(addressU);
        (void)ToTileMode(addressV);
        addressU_ = addressU;
        addressV_ = addressV;
        TraceSkiaState("sampler address-u=%s address-v=%s", AddressModeName(addressU),
                       AddressModeName(addressV));
    }

    void SkiaSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        Draw(texture, Rectangle(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()),
             Rectangle(0, 0, texture.GetWidth(), texture.GetHeight()), Color(255, 255, 255, 255),
             0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
    }

    void SkiaSpriteBatchBackend::Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                                      const Rectangle& sourceRectangle, const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0.0f, 0.0f),
             SpriteEffects::None, 0.0f);
    }

    void SkiaSpriteBatchBackend::Draw(const ITextureBackend& texture, const Rectangle& destinationRectangle,
                                      const Rectangle& sourceRectangle, const Color& color, float rotation,
                                      const Vector2& origin, SpriteEffects effects, float layerDepth)
    {
        const std::shared_ptr<SkiaRenderTargetBinding> targetBinding = LockBinding("SpriteBatch::Draw");
        if (!begun_)
            throw std::runtime_error("Skia SpriteBatch Draw() was called before Begin().");
        if (sourceRectangle.Width <= 0 || sourceRectangle.Height <= 0
            || destinationRectangle.Width == 0 || destinationRectangle.Height == 0)
        {
            return;
        }

        const auto* skiaImageSource = dynamic_cast<const SkiaImageSource*>(&texture);
        const SkiaSourceAlphaConvention sourceAlphaConvention = sourceAlphaConvention_
            ? *sourceAlphaConvention_
            : SkiaSourceAlphaConvention::Premultiplied;
        if (!skiaImageSource || skiaImageSource->MipLevelCountEXT() <= 0)
            throw std::runtime_error("Skia SpriteBatch can only draw Skia Texture2D or RenderTarget2D resources.");
        const bool sourceInBounds = sourceRectangle.X >= 0 && sourceRectangle.Y >= 0
            && sourceRectangle.X <= texture.GetWidth() - sourceRectangle.Width
            && sourceRectangle.Y <= texture.GetHeight() - sourceRectangle.Height;

        (void)layerDepth; // Shared SpriteBatch ordering determines call order before this backend is invoked.
        SkCanvas* canvas = activeSurface_ && *activeSurface_ ? (*activeSurface_)->Canvas() : nullptr;
        if (!canvas)
            throw std::runtime_error("Skia SpriteBatch has no active raster canvas.");

        SkPaint paint;
        if (customBlender_ && *customBlender_)
            paint.setBlender(*customBlender_);
        else
            paint.setBlendMode(blendMode_ ? *blendMode_ : SkBlendMode::kSrcOver);
        if (!customEffect_ && color != Color::White)
            paint.setColorFilter(MakeTintFilter(color, sourceAlphaConvention));

        const float sourceWidth = static_cast<float>(sourceRectangle.Width);
        const float sourceHeight = static_cast<float>(sourceRectangle.Height);
        SkAutoCanvasRestore restore(canvas, true);
        if (rasterState_ && rasterState_->viewportSet)
        {
            // XNA's viewport is target-space and SpriteBatch coordinates are local to it. Clip
            // in target space before installing any per-sprite transform, then move the local
            // origin to the viewport's top-left corner below.
            canvas->clipRect(SkRect::MakeXYWH(static_cast<float>(rasterState_->viewportX),
                                               static_cast<float>(rasterState_->viewportY),
                                               static_cast<float>(rasterState_->viewportWidth),
                                               static_cast<float>(rasterState_->viewportHeight)),
                             SkClipOp::kIntersect, false);
        }
        if (rasterState_ && rasterState_->scissorTestEnabled)
        {
            // ScissorRectangle lives in active-target pixel coordinates, not in the SpriteBatch
            // transform's local coordinates. Clip before applying the Begin transform so its
            // edges remain axis-aligned and exact after any rotation/scale of a sprite.
            canvas->clipRect(SkRect::MakeXYWH(static_cast<float>(rasterState_->scissorX),
                                               static_cast<float>(rasterState_->scissorY),
                                               static_cast<float>(rasterState_->scissorWidth),
                                               static_cast<float>(rasterState_->scissorHeight)),
                             SkClipOp::kIntersect, false);
        }
        if (rasterState_ && rasterState_->viewportSet)
            canvas->translate(static_cast<float>(rasterState_->viewportX),
                              static_cast<float>(rasterState_->viewportY));
        // SkCanvas::concat pre-concatenates the matrix, making each following local sprite
        // transform run first and this Begin transform run second. That is XNA's ordering:
        // generate the sprite corner, then pass it through Vector2::Transform(matrix).
        canvas->concat(ToSkMatrix(transformMatrix_));
        canvas->translate(static_cast<float>(destinationRectangle.X), static_cast<float>(destinationRectangle.Y));
        canvas->rotate(rotation * 180.0f / 3.14159265358979323846f);
        canvas->scale(static_cast<float>(destinationRectangle.Width) / sourceWidth,
                      static_cast<float>(destinationRectangle.Height) / sourceHeight);

        const float localCenterX = -origin.X + sourceWidth * 0.5f;
        const float localCenterY = -origin.Y + sourceHeight * 0.5f;
        if (IsFlipped(effects, SpriteEffects::FlipHorizontally))
        {
            canvas->translate(localCenterX, 0.0f);
            canvas->scale(-1.0f, 1.0f);
            canvas->translate(-localCenterX, 0.0f);
        }
        if (IsFlipped(effects, SpriteEffects::FlipVertically))
        {
            canvas->translate(0.0f, localCenterY);
            canvas->scale(1.0f, -1.0f);
            canvas->translate(0.0f, -localCenterY);
        }

        const SkiaMipFilter mipFilter = DecodeSkiaMipFilter(textureFilter_);
        const SkiaMipSelection mipSelection = SelectSkiaMipLevels(
            mipFilter, skiaImageSource->MipLevelCountEXT(),
            SkiaTexelRate(canvas->getLocalToDeviceAs3x3()));
        const SkSamplingOptions sampling = ToSampling(mipSelection.withinLevel);
        const sk_sp<SkImage> lowerImage = skiaImageSource->SnapshotMipLevelEXT(
            mipSelection.lowerLevel, sourceAlphaConvention);
        const sk_sp<SkImage> upperImage = mipSelection.upperLevel != mipSelection.lowerLevel
            ? skiaImageSource->SnapshotMipLevelEXT(
                mipSelection.upperLevel, sourceAlphaConvention)
            : nullptr;
        if (!lowerImage || (mipSelection.upperLevel != mipSelection.lowerLevel && !upperImage))
            throw std::runtime_error("Skia SpriteBatch failed to snapshot the selected texture mip level.");

        const bool strictSource = sourceInBounds && addressU_ == 1 && addressV_ == 1;
        const SkRect source = MipSourceRect(
            *lowerImage, texture.GetWidth(), texture.GetHeight(), sourceRectangle);
        const SkRect destination = SkRect::MakeXYWH(-origin.X, -origin.Y, sourceWidth, sourceHeight);
        sk_sp<SkShader> primary;
        if (upperImage)
        {
            primary = MakeMipBlendShader(
                lowerImage, upperImage, texture.GetWidth(), texture.GetHeight(),
                sourceRectangle, origin, ToTileMode(addressU_), ToTileMode(addressV_),
                sampling, mipSelection.upperWeight, strictSource);
        }
        else if (customEffect_ || !strictSource)
        {
            primary = lowerImage->makeShader(
                ToTileMode(addressU_), ToTileMode(addressV_), sampling,
                MipLocalMatrix(*lowerImage, texture.GetWidth(), texture.GetHeight(),
                               sourceRectangle, origin));
        }

        // Delay the destination notification until every source snapshot and shader exists. In
        // particular, self-sampling an active target may resolve its pending mip chain while the
        // source image is captured; marking the destination dirty afterwards ensures the actual
        // draw still creates one later pass-boundary regeneration. A failed source setup leaves
        // the selected target unchanged.
        const auto beforeDestinationWrite = [&targetBinding]()
        {
            if (SkiaRasterTarget* target = targetBinding->ActiveTarget())
                target->BeforeWriteEXT();
        };

        if (customEffect_)
        {
            // The child shader uses the same source-pixel coordinate mapping and public sampler
            // state as the already-proven tiled stock path. The runtime shader itself receives
            // local source-pixel coordinates; cnaTexture0.eval(p) therefore reaches the matching
            // texture coordinate even for transformed/flipped/extended source rectangles.
            if (!primary)
                throw std::runtime_error("Skia failed to create the custom effect's cnaTexture0 child.");

            const SkiaTintScale tint = MakeSkiaTintScale(color, sourceAlphaConvention);
            const float tintValues[4] = {tint.red, tint.green, tint.blue, tint.alpha};
            sk_sp<SkShader> shader = customEffect_->MakeSpriteShaderEXT(
                std::move(primary), tintValues);
            if (!shader)
                throw std::runtime_error("Skia failed to instantiate the explicit SkSL SpriteBatch effect.");
            paint.setShader(std::move(shader));
            beforeDestinationWrite();
            canvas->drawRect(destination, paint);
            return;
        }
        if (upperImage)
        {
            if (!primary)
                throw std::runtime_error("Skia failed to instantiate the mip interpolation shader.");
            paint.setShader(std::move(primary));
            beforeDestinationWrite();
            canvas->drawRect(destination, paint);
            return;
        }
        if (strictSource)
        {
            // Strict source clipping is more precise than a clamp shader for atlas sub-rectangles:
            // with Linear filtering it prevents neighbouring texels from bleeding into the crop.
            beforeDestinationWrite();
            canvas->drawImageRect(lowerImage.get(), source, destination, sampling,
                                  &paint, SkCanvas::kStrict_SrcRectConstraint);
            return;
        }

        // Image shaders expose Skia's independent X/Y repeat and mirror tile modes. The shader's
        // local matrix maps the sprite's unscaled source-pixel space to the full texture; Skia
        // applies its inverse while evaluating a canvas point, hence the negative offset. This
        // lets source rectangles legitimately extend before or past the image bounds, just like
        // XNA SpriteBatch UVs, while the destination rect still bounds the painted geometry.
        if (!primary)
            throw std::runtime_error("Skia failed to create the SpriteBatch image shader.");
        paint.setShader(std::move(primary));
        beforeDestinationWrite();
        canvas->drawRect(destination, paint);
    }

    void SkiaSpriteBatchBackend::DrawMeshEXT(
        Effect& effect, const Vector2* positions, const Color* colors, const Vector2* uvs,
        int vertexCount, const std::uint16_t* indices, int indexCount)
    {
        const std::shared_ptr<SkiaRenderTargetBinding> targetBinding =
            LockBinding("SpriteBatch::DrawMeshEXT");
        if (!begun_)
            throw std::runtime_error("Skia SpriteBatch DrawMeshEXT() was called before Begin().");
        if (!positions || vertexCount <= 0)
        {
            throw std::invalid_argument(
                "Skia SpriteBatch DrawMeshEXT requires positions and a positive vertexCount.");
        }
        if (!indices || indexCount <= 0 || indexCount % 3 != 0)
        {
            throw std::invalid_argument(
                "Skia SpriteBatch DrawMeshEXT requires a non-empty triangle-list indices array "
                "(indexCount must be a multiple of 3).");
        }

        auto* adapter = dynamic_cast<SkiaMeshEffectAdapterEXT*>(effect.GetEffectBackendPtr());
        if (!adapter || !adapter->IsValid())
        {
            throw std::runtime_error(
                "Skia SpriteBatch DrawMeshEXT requires a valid CNA_SKIA_SKSL_MESH_V1 ShaderEffect.");
        }
        // Throws with an actionable diagnostic before any Skia work if a declared cnaTexture0-7
        // child was never bound -- matches the sprite path's ValidateSpriteBindingsEXT precedent.
        adapter->ValidateMeshBindingsEXT();

        std::vector<SkPoint> skPositions(static_cast<std::size_t>(vertexCount));
        for (int i = 0; i < vertexCount; ++i)
            skPositions[static_cast<std::size_t>(i)] = SkPoint::Make(positions[i].X, positions[i].Y);

        std::vector<SkPoint> skUvs;
        if (uvs)
        {
            skUvs.resize(static_cast<std::size_t>(vertexCount));
            for (int i = 0; i < vertexCount; ++i)
                skUvs[static_cast<std::size_t>(i)] = SkPoint::Make(uvs[i].X, uvs[i].Y);
        }

        std::vector<SkColor> skColors;
        if (colors)
        {
            skColors.resize(static_cast<std::size_t>(vertexCount));
            for (int i = 0; i < vertexCount; ++i)
            {
                skColors[static_cast<std::size_t>(i)] = SkColorSetARGB(
                    colors[i].getAProperty(), colors[i].getRProperty(),
                    colors[i].getGProperty(), colors[i].getBProperty());
            }
        }

        sk_sp<SkVertices> vertices = SkVertices::MakeCopy(
            SkVertices::kTriangles_VertexMode, vertexCount, skPositions.data(),
            uvs ? skUvs.data() : nullptr, colors ? skColors.data() : nullptr, indexCount, indices);
        if (!vertices)
            throw std::runtime_error("Skia SpriteBatch DrawMeshEXT failed to build SkVertices.");

        SkCanvas* canvas = activeSurface_ && *activeSurface_ ? (*activeSurface_)->Canvas() : nullptr;
        if (!canvas)
            throw std::runtime_error("Skia SpriteBatch has no active raster canvas.");

        // Locked fresh from the adapter's current uniform/texture-child state every draw -- a
        // SetUniformX/BindTexture issued after this ShaderEffect was bound but before this draw is
        // therefore visible, matching every other bound-effect/texture live-reference contract in
        // this backend.
        sk_sp<SkShader> shader = adapter->MakeMeshShaderEXT();
        if (!shader)
        {
            throw std::runtime_error(
                "Skia SpriteBatch DrawMeshEXT failed to instantiate the mesh effect's shader.");
        }

        SkPaint paint;
        paint.setShader(std::move(shader));
        if (customBlender_ && *customBlender_)
            paint.setBlender(*customBlender_);
        else
            paint.setBlendMode(blendMode_ ? *blendMode_ : SkBlendMode::kSrcOver);

        SkAutoCanvasRestore restore(canvas, true);
        if (rasterState_ && rasterState_->viewportSet)
        {
            canvas->clipRect(SkRect::MakeXYWH(static_cast<float>(rasterState_->viewportX),
                                               static_cast<float>(rasterState_->viewportY),
                                               static_cast<float>(rasterState_->viewportWidth),
                                               static_cast<float>(rasterState_->viewportHeight)),
                             SkClipOp::kIntersect, false);
        }
        if (rasterState_ && rasterState_->scissorTestEnabled)
        {
            canvas->clipRect(SkRect::MakeXYWH(static_cast<float>(rasterState_->scissorX),
                                               static_cast<float>(rasterState_->scissorY),
                                               static_cast<float>(rasterState_->scissorWidth),
                                               static_cast<float>(rasterState_->scissorHeight)),
                             SkClipOp::kIntersect, false);
        }
        if (rasterState_ && rasterState_->viewportSet)
        {
            canvas->translate(static_cast<float>(rasterState_->viewportX),
                              static_cast<float>(rasterState_->viewportY));
        }
        // Same ordering rationale as the ordinary Draw() overloads above: concat pre-multiplies so
        // each caller-supplied position runs through this Begin transform exactly like a sprite's
        // own corner does.
        canvas->concat(ToSkMatrix(transformMatrix_));

        if (SkiaRasterTarget* target = targetBinding->ActiveTarget())
            target->BeforeWriteEXT();
        // kModulate combines any per-vertex colour against the shader's own output; when `colors`
        // is null this is a no-op (Skia's own drawVertices short-circuits to the shader alone),
        // matching docs/skia-vertices-2d-effect-contract.md's alpha-convention section exactly.
        canvas->drawVertices(vertices, SkBlendMode::kModulate, paint);
    }
} // namespace CNA::Internal::Backends::Skia
