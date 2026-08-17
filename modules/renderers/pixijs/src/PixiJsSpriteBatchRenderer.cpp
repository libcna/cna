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
EM_JS(void, CNA_PixiJs_FlushSprites, (const void* commands, int count, int stride, int blendMode), {
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

        let sprite = pool[i];
        if (!sprite) {
            sprite = new PIXI.Sprite();
            pool[i] = sprite;
        }
        if (sprite.parent !== container) container.addChild(sprite);

        // A fresh PIXI.Texture "view" per draw, sharing the entry's baseTexture/GPU resource but
        // carrying its own frame rectangle -- mutating a shared texture's own .frame in place would
        // corrupt every other sprite currently sampling the same atlas with a different sub-rect.
        const baseTexture = entry.texture.baseTexture || entry.texture;
        sprite.texture = new PIXI.Texture(baseTexture, new PIXI.Rectangle(sourceX, sourceY, sourceWidth, sourceHeight));

        // plan_pixijs.md PIXIJS-43: anchor is PixiJS's own normalized (0..1 of the frame) pivot --
        // XNA's origin is source-pixel space, so divide through by the source rectangle's own size.
        sprite.anchor.set(sourceWidth ? originX / sourceWidth : 0, sourceHeight ? originY / sourceHeight : 0);
        sprite.position.set(destinationX, destinationY);

        // plan_pixijs.md PIXIJS-44 (unverified): flip via negative scale composed with the same
        // anchor point rotation/scale already pivot around -- believed correct by construction
        // (anchor-relative scale/rotation is exactly XNA's own origin-relative model) but not
        // verified against a real FNA reference render yet.
        const flipH = (flags & 1) !== 0;
        const flipV = (flags & 2) !== 0;
        const scaleX = (sourceWidth ? destinationWidth / sourceWidth : 1) * (flipH ? -1 : 1);
        const scaleY = (sourceHeight ? destinationHeight / sourceHeight : 1) * (flipV ? -1 : 1);
        sprite.scale.set(scaleX, scaleY);
        sprite.rotation = rotation;

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
            const int pixiBlendModeCode = activeBlendMode_ == PixiJsBlendMode::Additive ? 1 : 0;
            CNA_PixiJs_FlushSprites(commands_.data(), static_cast<int>(commands_.size()), 14,
                                    pixiBlendModeCode);
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
        // plan_pixijs.md PIXIJS-45: not yet implemented. A non-identity transform silently ignored
        // would misrender with no error, so this throws rather than pretending to apply it.
        if (m != Matrix::getIdentityProperty())
            throw std::runtime_error(
                "PixiJS (v1 scope): SpriteBatch::Begin(transformMatrix) with a non-identity matrix "
                "is not yet implemented -- plan_pixijs.md PIXIJS-45.");
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

    void PixiJsSpriteBatchRenderer::SetSamplerFilter(int /*textureFilter*/)
    {
        // plan_pixijs.md PIXIJS-53: not yet implemented.
    }

    void PixiJsSpriteBatchRenderer::SetSamplerAddressMode(int /*addressU*/, int /*addressV*/)
    {
        // plan_pixijs.md PIXIJS-46: not yet implemented.
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
            const int pixiBlendModeCode = activeBlendMode_ == PixiJsBlendMode::Additive ? 1 : 0;
            CNA_PixiJs_FlushSprites(&command, 1, 14, pixiBlendModeCode);
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
