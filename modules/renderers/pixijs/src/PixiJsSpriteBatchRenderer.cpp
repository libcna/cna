#include "CNA/Internal/Renderers/PixiJs/PixiJsSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/PixiJs/PixiJsTextureRenderer.hpp"
#include "CNA/Internal/Renderers/PixiJs/PixiJsRenderTargetRenderer.hpp"

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"

#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_pixijs.md Design decisions 5/7: one EM_JS call per SpriteBatch::End() flush, walking a
// packed DrawCommand array (14 int32-words/command, matching CanvasSpriteBatchRenderer's own
// word-indexed HEAP32/HEAPF32 walk) and updating a pooled array of PIXI.Sprite objects via their
// native anchor/position/scale/rotation/tint/alpha properties -- no manual transform-stack math.
//
// Per-command word layout (all 4-byte fields, stride=14):
//   0 textureId (int32)          1 sourceX (int32)        2 sourceY (int32)
//   3 sourceWidth (int32)        4 sourceHeight (int32)
//   5 destinationX (float32)     6 destinationY (float32)
//   7 destinationWidth (float32) 8 destinationHeight (float32)
//   9 rotation (float32)         10 originX (float32)      11 originY (float32)
//   12 flags (int32, bit0=flipH bit1=flipV)  13 packedColor (uint32, R|G<<8|B<<16|A<<24)
// plan_pixijs.md PIXIJS-46/PIXIJS-53: wrapMode/scaleMode are real PIXI.WRAP_MODES/SCALE_MODES
// numeric (GL) values, applied to each drawn texture's shared baseTexture -- these are
// SpriteBatch-level sampler state (set once per Begin()/SetSampler* call, same as XNA's own
// SamplerState application), not per-command, so every sprite in one flush gets the same value.
// A baseTexture's wrapMode/scaleMode are properties of the GPU texture OBJECT, not of any one
// PIXI.Texture "view" onto it (Design decision 8's own per-draw frame views all share one
// baseTexture) -- setting them here is genuinely global to that texture, matching how a real
// WebGL sampler parameter would behave too.
// plan_pixijs.md PIXIJS-45: transformA/B/C/D/tx/ty are the batch's Begin(transformMatrix), applied
// AFTER each sprite's own local placement matrix (position/rotation/scale) -- matching FNA's own
// SpriteEffect vertex shader, which multiplies the per-sprite local quad by this matrix as a
// separate world/view step. Composition and PIXI.Transform.setFromMatrix's own decomposition back
// into position/scale/rotation/skew were confirmed correct via a standalone browser probe (identity,
// pure translation, and pure scale cases all landed at the exact expected pixel bounding box) before
// this code was written.
EM_JS(void, CNA_PixiJs_FlushSprites, (const void* commands, int count, int stride, int blendMode, int wrapMode, int scaleMode,
                                       float transformA, float transformB, float transformC, float transformD,
                                       float transformTx, float transformTy), {
    const isIdentityTransform = transformA === 1 && transformB === 0 && transformC === 0 &&
                                 transformD === 1 && transformTx === 0 && transformTy === 0;
    const app = Module['cnaPixiApp'];
    if (!app) return;
    if (!Module['cnaPixiSpritePool']) Module['cnaPixiSpritePool'] = [];
    const pool = Module['cnaPixiSpritePool'];
    const container = Module['cnaPixiActiveContainer'] || app.stage;
    const textures = Module['cnaPixiTextures'] || {};

    const base = commands >> 2;
    for (let i = 0; i < count; ++i) {
        const o = base + i * stride;
        const textureId     = HEAP32[o + 0];
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

        const drawnBaseTexture = entry.texture.baseTexture || entry.texture;
        if (drawnBaseTexture.wrapMode !== wrapMode) drawnBaseTexture.wrapMode = wrapMode;
        if (drawnBaseTexture.scaleMode !== scaleMode) { drawnBaseTexture.scaleMode = scaleMode; drawnBaseTexture.update(); }

        let sprite = pool[i];
        if (!sprite) {
            sprite = new PIXI.Sprite();
            pool[i] = sprite;
        }
        if (sprite.parent !== container) container.addChild(sprite);

        // plan_pixijs.md PIXIJS-44 / REMED-PIXIJS-2: flip via the texture's own GroupD8 `rotate` (mirrors WHICH
        // texel samples which corner), never via negative sprite.scale. Empirically verified
        // (2026-08-17, live browser probe against known RGBY texel data, independent of this
        // build): negative scale composed with an off-center anchor visibly SHIFTS the sprite's
        // on-screen footprint instead of mirroring content in place (anchor=(0,0), scale=(-4,4)
        // moved a destRect(20,8,8,8) draw's visible content to x=[12,20) instead of leaving it at
        // x=[20,28) -- a real bug the original, unverified "negative scale" design got wrong).
        // GroupD8 rotate values, also confirmed empirically against the same fixture (E=0 is
        // identity): 12 = pure horizontal mirror, 8 = pure vertical mirror, 4 = both (== 12 XOR 8)
        // -- all three leave the destination rectangle exactly where destinationX/Y/Width/Height
        // says it should be, matching XNA's real contract (flip changes sampling, never position).
        const flipH = (flags & 1) !== 0;
        const flipV = (flags & 2) !== 0;
        const pixiRotate = (flipH ? 12 : 0) ^ (flipV ? 8 : 0);

        // A fresh PIXI.Texture "view" per draw, sharing the entry's baseTexture/GPU resource but
        // carrying its own frame rectangle -- mutating a shared texture's own .frame in place would
        // corrupt every other sprite currently sampling the same atlas with a different sub-rect.
        sprite.texture = new PIXI.Texture(drawnBaseTexture, new PIXI.Rectangle(sourceX, sourceY, sourceWidth, sourceHeight), undefined, undefined, pixiRotate);

        // plan_pixijs.md PIXIJS-43: anchor is PixiJS's own normalized (0..1 of the frame) pivot --
        // XNA's origin is source-pixel space, so divide through by the source rectangle's own size.
        // Anchor is a vertex-generation-time pivot, independent of the transform below (matches
        // FNA's own origin subtraction happening before the transformMatrix multiply).
        sprite.anchor.set(sourceWidth ? originX / sourceWidth : 0, sourceHeight ? originY / sourceHeight : 0);

        const scaleX = sourceWidth ? destinationWidth / sourceWidth : 1;
        const scaleY = sourceHeight ? destinationHeight / sourceHeight : 1;
        if (isIdentityTransform) {
            // Fast path: byte-identical to this renderer's pre-PIXIJS-45 behavior.
            sprite.position.set(destinationX, destinationY);
            sprite.scale.set(scaleX, scaleY);
            sprite.rotation = rotation;
        } else {
            // plan_pixijs.md PIXIJS-45: compose the batch's transform (a1,b1,c1,d1,tx1,ty1) with this
            // sprite's own local placement matrix (a2,b2,c2,d2,tx2,ty2 -- translate*rotate*scale, the
            // same matrix PixiJS's own updateLocalTransform would build from position/rotation/scale),
            // then hand the combined 2x3 affine matrix to PIXI.Transform's own decomposition rather
            // than re-deriving rotation/scale/skew by hand.
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

        // plan_pixijs.md PIXIJS-42: native sprite.tint is RGB-only (same split CANVAS-32 already
        // has for Canvas2D) -- alpha is the separate sprite.alpha property.
        const r = color & 0xFF, g = (color >>> 8) & 0xFF, b = (color >>> 16) & 0xFF, a = (color >>> 24) & 0xFF;
        sprite.tint = (r << 16) | (g << 8) | b;
        sprite.alpha = a / 255;
        sprite.blendMode = blendMode;
        sprite.visible = true;
    }
    // Hide pooled sprites left over from an earlier flush within the same frame (Clear() already
    // removes every child on a fresh frame -- this only matters for a second Begin/End before the
    // next Clear()).
    for (let i = count; i < pool.length; ++i) {
        if (pool[i] && pool[i].parent === container) pool[i].visible = false;
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

        // REMED-PIXIJS-3: single source of truth for the PixiJsBlendMode -> real PIXI.BLEND_MODES
        // numeric value mapping, matching CNA_PixiJs_SetBlendMode's own table exactly (PixiJsRenderer.cpp)
        // -- kept as one function rather than duplicated inline so the two never drift apart.
        int PixiBlendModeToPixiJsCode(PixiJsBlendMode mode)
        {
            switch (mode)
            {
                case PixiJsBlendMode::Opaque: return 20; // PIXI.BLEND_MODES.NONE
                case PixiJsBlendMode::Additive: return 1; // PIXI.BLEND_MODES.ADD
                case PixiJsBlendMode::AlphaBlend:
                case PixiJsBlendMode::NonPremultiplied:
                default: return 0; // PIXI.BLEND_MODES.NORMAL
            }
        }

        // plan_pixijs.md PIXIJS-46: raw TextureAddressMode int (0=Wrap, 1=Clamp, 2=Mirror) -> real
        // WebGL wrap-mode GL enum values PIXI.WRAP_MODES exposes directly (confirmed live,
        // 2026-08-17: PIXI.WRAP_MODES.CLAMP===33071/CLAMP_TO_EDGE, .REPEAT===10497,
        // .MIRRORED_REPEAT===33648 -- real gl.* constants, not small ordinal indices).
        //
        // Known boundary (found via a live browser probe, 2026-08-17, before writing a smoke-test
        // assertion that would have been untestable): this value is genuinely applied to the
        // baseTexture's own WebGL sampler (`baseTexture.wrapMode`), so it is real, not a stub -- but
        // XNA/D3D's classic "one Draw() call with a source rectangle larger than the texture tiles
        // automatically under TextureAddressMode.Wrap" trick cannot be reproduced through it. Each
        // Draw() builds a fresh `new PIXI.Texture(baseTexture, frameRect, ...)` "view" (see
        // CNA_PixiJs_FlushSprites above), and PixiJS's Texture constructor throws
        // ("frame does not fit inside the base Texture dimensions") for any frameRect that exceeds
        // the base texture's own pixel bounds -- confirmed live: `new PIXI.Texture(base2x2,
        // new PIXI.Rectangle(0,0,4,4))` throws even with wrapMode already set to REPEAT. A source
        // rect stays within the base texture's bounds by construction in every other test in this
        // file, so wrapMode currently only affects the subtler linear-filter edge-bleed case (a
        // sampler reading half a texel past a frame's edge), not visible large-scale tiling --
        // that would need a `PIXI.TilingSprite`-based draw path, which is out of this v1 scope.
        int TextureAddressModeToPixiWrapMode(int addressMode)
        {
            switch (addressMode)
            {
                case 0: return 10497; // Wrap -> PIXI.WRAP_MODES.REPEAT
                case 2: return 33648; // Mirror -> PIXI.WRAP_MODES.MIRRORED_REPEAT
                case 1:
                default: return 33071; // Clamp -> PIXI.WRAP_MODES.CLAMP
            }
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
        activeBlendMode_ = state_->blendMode;
        begun_ = true;
    }

    void PixiJsSpriteBatchRenderer::End()
    {
#if defined(__EMSCRIPTEN__)
        if (!commands_.empty())
        {
            // PIXI.BLEND_MODES.NORMAL=0 / .ADD=1 in PixiJS v7 -- see
            // PixiJsRenderer.cpp's CNA_PixiJs_SetBlendMode for the same table.
            const int pixiBlendModeCode = PixiBlendModeToPixiJsCode(activeBlendMode_);
            const int pixiWrapMode = TextureAddressModeToPixiWrapMode(addressU_);
            const int pixiScaleMode = linearFilter_ ? 1 : 0; // PIXI.SCALE_MODES.LINEAR/.NEAREST
            CNA_PixiJs_FlushSprites(commands_.data(), static_cast<int>(commands_.size()), 14,
                                    pixiBlendModeCode, pixiWrapMode, pixiScaleMode,
                                    transformA_, transformB_, transformC_, transformD_,
                                    transformTx_, transformTy_);
        }
#endif
        begun_ = false;
    }

    void PixiJsSpriteBatchRenderer::SetImmediateMode(bool immediate)
    {
        immediateMode_ = immediate;
    }

    void PixiJsSpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        // plan_pixijs.md PIXIJS-45: SpriteBatch's transformMatrix is always a 2D affine map in this
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
        // plan_pixijs.md PIXIJS-47/Design decision 10.
        if (effect != nullptr)
            throw std::runtime_error(
                "PixiJS (v1 scope) does not support a custom Effect passed to SpriteBatch::Begin() "
                "yet -- plan_pixijs.md Design decision 10 (PixiJS has a real shader stage, unlike "
                "Canvas2D/DOM, but mapping CNA's Effect model onto it is out of v1 scope).");
    }

    void PixiJsSpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        // plan_pixijs.md PIXIJS-53: same magnification-dominant TextureFilter grouping
        // CANVAS-42/CanvasSpriteBatchRenderer::SetSamplerFilter already established -- SpriteBatch
        // draws are near-universally magnification-dominant, so the "expand" component of
        // TextureFilter is what visibly matters. Linear=0, Anisotropic=2, LinearMipPoint=3,
        // MinLinearMagPointMipLinear/MipPoint... -- reusing the exact {0,2,3,7,8} set Canvas's own
        // Task 701 derivation settled on, applied here as PIXI.SCALE_MODES.LINEAR vs .NEAREST.
        switch (textureFilter)
        {
            case 0: case 2: case 3: case 7: case 8:
                linearFilter_ = true;
                break;
            default:
                linearFilter_ = false;
                break;
        }
    }

    void PixiJsSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        // plan_pixijs.md PIXIJS-46: PIXI.BaseTexture exposes one wrapMode for both axes -- addressU
        // is applied at flush time as the representative value (addressV is stored but currently
        // unused); mixed per-axis U/V modes are a known, documented boundary, not silently dropped.
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

#if defined(__EMSCRIPTEN__)
        if (immediateMode_)
        {
            const int pixiBlendModeCode = PixiBlendModeToPixiJsCode(activeBlendMode_);
            const int pixiWrapMode = TextureAddressModeToPixiWrapMode(addressU_);
            const int pixiScaleMode = linearFilter_ ? 1 : 0; // PIXI.SCALE_MODES.LINEAR/.NEAREST
            CNA_PixiJs_FlushSprites(&command, 1, 14, pixiBlendModeCode, pixiWrapMode, pixiScaleMode,
                                    transformA_, transformB_, transformC_, transformD_,
                                    transformTx_, transformTy_);
            return;
        }
#endif
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
