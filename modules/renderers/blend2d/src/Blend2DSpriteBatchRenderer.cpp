#include "CNA/Internal/Renderers/Blend2D/Blend2DRenderer.hpp"
#include "CNA/Internal/Renderers/Blend2D/Blend2DCheckedCallEXT.hpp"
#include "CNA/Internal/Renderers/Blend2D/Blend2DPixelConvert.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Renderers::Blend2D
{
    namespace
    {
        /// RAII guard for BLContext::save()/restore(): guarantees the context's transform/clip
        /// stack stays balanced even if an exception (a failed BLResult check, a bad_alloc from a
        /// resolved sub-image, ...) unwinds out of the middle of a draw. Without this, a partial
        /// DrawQuad failure between save() and a manual restore() would leave every subsequent
        /// draw on this surface permanently offset/clipped by the abandoned sprite's transform.
        class ScopedContextStateEXT final
        {
        public:
            explicit ScopedContextStateEXT(BLContext& ctx) : ctx_(ctx)
            {
                Blend2DCheckEXT(ctx_.save(), "BLContext::save");
            }
            ~ScopedContextStateEXT() { ctx_.restore(); }
            ScopedContextStateEXT(const ScopedContextStateEXT&) = delete;
            ScopedContextStateEXT& operator=(const ScopedContextStateEXT&) = delete;

        private:
            BLContext& ctx_;
        };

        /// Checked cross-renderer resource cast: ISpriteBatchRenderer::Draw() receives only the
        /// abstract ITextureRenderer&, and a texture/render-target created by a DIFFERENT renderer
        /// crossing this boundary is a caller bug, not a Blend2D internal-consistency violation.
        /// An unchecked `dynamic_cast<const T&>` would raise std::bad_cast for that case -- an
        /// accidental RTTI exception with no actionable message and (per the C++ standard) no
        /// requirement that construction happens before any further evaluation. Fail with a clear,
        /// deliberate diagnostic instead, and do it before any native BLImage memory is touched.
        [[nodiscard]] const Blend2DNativeImageSourceEXT& RequireNativeImageEXT(const ITextureRenderer& texture)
        {
            const auto* source = dynamic_cast<const Blend2DNativeImageSourceEXT*>(&texture);
            if (!source)
            {
                throw std::invalid_argument(
                    "BLEND2D SpriteBatch::Draw received a Texture2D/RenderTarget2D created by a "
                    "different graphics renderer; cross-renderer resources cannot be sampled.");
            }
            return *source;
        }

        /// Raw Microsoft::Xna::Framework::Graphics::TextureFilter ordinal -> Blend2D pattern
        /// quality. TextureFilter has 9 values encoding separate min/mag/mip components (see
        /// TextureFilter.hpp), but BLPatternQuality is a single nearest/bilinear choice with no
        /// separate min/mag/mip control and Blend2D has no mip chain at all. SpriteBatch draws are
        /// near-universally magnification-dominant, so the MAGNIFICATION ("expand") component is
        /// used as the effective filter -- the same decomposition the native 2D renderer's
        /// SetSamplerFilter uses for the identical reason (Task 701 finding):
        ///   Linear=0, Anisotropic=2 (linear-based, no Blend2D equivalent), LinearMipPoint=3,
        ///   MinPointMagLinearMipLinear=7, MinPointMagLinearMipPoint=8 (all mag=Linear) -> BILINEAR.
        ///   Point=1, PointMipLinear=4, MinLinearMagPointMipLinear=5, MinLinearMagPointMipPoint=6
        ///   (all mag=Point) -> NEAREST.
        [[nodiscard]] BLPatternQuality SamplerFilterToPatternQualityEXT(int textureFilter)
        {
            switch (textureFilter)
            {
                case 0: case 2: case 3: case 7: case 8:
                    return BL_PATTERN_QUALITY_BILINEAR;
                case 1: case 4: case 5: case 6:
                    return BL_PATTERN_QUALITY_NEAREST;
                default:
                    throw std::runtime_error("Blend2D SpriteBatch received an invalid texture filter.");
            }
        }

        /// Validates a raw Microsoft::Xna::Framework::Graphics::TextureAddressMode ordinal
        /// (0=Wrap, 1=Clamp, 2=Mirror).
        void ValidateAddressModeEXT(int addressMode)
        {
            if (addressMode < 0 || addressMode > 2)
                throw std::runtime_error("Blend2D SpriteBatch received an invalid texture address mode.");
        }

        /// Maps @p coord (a texel coordinate that may be negative or >= @p size) into [0, size)
        /// per @p addressMode, exactly like a GPU sampler's address mode would for an out-of-[0,1]
        /// UV. Always returns a value that safely indexes the source image -- the one function
        /// every source-pixel read in this file goes through, so no source rectangle (however far
        /// negative or oversized) can ever produce an out-of-bounds read.
        [[nodiscard]] int AddressedCoordinateEXT(int coord, int size, int addressMode)
        {
            if (size <= 0) return 0;
            switch (addressMode)
            {
                case 1: // Clamp
                    return std::clamp(coord, 0, size - 1);
                case 0: // Wrap
                {
                    int m = coord % size;
                    if (m < 0) m += size;
                    return m;
                }
                case 2: // Mirror
                {
                    const int period = 2 * size;
                    int m = coord % period;
                    if (m < 0) m += period;
                    return m < size ? m : (period - 1 - m);
                }
                default:
                    throw std::runtime_error("Blend2D SpriteBatch received an invalid texture address mode.");
            }
        }

        /// Builds a new, fully independent premultiplied BLImage of exactly @p sw x @p sh pixels,
        /// safely gathering every source pixel from @p source (whose real dimensions are
        /// @p texW x @p texH) through AddressedCoordinateEXT -- so a source rectangle that is
        /// negative, extends past the texture, or samples the texture's own currently-active
        /// render-target image (self-sampling) never performs a direct, unchecked read against
        /// @p source's raw buffer. When @p applyTint is true, @p tint is applied as XNA
        /// SpriteBatch's per-vertex straight-component multiply before the result is
        /// re-premultiplied for BLImage storage (matches BuildTintedSubImageEXT's prior tint
        /// math exactly, just generalized to every source-rectangle case instead of only the
        /// already-in-bounds one).
        [[nodiscard]] BLImage ResolveSourceImageEXT(const BLImage& source, int texW, int texH, int sx, int sy,
                                                    int sw, int sh, int addressU, int addressV,
                                                    const Color& tint, bool applyTint)
        {
            BLImageData srcData{};
            Blend2DCheckEXT(source.get_data(&srcData), "BLImage::get_data (sprite source)");

            BLImage resolved;
            Blend2DCheckEXT(resolved.create(sw, sh, BL_FORMAT_PRGB32), "BLImage::create (resolved sprite source)");
            BLImageData dstData{};
            Blend2DCheckEXT(resolved.make_mutable(&dstData), "BLImage::make_mutable (resolved sprite source)");

            const auto* srcBase = static_cast<const std::uint8_t*>(srcData.pixel_data);
            auto* dstBase = static_cast<std::uint8_t*>(dstData.pixel_data);

            const int tr = static_cast<int>(tint.getRProperty());
            const int tg = static_cast<int>(tint.getGProperty());
            const int tb = static_cast<int>(tint.getBProperty());
            const int ta = static_cast<int>(tint.getAProperty());

            std::vector<std::uint8_t> gatheredPremulRow(static_cast<std::size_t>(sw) * 4);
            std::vector<std::uint8_t> straightRow(static_cast<std::size_t>(sw) * 4);

            for (int row = 0; row < sh; ++row)
            {
                const int srcY = AddressedCoordinateEXT(sy + row, texH, addressV);
                const std::uint8_t* srcRowBase = srcBase + static_cast<std::ptrdiff_t>(srcY) * srcData.stride;

                for (int col = 0; col < sw; ++col)
                {
                    const int srcX = AddressedCoordinateEXT(sx + col, texW, addressU);
                    const std::uint8_t* srcPixel = srcRowBase + static_cast<std::ptrdiff_t>(srcX) * 4;
                    std::memcpy(&gatheredPremulRow[static_cast<std::size_t>(col) * 4], srcPixel, 4);
                }
                ConvertPremultipliedBgraRowToStraightRgba(gatheredPremulRow.data(), straightRow.data(), sw);

                if (applyTint)
                {
                    for (int col = 0; col < sw; ++col)
                    {
                        straightRow[static_cast<std::size_t>(col) * 4 + 0] = static_cast<std::uint8_t>(
                            (straightRow[static_cast<std::size_t>(col) * 4 + 0] * tr + 127) / 255);
                        straightRow[static_cast<std::size_t>(col) * 4 + 1] = static_cast<std::uint8_t>(
                            (straightRow[static_cast<std::size_t>(col) * 4 + 1] * tg + 127) / 255);
                        straightRow[static_cast<std::size_t>(col) * 4 + 2] = static_cast<std::uint8_t>(
                            (straightRow[static_cast<std::size_t>(col) * 4 + 2] * tb + 127) / 255);
                        straightRow[static_cast<std::size_t>(col) * 4 + 3] = static_cast<std::uint8_t>(
                            (straightRow[static_cast<std::size_t>(col) * 4 + 3] * ta + 127) / 255);
                    }
                }

                std::uint8_t* dstRow = dstBase + static_cast<std::ptrdiff_t>(row) * dstData.stride;
                ConvertStraightRgbaRowToPremultipliedBgra(straightRow.data(), dstRow, sw);
            }

            return resolved;
        }

        [[nodiscard]] BLMatrix2D ToBlMatrixEXT(const Matrix& m)
        {
            // CNA/XNA uses row vectors: Vector2::Transform(x, y, matrix) computes
            // (x*M11 + y*M21 + M41, x*M12 + y*M22 + M42) -- see SkiaSpriteBatchRenderer's own
            // ToSkMatrix for the identical mapping applied to Skia's 3x3 convention. BLMatrix2D's
            // transform is x' = x*m00 + y*m10 + m20, y' = x*m01 + y*m11 + m21.
            return BLMatrix2D(m.M11, m.M12, m.M21, m.M22, m.M41, m.M42);
        }

        /// Everything one sprite draw applies to a BLContext AFTER set_comp_op/set_pattern_quality:
        /// viewport clip, scissor clip, viewport-local origin shift, the Begin() transform matrix,
        /// per-sprite translate/rotate/flip, and the final blit_image call. Extracted so
        /// ApplyIndependentBlendCorrectionEXT's auxiliary "effective source colour/alpha" recovery
        /// pass (below) can replay the IDENTICAL geometry/clipping/sampling against a scratch
        /// context -- the two passes must touch exactly the same destination pixels for the
        /// recovered per-pixel values to correspond to what the real draw actually painted.
        void PlaceAndBlitEXT(BLContext& ctx, const BLImage& img, const Matrix& transformMatrix,
                             bool viewportSet, int viewportX, int viewportY, int viewportW, int viewportH,
                             bool scissorEnabled, int scissorX, int scissorY, int scissorW, int scissorH,
                             double destX, double destY, double rotation, bool effectiveFlipH,
                             bool effectiveFlipV, double rectX, double rectY, double rectW, double rectH,
                             int blitSrcX, int blitSrcY, int sw, int sh)
        {
            ScopedContextStateEXT scopedState(ctx);

            if (viewportSet)
                Blend2DCheckEXT(ctx.clip_to_rect(BLRectI(viewportX, viewportY, viewportW, viewportH)), "BLContext::clip_to_rect (viewport)");
            if (scissorEnabled)
                Blend2DCheckEXT(ctx.clip_to_rect(BLRectI(scissorX, scissorY, scissorW, scissorH)), "BLContext::clip_to_rect");
            if (viewportSet)
                Blend2DCheckEXT(ctx.translate(static_cast<double>(viewportX), static_cast<double>(viewportY)), "BLContext::translate (viewport)");

            Blend2DCheckEXT(ctx.apply_transform(ToBlMatrixEXT(transformMatrix)), "BLContext::apply_transform");
            Blend2DCheckEXT(ctx.translate(destX, destY), "BLContext::translate");
            if (rotation != 0.0)
                Blend2DCheckEXT(ctx.rotate(rotation), "BLContext::rotate");

            if (effectiveFlipH)
            {
                const double centerX = rectX + rectW / 2.0;
                Blend2DCheckEXT(ctx.translate(centerX, 0.0), "BLContext::translate (flip H)");
                Blend2DCheckEXT(ctx.scale(-1.0, 1.0), "BLContext::scale (flip H)");
                Blend2DCheckEXT(ctx.translate(-centerX, 0.0), "BLContext::translate (flip H)");
            }
            if (effectiveFlipV)
            {
                const double centerY = rectY + rectH / 2.0;
                Blend2DCheckEXT(ctx.translate(0.0, centerY), "BLContext::translate (flip V)");
                Blend2DCheckEXT(ctx.scale(1.0, -1.0), "BLContext::scale (flip V)");
                Blend2DCheckEXT(ctx.translate(0.0, -centerY), "BLContext::translate (flip V)");
            }

            Blend2DCheckEXT(
                ctx.blit_image(BLRect(rectX, rectY, rectW, rectH), img, BLRectI(blitSrcX, blitSrcY, sw, sh)),
                "BLContext::blit_image");
        }

        /// Bounded CPU correction for BlendState::NonPremultiplied/Additive, which no single
        /// Blend2D BLCompOp can express: both presets set ColorSourceBlend=AlphaSourceBlend=
        /// SourceAlpha, so XNA evaluates colour and alpha from the same straight-space equation
        /// pair (matching SkiaRenderer.cpp's MaskedBlendEffect() runtime-blender oracle, the
        /// semantic source of truth here -- NOT native BLCompOp behaviour):
        ///   dstColor    = Da > 0 ? Dca/Da : 0                         (recover straight dest RGB)
        ///   NonPremul:  Cout = saturate(Sca + dstColor*(1-Sa))          Aout = saturate(Sa*Sa+Da*(1-Sa))
        ///   Additive:   Cout = saturate(Sca + dstColor)                 Aout = saturate(Sa*Sa+Da)
        ///   stored.rgb  = Cout * Aout                                   stored.a = Aout
        /// where Sca is the source's OWN premultiplied contribution (straight source colour times
        /// Sa) -- Skia's shader receives this directly as its already-premultiplied `src.rgb`
        /// input; this renderer recovers the per-pixel equivalent from the scratch pass below.
        /// Note this REPLACES both the colour and alpha bytes the real (native-BLCompOp) draw
        /// wrote -- native SRC_OVER/PLUS colour math premultiplies the blended straight colour
        /// against the OLD destination alpha (Da), but XNA's Cout must be premultiplied against
        /// the NEW alpha (Aout, which differs from Da whenever Sa is neither 0 nor 1), so the
        /// native draw's colour bytes are only a staging step, not the final answer, for these two
        /// presets specifically.
        ///
        /// Renders the identical transformed blit a SECOND time onto a fresh, fully transparent
        /// scratch canvas the same size as the active surface, using SRC_OVER: since the scratch
        /// destination starts at alpha=0, SRC_OVER's own equations Dca'=Sca+Dca*(1-Sa)=Sca and
        /// Da'=Sa+Da*(1-Sa)=Sa collapse to exactly the source's own premultiplied contribution and
        /// effective alpha -- so the scratch canvas IS both Sca and Sa as Blend2D's real
        /// rasterizer actually produced them for this draw, antialiased edge coverage/sampling/
        /// rotation/clipping all included -- reusing Blend2D's own rasterizer for the hard part
        /// rather than reimplementing it. Combined with @p beforeBytes (the destination's state
        /// BEFORE the real draw, snapshotted by the caller), the true per-pixel colour and alpha
        /// are computed and written back over every touched surface pixel.
        ///
        /// `Cout` is saturated to [0,1] BEFORE multiplying by `Aout` (matching MaskedBlendEffect's
        /// own `saturate(...)` placement) specifically so `stored.rgb = Cout*Aout <= Aout` holds by
        /// construction for every touched pixel -- Additive's colour weights (Sa + 1) do not sum to
        /// 1, so an unsaturated Cout can genuinely exceed 1.0 (a real, expected additive "blow out"
        /// XNA/FNA also clip), and premultiplying an unsaturated Cout would produce a PRGB32 pixel
        /// with an RGB channel greater than its own alpha, which is not a valid representation this
        /// renderer relies on anywhere else. `Blend2DPixelConvert.hpp`'s unpremultiply clamp is
        /// defensive hardening only, never load-bearing for renderer-owned output after this fix
        /// (see `Blend2D_Correctness::TestPrgb32Invariant`).
        ///
        /// Bounded to the whole active surface (same "simple and always correct for any
        /// rotation/clip" tradeoff DrawQuad's ColorWriteChannels merge below already accepts)
        /// rather than computing a tighter bounding box.
        void ApplyIndependentBlendCorrectionEXT(
            Blend2DSurface& activeSurface, const BLImage& img, const Matrix& transformMatrix,
            bool viewportSet, int viewportX, int viewportY, int viewportW, int viewportH,
            bool scissorEnabled, int scissorX, int scissorY, int scissorW, int scissorH, double destX,
            double destY, double rotation, bool effectiveFlipH, bool effectiveFlipV, double rectX,
            double rectY, double rectW, double rectH, int blitSrcX, int blitSrcY, int sw, int sh,
            BLPatternQuality patternQuality, const std::vector<std::uint8_t>& beforeBytes, int survW,
            int survH, std::ptrdiff_t beforeStride, bool additive)
        {
            BLImage scratch;
            Blend2DCheckEXT(scratch.create(survW, survH, BL_FORMAT_PRGB32),
                            "BLImage::create (blend-correction scratch)");
            {
                BLImageData sd{};
                Blend2DCheckEXT(scratch.make_mutable(&sd), "BLImage::make_mutable (blend-correction scratch zero)");
                auto* base = static_cast<std::uint8_t*>(sd.pixel_data);
                for (int row = 0; row < survH; ++row)
                {
                    std::memset(base + static_cast<std::ptrdiff_t>(row) * sd.stride, 0,
                               static_cast<std::size_t>(survW) * 4);
                }
            }

            BLContext scratchCtx;
            Blend2DCheckEXT(scratchCtx.begin(scratch), "BLContext::begin (blend-correction scratch)");
            Blend2DCheckEXT(scratchCtx.set_comp_op(BL_COMP_OP_SRC_OVER), "BLContext::set_comp_op (blend-correction scratch)");
            Blend2DCheckEXT(scratchCtx.set_pattern_quality(patternQuality),
                            "BLContext::set_pattern_quality (blend-correction scratch)");
            PlaceAndBlitEXT(scratchCtx, img, transformMatrix, viewportSet, viewportX, viewportY,
                           viewportW, viewportH, scissorEnabled, scissorX, scissorY, scissorW,
                           scissorH, destX, destY, rotation, effectiveFlipH, effectiveFlipV, rectX,
                           rectY, rectW, rectH, blitSrcX, blitSrcY, sw, sh);
            Blend2DCheckEXT(scratchCtx.end(), "BLContext::end (blend-correction scratch)");

            BLImageData scratchData{};
            Blend2DCheckEXT(scratch.get_data(&scratchData), "BLImage::get_data (blend-correction scratch readback)");
            BLImageData afterData{};
            Blend2DCheckEXT(activeSurface.Image().make_mutable(&afterData),
                            "BLImage::make_mutable (blend-correction write-back)");

            const auto* scratchBase = static_cast<const std::uint8_t*>(scratchData.pixel_data);
            auto* afterBase = static_cast<std::uint8_t*>(afterData.pixel_data);
            for (int row = 0; row < survH; ++row)
            {
                const std::uint8_t* scratchRow = scratchBase + static_cast<std::ptrdiff_t>(row) * scratchData.stride;
                const std::uint8_t* beforeRow = beforeBytes.data() + static_cast<std::ptrdiff_t>(row) * beforeStride;
                std::uint8_t* afterRow = afterBase + static_cast<std::ptrdiff_t>(row) * afterData.stride;
                for (int col = 0; col < survW; ++col)
                {
                    const std::size_t idx = static_cast<std::size_t>(col) * 4;
                    const double sa = scratchRow[idx + 3] / 255.0;
                    if (sa <= 0.0)
                        continue; // Untouched by this draw -- destination stays exactly as-is.

                    // BL_FORMAT_PRGB32 is BGRA byte order on this little-endian-only renderer.
                    const double scaB = scratchRow[idx + 0] / 255.0;
                    const double scaG = scratchRow[idx + 1] / 255.0;
                    const double scaR = scratchRow[idx + 2] / 255.0;

                    const double da = beforeRow[idx + 3] / 255.0;
                    const double dcaB = beforeRow[idx + 0] / 255.0;
                    const double dcaG = beforeRow[idx + 1] / 255.0;
                    const double dcaR = beforeRow[idx + 2] / 255.0;
                    const double dstB = da > 0.0 ? dcaB / da : 0.0;
                    const double dstG = da > 0.0 ? dcaG / da : 0.0;
                    const double dstR = da > 0.0 ? dcaR / da : 0.0;

                    const double aout = std::clamp(additive ? (sa * sa + da) : (sa * sa + da * (1.0 - sa)),
                                                   0.0, 1.0);
                    const double coutB = std::clamp(additive ? (scaB + dstB) : (scaB + dstB * (1.0 - sa)), 0.0, 1.0);
                    const double coutG = std::clamp(additive ? (scaG + dstG) : (scaG + dstG * (1.0 - sa)), 0.0, 1.0);
                    const double coutR = std::clamp(additive ? (scaR + dstR) : (scaR + dstR * (1.0 - sa)), 0.0, 1.0);

                    afterRow[idx + 3] = static_cast<std::uint8_t>(aout * 255.0 + 0.5);
                    afterRow[idx + 0] = static_cast<std::uint8_t>(coutB * aout * 255.0 + 0.5);
                    afterRow[idx + 1] = static_cast<std::uint8_t>(coutG * aout * 255.0 + 0.5);
                    afterRow[idx + 2] = static_cast<std::uint8_t>(coutR * aout * 255.0 + 0.5);
                }
            }
        }
    } // namespace

    void Blend2DSpriteBatchRenderer::Begin()
    {
        if (begun_)
            throw std::runtime_error("Blend2D SpriteBatch Begin() was called without a matching End().");
        begun_ = true;
    }

    void Blend2DSpriteBatchRenderer::End()
    {
        if (!begun_)
            throw std::runtime_error("Blend2D SpriteBatch End() was called without a matching Begin().");
        begun_ = false;
    }

    void Blend2DSpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        transformMatrix_ = m;
    }

    void Blend2DSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        // Blend2D has no shader/effect pipeline at all -- only null (restore the built-in sprite
        // path) or the exact stock SpriteEffect (which owns no renderer program of its own; every
        // other CNA renderer treats it identically to null) can be honoured. Exact type identity
        // matters: silently accepting a SpriteEffect subclass could discard an overridden
        // OnApply() the caller expects to run. Anything else -- a real custom shader Effect --
        // must fail loudly here rather than being accepted and then producing the built-in sprite
        // output while the caller believes their shader ran.
        if (effect && !effect->IsExactStockSpriteEffectEXT())
        {
            throw std::runtime_error(
                "BLEND2D does not support custom SpriteBatch Effects (no shader/effect pipeline); "
                "only the built-in sprite path (a null or exact stock SpriteEffect) is supported.");
        }
    }

    void Blend2DSpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        (void)SamplerFilterToPatternQualityEXT(textureFilter); // validated up front
        textureFilter_ = textureFilter;
    }

    void Blend2DSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        ValidateAddressModeEXT(addressU);
        ValidateAddressModeEXT(addressV);
        addressU_ = addressU;
        addressV_ = addressV;
    }

    void Blend2DSpriteBatchRenderer::SetImmediateMode(bool /*immediate*/)
    {
        // Every Draw() overload already executes synchronously against the active BLContext the
        // instant it is called -- DrawQuad has no internal batching/deferral of its own -- so
        // Immediate vs Deferred SpriteSortMode makes no observable difference here. An explicit,
        // audited no-op rather than a silently inherited one (ISpriteBatchRenderer::SetImmediateMode's
        // own doc comment).
    }

    void Blend2DSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        DrawQuad(texture, x, y, texture.GetWidth(), texture.GetHeight(), 0, 0, -1, -1,
                 Color(255, 255, 255, 255), 0.0, 0.0, 0.0, false, false);
    }

    void Blend2DSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                          const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle, const Color& color)
    {
        DrawQuad(texture, destinationRectangle.X, destinationRectangle.Y,
                 destinationRectangle.Width, destinationRectangle.Height, sourceRectangle.X,
                 sourceRectangle.Y, sourceRectangle.Width, sourceRectangle.Height, color, 0.0, 0.0,
                 0.0, false, false);
    }

    void Blend2DSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                          const Rectangle& destinationRectangle,
                                          const Rectangle& sourceRectangle, const Color& color,
                                          float rotation, const Vector2& origin,
                                          SpriteEffects effects, float /*layerDepth*/)
    {
        const bool flipH = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0;
        const bool flipV = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0;
        DrawQuad(texture, destinationRectangle.X, destinationRectangle.Y,
                 destinationRectangle.Width, destinationRectangle.Height, sourceRectangle.X,
                 sourceRectangle.Y, sourceRectangle.Width, sourceRectangle.Height, color,
                 static_cast<double>(rotation), origin.X, origin.Y, flipH, flipV);
    }

    void Blend2DSpriteBatchRenderer::DrawQuad(const ITextureRenderer& texture, double destX,
                                              double destY, double destW, double destH, int srcX,
                                              int srcY, int srcW, int srcH, const Color& color,
                                              double rotation, double originX, double originY,
                                              bool flipH, bool flipV)
    {
        if (!begun_)
            throw std::runtime_error("Blend2DSpriteBatchRenderer::Draw called before Begin().");

        // Checked before any native memory is touched (REMED cross-renderer safety).
        const auto& source = RequireNativeImageEXT(texture);

        int sx = srcX, sy = srcY, sw = srcW, sh = srcH;
        if (sw <= 0 || sh <= 0)
        {
            sx = 0;
            sy = 0;
            sw = texture.GetWidth();
            sh = texture.GetHeight();
        }
        if (sw <= 0 || sh <= 0 || destW == 0.0 || destH == 0.0)
            return;

        const double scaleX = destW / static_cast<double>(sw);
        const double scaleY = destH / static_cast<double>(sh);
        const double originDestX = originX * scaleX;
        const double originDestY = originY * scaleY;

        // The un-flipped destination box in local (post translate/rotate) space: it spans from
        // -originDestX to -originDestX + destW, which runs "backwards" (right-to-left) when destW
        // is negative -- a legitimate way to reach SpriteBatch (e.g. Draw(..., scale) with a
        // negative Vector2 scale component). Normalize to a canonical non-negative-size rect for
        // blit_image, folding that sign into an extra mirror exactly like SpriteEffects does below
        // (XOR-composed: two independent flip sources cancel out, matching FNA/XNA's own vertex
        // math, which flips SOURCE UV coordinates for SpriteEffects but flips destination-quad
        // GEOMETRY for a negative scale -- see SoftwareSpriteBatchRenderer::Draw for the reference
        // per-vertex derivation this mirrors).
        const double rectX = -originDestX + std::min(0.0, destW);
        const double rectY = -originDestY + std::min(0.0, destH);
        const double rectW = std::abs(destW);
        const double rectH = std::abs(destH);
        const bool effectiveFlipH = flipH != (destW < 0.0);
        const bool effectiveFlipV = flipV != (destH < 0.0);

        const bool isWhite = color.getRProperty() == 255 && color.getGProperty() == 255 &&
                              color.getBProperty() == 255 && color.getAProperty() == 255;

        const int texW = texture.GetWidth();
        const int texH = texture.GetHeight();
        const bool sourceInBounds = sx >= 0 && sy >= 0 && sx + sw <= texW && sy + sh <= texH;
        Blend2DSurface& activeSurface = renderer_.ActiveSurface();
        // Blend2D's synchronous-context guarantees do not cover drawing an image while that exact
        // image is the live target of the BLContext currently attached to it (see
        // Blend2DRenderTargetRenderer's own doc comment) -- resolve an independent snapshot rather
        // than assume source/destination aliasing is safe.
        const bool selfSampling = &source.NativeImageEXT() == &activeSurface.Image();
        const bool needsResolve = !sourceInBounds || selfSampling || !isWhite;

        BLImage resolvedImage;
        const BLImage* img = &source.NativeImageEXT();
        int blitSrcX = sx, blitSrcY = sy;
        if (needsResolve)
        {
            resolvedImage = ResolveSourceImageEXT(*img, texW, texH, sx, sy, sw, sh, addressU_,
                                                  addressV_, color, !isWhite);
            img = &resolvedImage;
            blitSrcX = 0;
            blitSrcY = 0;
        }

        BLContext& ctx = activeSurface.Context();
        Blend2DCheckEXT(ctx.set_comp_op(renderer_.ActiveCompOp()), "BLContext::set_comp_op");
        const BLPatternQuality patternQuality = SamplerFilterToPatternQualityEXT(textureFilter_);
        Blend2DCheckEXT(ctx.set_pattern_quality(patternQuality), "BLContext::set_pattern_quality");

        const int colorWriteMask = renderer_.ActiveColorWriteMaskEXT();
        const bool needsWriteMask = colorWriteMask != 15;
        const Blend2DBlendPresetEXT activePreset = renderer_.ActiveBlendPresetEXT();
        const bool needsBlendCorrection = activePreset == Blend2DBlendPresetEXT::NonPremultiplied ||
                                          activePreset == Blend2DBlendPresetEXT::Additive;

        // Blend2D has no native per-channel colour write mask, and no single BLCompOp can express
        // NonPremultiplied/Additive's independent colour+alpha equations (see
        // ApplyIndependentBlendCorrectionEXT's doc comment) -- both are realized as bounded
        // post-processes that need the destination's state from BEFORE the real draw, so it is
        // snapshotted up front for whichever (or both) is needed. Bounded to the whole surface
        // (simple and always correct for any rotation/clip), acceptable since neither is the
        // fast/common path (mask==15 with AlphaBlend/Opaque is).
        const bool needsBeforeSnapshot = needsWriteMask || needsBlendCorrection;
        std::vector<std::uint8_t> beforeBytes;
        BLImageData beforeData{};
        int survW = 0, survH = 0;
        if (needsBeforeSnapshot)
        {
            survW = activeSurface.Width();
            survH = activeSurface.Height();
            Blend2DCheckEXT(activeSurface.Image().get_data(&beforeData), "BLImage::get_data (pre-draw snapshot)");
            beforeBytes.resize(static_cast<std::size_t>(survH) * static_cast<std::size_t>(beforeData.stride));
            std::memcpy(beforeBytes.data(), beforeData.pixel_data, beforeBytes.size());
        }

        // Viewport and scissor clips are TARGET-space state: read once and apply before any
        // per-sprite or Begin() transform is installed, so their coordinates are never
        // reinterpreted as sprite-local (REMED-GFX scissor/viewport audit). Re-read fresh from the
        // renderer on every draw so they reflect the current SetViewport/ScissorTestEnable/
        // SetScissorRect state exactly, on whichever BLContext is active right now -- neither can
        // leak stale state across a render-target switch or an intervening state change the way a
        // persistently-applied clip would. Captured once here (rather than inside PlaceAndBlitEXT)
        // so the real draw and, when needed, ApplyIndependentBlendCorrectionEXT's auxiliary pass
        // apply byte-identical clipping.
        bool viewportSet = false;
        int viewportX = 0, viewportY = 0, viewportW = 0, viewportH = 0;
        renderer_.GetViewportStateEXT(viewportSet, viewportX, viewportY, viewportW, viewportH);
        bool scissorEnabled = false;
        int scissorX = 0, scissorY = 0, scissorW = 0, scissorH = 0;
        renderer_.GetScissorStateEXT(scissorEnabled, scissorX, scissorY, scissorW, scissorH);

        PlaceAndBlitEXT(ctx, *img, transformMatrix_, viewportSet, viewportX, viewportY, viewportW,
                        viewportH, scissorEnabled, scissorX, scissorY, scissorW, scissorH, destX,
                        destY, rotation, effectiveFlipH, effectiveFlipV, rectX, rectY, rectW, rectH,
                        blitSrcX, blitSrcY, sw, sh);

        if (needsBlendCorrection)
        {
            ApplyIndependentBlendCorrectionEXT(
                activeSurface, *img, transformMatrix_, viewportSet, viewportX, viewportY, viewportW,
                viewportH, scissorEnabled, scissorX, scissorY, scissorW, scissorH, destX, destY,
                rotation, effectiveFlipH, effectiveFlipV, rectX, rectY, rectW, rectH, blitSrcX,
                blitSrcY, sw, sh, patternQuality, beforeBytes, survW, survH, beforeData.stride,
                activePreset == Blend2DBlendPresetEXT::Additive);
        }

        if (needsWriteMask)
        {
            BLImageData afterData{};
            Blend2DCheckEXT(activeSurface.Image().make_mutable(&afterData), "BLImage::make_mutable (write-mask merge)");
            auto* afterBase = static_cast<std::uint8_t*>(afterData.pixel_data);
            for (int row = 0; row < survH; ++row)
            {
                std::uint8_t* afterRow = afterBase + static_cast<std::ptrdiff_t>(row) * afterData.stride;
                const std::uint8_t* beforeRow = beforeBytes.data() + static_cast<std::ptrdiff_t>(row) * beforeData.stride;
                for (int col = 0; col < survW; ++col)
                {
                    // BL_FORMAT_PRGB32 stores BGRA byte order on this little-endian-only renderer
                    // (see Blend2DPixelConvert.hpp).
                    const std::size_t idx = static_cast<std::size_t>(col) * 4;
                    if (!ColorWriteHasBlue(colorWriteMask))  afterRow[idx + 0] = beforeRow[idx + 0];
                    if (!ColorWriteHasGreen(colorWriteMask)) afterRow[idx + 1] = beforeRow[idx + 1];
                    if (!ColorWriteHasRed(colorWriteMask))   afterRow[idx + 2] = beforeRow[idx + 2];
                    if (!ColorWriteHasAlpha(colorWriteMask)) afterRow[idx + 3] = beforeRow[idx + 3];
                }
            }
        }
    }
} // namespace CNA::Internal::Renderers::Blend2D
