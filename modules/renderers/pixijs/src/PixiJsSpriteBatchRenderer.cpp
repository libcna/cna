#include "CNA/Internal/Renderers/PixiJs/PixiJsSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/PixiJs/PixiJsTextureRenderer.hpp"
#include "CNA/Internal/Renderers/PixiJs/PixiJsRenderTargetRenderer.hpp"

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"

#include "System/NotSupportedException.hpp"

#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plans/plan_pixijs.md Design decisions 5/7: one EM_JS call per SpriteBatch::End() flush, walking a
// packed DrawCommand array (14 int32-words/command, matching CanvasSpriteBatchRenderer's own
// word-indexed HEAP32/HEAPF32 walk) and driving a pooled array of PIXI.Sprite objects through
// their native anchor/position/scale/rotation/tint/alpha properties -- no manual transform math.
//
// PIXIJS-87: this call also COMMITS. It fills the scratch container from the pool, renders that
// container into the active target with `clear:false`, and empties it again -- see
// PixiJsRenderer.cpp's own submission-model comment for why a retained pool parented to the
// active container was unsound. Everything the batch's state affects (blend factors, the constant
// blend colour, the colour write mask, the sampled textures' sampler state) is therefore applied
// to the GL context immediately before the one render that consumes it, and can never be
// retroactively rewritten by a later batch.
//
// Per-command word layout (all 4-byte fields, stride=14):
//   0 textureId (int32)          1 sourceX (int32)        2 sourceY (int32)
//   3 sourceWidth (int32)        4 sourceHeight (int32)
//   5 destinationX (float32)     6 destinationY (float32)
//   7 destinationWidth (float32) 8 destinationHeight (float32)
//   9 rotation (float32)         10 originX (float32)      11 originY (float32)
//   12 flags (int32, bit0=flipH bit1=flipV)  13 packedColor (uint32, R|G<<8|B<<16|A<<24)
//
// plans/plan_pixijs.md PIXIJS-45: transformA/B/C/D/tx/ty are the batch's Begin(transformMatrix), applied
// AFTER each sprite's own local placement matrix (position/rotation/scale) -- matching FNA's own
// SpriteEffect vertex shader, which multiplies the per-sprite local quad by this matrix as a
// separate world/view step.
EM_JS(int, CNA_PixiJs_FlushSprites, (const void* commands, int count, int stride,
                                      int opaqueNoBlend, int wrapMode, int scaleMode,
                                      float transformA, float transformB, float transformC, float transformD,
                                      float transformTx, float transformTy,
                                      int blendSrcRGB, int blendDstRGB,
                                      int blendSrcAlpha, int blendDstAlpha,
                                      int blendEquationRGB, int blendEquationAlpha,
                                      float blendFactorR, float blendFactorG,
                                      float blendFactorB, float blendFactorA,
                                      int colorWriteChannels), {
    const state = Module['cnaPixi'];
    if (!state || !state.app) return 0;
    try {
        const app = state.app;
        const gl = state.gl;
        const pool = state.pool;
        const scratch = state.scratch;
        const textures = state.textures;
        const isIdentityTransform = transformA === 1 && transformB === 0 && transformC === 0 &&
                                     transformD === 1 && transformTx === 0 && transformTy === 0;

        // PIXIJS-87: a DISTINCT slot per distinct factor tuple. PixiJS's StateSystem skips
        // setBlendMode() when the incoming id equals the one already set, so mutating a single
        // reserved slot's factors would leave a later batch rendering with the PREVIOUS batch's
        // blend -- confirmed empirically in a real browser before this was written. Opaque takes
        // PixiJS's own BLEND_MODES.NONE instead: (ONE, ZERO) is arithmetically identical to no
        // blending at all, and NONE is the path PixiJS optimizes.
        let resolvedBlendMode;
        if (opaqueNoBlend) {
            resolvedBlendMode = PIXI.BLEND_MODES.NONE;
        } else {
            const key = blendSrcRGB + ',' + blendDstRGB + ',' + blendSrcAlpha + ',' + blendDstAlpha +
                        ',' + blendEquationRGB + ',' + blendEquationAlpha;
            let slot = state.blendSlots[key];
            if (slot === undefined) {
                slot = app.renderer.state.blendModes.length;
                app.renderer.state.blendModes[slot] = [blendSrcRGB, blendDstRGB, blendSrcAlpha,
                                                        blendDstAlpha, blendEquationRGB, blendEquationAlpha];
                // PIXIJS-51: PixiJS rewrites a sprite's blend mode through
                // utils.premultiplyBlendMode[isPremultiplied][mode] before applying it, which is
                // how BLEND_MODES.NORMAL silently became NORMAL_NPM -- a DIFFERENT factor tuple --
                // for every non-premultiplied texture, making BlendState::AlphaBlend render as
                // BlendState::NonPremultiplied. Registering an identity entry for each of this
                // renderer's own slots is what keeps the literal XNA factors literal. It is also
                // required for correctness once a build allocates more than 32 slots, where the
                // stock table has no entry at all and the lookup would yield undefined.
                PIXI.utils.premultiplyBlendMode[0][slot] = slot;
                PIXI.utils.premultiplyBlendMode[1][slot] = slot;
                state.blendSlots[key] = slot;
            }
            resolvedBlendMode = slot;
        }

        const base = commands >> 2;
        let used = 0;
        for (let i = 0; i < count; ++i) {
            const o = base + i * stride;
            const textureId      = HEAP32[o + 0];
            const sourceX        = HEAP32[o + 1];
            const sourceY        = HEAP32[o + 2];
            const sourceWidth    = HEAP32[o + 3];
            const sourceHeight   = HEAP32[o + 4];
            const destinationX      = HEAPF32[o + 5];
            const destinationY      = HEAPF32[o + 6];
            const destinationWidth  = HEAPF32[o + 7];
            const destinationHeight = HEAPF32[o + 8];
            const rotation = HEAPF32[o + 9];
            const originX  = HEAPF32[o + 10];
            const originY  = HEAPF32[o + 11];
            const flags = HEAP32[o + 12];
            const color = HEAPU32[o + 13];

            const entry = textures[textureId];
            if (!entry) continue;

            // Sampler state belongs to the GPU texture object, and this flush is rasterized before
            // the call returns, so applying it here genuinely scopes it to THIS batch: a later
            // batch drawing the same texture with a different SamplerState sets it again before its
            // own render. dirtyStyleId is the same mechanism PIXI.BaseTexture.setStyle uses -- it
            // re-applies the sampler parameters at bind time without re-uploading the pixels, which
            // an update() call would have forced.
            const drawnBaseTexture = entry.texture.baseTexture || entry.texture;
            if (drawnBaseTexture.wrapMode !== wrapMode || drawnBaseTexture.scaleMode !== scaleMode) {
                drawnBaseTexture.wrapMode = wrapMode;
                drawnBaseTexture.scaleMode = scaleMode;
                drawnBaseTexture.dirtyStyleId++;
            }

            let sprite = pool[used];
            if (!sprite) {
                sprite = new PIXI.Sprite();
                pool[used] = sprite;
            }
            ++used;

            // plans/plan_pixijs.md PIXIJS-44 / REMED-PIXIJS-2: flip via the texture's own GroupD8
            // `rotate` (which texel samples which corner), never via negative sprite.scale.
            // Empirically verified: negative scale composed with an off-center anchor visibly
            // SHIFTS the sprite's on-screen footprint instead of mirroring content in place.
            // GroupD8 values, also confirmed empirically: 12 = horizontal mirror, 8 = vertical,
            // 4 = both -- all three leave the destination rectangle exactly where
            // destinationX/Y/Width/Height says it should be, matching XNA's real contract.
            const flipH = (flags & 1) !== 0;
            const flipV = (flags & 2) !== 0;
            const pixiRotate = (flipH ? 12 : 0) ^ (flipV ? 8 : 0);

            // PIXIJS-94: a PIXI.Texture "view" per (frame, rotate), CACHED on the registry entry
            // rather than constructed fresh for every draw of every frame. A view is a cheap
            // descriptor over the entry's shared baseTexture, but it is still a real object with
            // event wiring, and allocating one per sprite per frame was exactly the GC pressure
            // Design decision 7's sprite pooling exists to avoid. The cache is owned by the entry
            // and dies with it, so a view can never outlive the base texture it describes.
            const viewKey = sourceX + ',' + sourceY + ',' + sourceWidth + ',' + sourceHeight + ',' + pixiRotate;
            let view = entry.views[viewKey];
            if (!view) {
                view = new PIXI.Texture(drawnBaseTexture,
                                        new PIXI.Rectangle(sourceX, sourceY, sourceWidth, sourceHeight),
                                        undefined, undefined, pixiRotate);
                entry.views[viewKey] = view;
            }
            sprite.texture = view;

            // plans/plan_pixijs.md PIXIJS-43: anchor is PixiJS's own normalized (0..1 of the frame) pivot
            // -- XNA's origin is source-pixel space, so divide through by the source rectangle.
            sprite.anchor.set(sourceWidth ? originX / sourceWidth : 0, sourceHeight ? originY / sourceHeight : 0);

            const scaleX = sourceWidth ? destinationWidth / sourceWidth : 1;
            const scaleY = sourceHeight ? destinationHeight / sourceHeight : 1;
            if (isIdentityTransform) {
                sprite.position.set(destinationX, destinationY);
                sprite.scale.set(scaleX, scaleY);
                sprite.rotation = rotation;
                sprite.skew.set(0, 0);
            } else {
                // plans/plan_pixijs.md PIXIJS-45: compose the batch's transform with this sprite's own
                // local placement matrix (translate*rotate*scale, the same matrix PixiJS's own
                // updateLocalTransform would build), then let PIXI.Transform decompose the result
                // rather than re-deriving rotation/scale/skew by hand.
                const cosR = Math.cos(rotation), sinR = Math.sin(rotation);
                const a2 = cosR * scaleX, b2 = sinR * scaleX;
                const c2 = -sinR * scaleY, d2 = cosR * scaleY;
                const tx2 = destinationX, ty2 = destinationY;
                const ca = transformA * a2 + transformC * b2;
                const cb = transformB * a2 + transformD * b2;
                const cc = transformA * c2 + transformC * d2;
                const cd = transformB * c2 + transformD * d2;
                const ctx = transformA * tx2 + transformC * ty2 + transformTx;
                const cty = transformB * tx2 + transformD * ty2 + transformTy;
                sprite.transform.setFromMatrix(new PIXI.Matrix(ca, cb, cc, cd, ctx, cty));
            }

            // plans/plan_pixijs.md PIXIJS-42: sprite.tint is RGB-only; alpha is sprite.alpha. Because
            // every texture uploads with ALPHA_MODES.NPM, PixiJS packs a STRAIGHT vertex colour
            // (tint untouched, alpha in the high byte) rather than premultiplying it -- which is
            // exactly XNA's own straight tint semantics.
            const r = color & 0xFF, g = (color >>> 8) & 0xFF, b = (color >>> 16) & 0xFF, a = (color >>> 24) & 0xFF;
            sprite.tint = (r << 16) | (g << 8) | b;
            sprite.alpha = a / 255;
            sprite.blendMode = resolvedBlendMode;
            sprite.visible = true;
            scratch.addChild(sprite);
        }

        if (used > 0) {
            // PIXIJS-88/89: the constant blend colour and the colour write mask are plain GL state
            // that PixiJS neither owns nor resets, so setting them immediately before the render
            // scopes them to exactly this batch.
            gl.blendColor(blendFactorR, blendFactorG, blendFactorB, blendFactorA);
            gl.colorMask((colorWriteChannels & 1) !== 0, (colorWriteChannels & 2) !== 0,
                         (colorWriteChannels & 4) !== 0, (colorWriteChannels & 8) !== 0);
            if (state.activeTarget)
                app.renderer.render(scratch, { renderTexture: state.activeTarget, clear: false });
            else
                app.renderer.render(scratch, { clear: false });
            gl.colorMask(true, true, true, true);
        }
        // Emptied unconditionally: a sprite that stayed parented here would be re-rendered by the
        // next flush, which is the retained-mode duplication this model exists to remove.
        scratch.removeChildren();
        return 1;
    } catch (e) {
        console.error('[CNA] PixiJS: SpriteBatch flush failed:', e);
        // Leave nothing parented behind: the next flush must not inherit this one's sprites.
        try { state.scratch.removeChildren(); } catch (ignored) { /* the scene graph is unusable */ }
        return 0;
    }
});
#endif

namespace CNA::Internal::Renderers::PixiJs
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    namespace
    {
        int PixiTextureIdOf(const ITextureRenderer& texture)
        {
            if (const auto* t = dynamic_cast<const PixiJsTextureRenderer*>(&texture)) return t->GetPixiTextureId();
            if (const auto* rt = dynamic_cast<const PixiJsRenderTargetRenderer*>(&texture)) return rt->GetPixiTextureId();
            return 0;
        }
    }

    PixiJsSpriteBatchRenderer::PixiJsSpriteBatchRenderer()
        : state_(std::make_shared<PixiJsRendererState>())
    {
    }

    PixiJsSpriteBatchRenderer::PixiJsSpriteBatchRenderer(std::shared_ptr<PixiJsRendererState> state)
        : state_(std::move(state))
    {
    }

    void PixiJsSpriteBatchRenderer::Begin()
    {
        commands_.clear();
        // PIXIJS-87/88/89: the whole graphics state a flush needs is captured HERE, at Begin(),
        // not read from the shared renderer state at flush time. SpriteBatch::Begin applies the
        // BlendState to the device immediately before calling this, so what is captured is exactly
        // the state this batch was begun with -- a later batch changing the device's BlendState,
        // BlendFactor or ColorWriteChannels can no longer reach back into this one.
        activeBlendMode_ = state_->blendMode;
        blendSrcRGB_ = state_->blendSrcRGB;
        blendDstRGB_ = state_->blendDstRGB;
        blendSrcAlpha_ = state_->blendSrcAlpha;
        blendDstAlpha_ = state_->blendDstAlpha;
        blendEquationRGB_ = state_->blendEquationRGB;
        blendEquationAlpha_ = state_->blendEquationAlpha;
        blendFactorR_ = state_->blendFactorR;
        blendFactorG_ = state_->blendFactorG;
        blendFactorB_ = state_->blendFactorB;
        blendFactorA_ = state_->blendFactorA;
        colorWriteChannels_ = state_->colorWriteChannels;
        begun_ = true;
    }

    void PixiJsSpriteBatchRenderer::Flush(const DrawCommand* commands, int count)
    {
#if defined(__EMSCRIPTEN__)
        if (count <= 0) return;
        const int pixiWrapMode = TextureAddressModeToPixiWrapMode(addressU_);
        const int pixiScaleMode = linearFilter_ ? 1 : 0; // PIXI.SCALE_MODES.LINEAR/.NEAREST
        const int opaqueNoBlend = activeBlendMode_ == PixiJsBlendMode::Opaque ? 1 : 0;
        if (CNA_PixiJs_FlushSprites(commands, count, 14,
                                    opaqueNoBlend, pixiWrapMode, pixiScaleMode,
                                    transformA_, transformB_, transformC_, transformD_,
                                    transformTx_, transformTy_,
                                    blendSrcRGB_, blendDstRGB_, blendSrcAlpha_, blendDstAlpha_,
                                    blendEquationRGB_, blendEquationAlpha_,
                                    blendFactorR_, blendFactorG_, blendFactorB_, blendFactorA_,
                                    colorWriteChannels_) == 0)
            throw std::runtime_error(
                "PixiJS: SpriteBatch flush failed; see the browser console for the underlying error.");
#else
        (void)commands; (void)count;
#endif
    }

    void PixiJsSpriteBatchRenderer::End()
    {
        Flush(commands_.data(), static_cast<int>(commands_.size()));
        commands_.clear();
        begun_ = false;
    }

    void PixiJsSpriteBatchRenderer::SetImmediateMode(bool immediate)
    {
        immediateMode_ = immediate;
    }

    void PixiJsSpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        // plans/plan_pixijs.md PIXIJS-45: SpriteBatch's transformMatrix is always a 2D affine map in this
        // v1 scope (matching FNA's own SpriteEffect vertex shader, which only ever receives a 2D
        // camera/view matrix here) -- only the upper-left 2x2 and the XY translation row matter.
        transformA_ = m.M11;
        transformB_ = m.M12;
        transformC_ = m.M21;
        transformD_ = m.M22;
        transformTx_ = m.M41;
        transformTy_ = m.M42;
    }

    void PixiJsSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        // plans/plan_pixijs.md PIXIJS-47/Design decision 10.
        if (effect != nullptr)
            throw std::runtime_error(
                "PixiJS (v1 scope) does not support a custom Effect passed to SpriteBatch::Begin() "
                "yet -- plans/plan_pixijs.md Design decision 10 (PixiJS has a real shader stage, unlike "
                "Canvas2D/DOM, but mapping CNA's Effect model onto it is out of v1 scope).");
    }

    void PixiJsSpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        // plans/plan_pixijs.md PIXIJS-53. Throws for a value outside the enumeration rather than
        // defaulting, so an unrecognized filter is never silently rendered as Point.
        linearFilter_ = TextureFilterIsLinear(textureFilter);
    }

    void PixiJsSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        // PIXIJS-90: PIXI.BaseTexture carries ONE wrapMode covering both axes, so a mixed
        // per-axis request genuinely cannot be represented. It used to be stored and then
        // half-applied (addressU won, addressV was dropped), which rendered a sampler state the
        // caller never asked for while reporting success. Rejecting says so.
        //
        // Both arguments are validated before the comparison, so an out-of-range value is reported
        // as such rather than as a mismatch.
        const int wrapU = TextureAddressModeToPixiWrapMode(addressU);
        const int wrapV = TextureAddressModeToPixiWrapMode(addressV);
        if (wrapU != wrapV)
            throw System::NotSupportedException(
                "PixiJS: SamplerState.AddressU and AddressV differ (" + std::to_string(addressU) +
                " vs " + std::to_string(addressV) + "). A PIXI.BaseTexture exposes a single "
                "wrapMode for both axes, so independent per-axis addressing cannot be expressed by "
                "this renderer. Use the same TextureAddressMode for both axes.");
        addressU_ = addressU;
        addressV_ = addressV;
    }

    void PixiJsSpriteBatchRenderer::QueueOrDraw(const ITextureRenderer& texture,
                                                const Rectangle& destinationRectangle,
                                                const Rectangle& sourceRectangle,
                                                const Color& color,
                                                float rotation,
                                                const Vector2& origin,
                                                SpriteEffects effects)
    {
        const int id = PixiTextureIdOf(texture);
        if (id == 0) return;

        const bool flipH =
            (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0;
        const bool flipV =
            (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0;
        const std::uint32_t packedColor =
            static_cast<std::uint32_t>(color.getRProperty()) |
            (static_cast<std::uint32_t>(color.getGProperty()) << 8) |
            (static_cast<std::uint32_t>(color.getBProperty()) << 16) |
            (static_cast<std::uint32_t>(color.getAProperty()) << 24);

        const DrawCommand command{
            id,
            sourceRectangle.X, sourceRectangle.Y, sourceRectangle.Width, sourceRectangle.Height,
            static_cast<float>(destinationRectangle.X),
            static_cast<float>(destinationRectangle.Y),
            static_cast<float>(destinationRectangle.Width),
            static_cast<float>(destinationRectangle.Height),
            rotation, origin.X, origin.Y,
            (flipH ? 1 : 0) | (flipV ? 2 : 0),
            packedColor
        };

        if (immediateMode_)
        {
            // SpriteSortMode::Immediate: this ONE sprite is rasterized before Draw() returns, which
            // is what makes immediate mode observably immediate. It used to overwrite pool[0] and
            // paint nothing, so a batch of N immediate draws left only the last sprite visible.
            Flush(&command, 1);
            return;
        }
        commands_.push_back(command);
    }

    void PixiJsSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        QueueOrDraw(texture,
                    Rectangle(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()),
                    Rectangle(0, 0, texture.GetWidth(), texture.GetHeight()),
                    Color::White, 0.0f, Vector2::Zero, SpriteEffects::None);
    }

    void PixiJsSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color)
    {
        QueueOrDraw(texture, destinationRectangle, sourceRectangle, color,
                    0.0f, Vector2::Zero, SpriteEffects::None);
    }

    void PixiJsSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color,
                                         float rotation,
                                         const Vector2& origin,
                                         SpriteEffects effects,
                                         float /*layerDepth*/)
    {
        QueueOrDraw(texture, destinationRectangle, sourceRectangle, color, rotation, origin, effects);
    }
}
