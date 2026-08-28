// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Utf8Decode.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/Collections/Generic/KeyNotFoundException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    using namespace CNA::Internal::Renderers;

    namespace
    {
        /**
         * Total order over float, matching FNA's own depth comparers, which are
         * `p2->depth.CompareTo(p1->depth)` (`SpriteBatch.cs:1602`): NaN sorts below everything,
         * including negative infinity, and NaN compares equal to NaN.
         *
         * FNA is the behavioural reference, and here it and XNA genuinely differ. XNA's comparers
         * are a bare `>` / `<` pair returning 0 when neither holds, so a NaN depth compares
         * **equal to every other depth**. That is not merely a different order: equivalence stops
         * being transitive (NaN ~ 1 and NaN ~ 2 while 1 !~ 2), which is exactly what
         * `std::stable_sort` forbids. Copying XNA's shape here would leave the undefined behaviour
         * in place, so FNA's total order is both the reference answer and the only safe one --
         * and it is what let CNA start accepting non-finite depths at all.
         */
        [[nodiscard]] int CompareOrdered(float a, float b) noexcept
        {
            const bool aNaN = std::isnan(a);
            const bool bNaN = std::isnan(b);
            if (aNaN || bNaN)
            {
                if (aNaN && bNaN) return 0;
                return aNaN ? -1 : 1;
            }
            if (a < b) return -1;
            if (a > b) return 1;
            return 0;
        }

        [[noreturn]] void ThrowDestinationOutOfRange(const char* parameterName)
        {
            throw System::ArgumentOutOfRangeException(
                parameterName,
                "The calculated SpriteBatch destination component must be within Int32 range.");
        }

        // XNA/FNA keep a sprite's destination in floating point all the way to the vertex data,
        // so a component is validated here but never quantised. The Int32 window is still the
        // documented boundary of a SpriteBatch destination, and rejecting outside it keeps the
        // exception contract every renderer and test already relies on.
        // CABI-38: a non-finite component is not outside the Int32 window, it is outside the
        // number line, and XNA carries it into the vertex path rather than refusing it. The range
        // check therefore only asks the question of values that have an answer.
        float ValidateDestinationComponent(float value, const char* parameterName)
        {
            if (!std::isfinite(value))
            {
                return value;
            }
            const double widened = static_cast<double>(value);
            if (widened < static_cast<double>(std::numeric_limits<intcs>::lowest()) ||
                widened > static_cast<double>(std::numeric_limits<intcs>::max()))
            {
                ThrowDestinationOutOfRange(parameterName);
            }
            return value;
        }

    }

    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    SpriteBatch::SpriteBatch(GraphicsDevice& graphicsDevice)
        : GraphicsResource(&graphicsDevice)
        , renderer_(graphicsDevice.GetRenderer().CreateSpriteBatch())
    {
    }

    SpriteBatch::SpriteBatch() = default;

    SpriteBatch::SpriteBatch(std::unique_ptr<ISpriteBatchRenderer> renderer)
        : GraphicsResource(nullptr)
        , renderer_(std::move(renderer))
    {
    }

    SpriteBatch::~SpriteBatch() = default;

    GetTypeNameCPP(SpriteBatch, "Microsoft.Xna.Framework.Graphics.SpriteBatch")

    // -----------------------------------------------------------------------
    // Begin / End
    // -----------------------------------------------------------------------

    void SpriteBatch::Begin()
    {
        Begin(SpriteSortMode::Deferred, static_cast<const BlendState*>(nullptr), nullptr,
              nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
    }

    void SpriteBatch::Begin(SpriteSortMode sprite_sort_mode, BlendState blend_state)
    {
        Begin(sprite_sort_mode, blend_state, nullptr, nullptr, nullptr, nullptr,
              Matrix::getIdentityProperty());
    }

    void SpriteBatch::Begin(SpriteSortMode sortMode,
                            BlendState blendState,
                            SamplerState* samplerState,
                            DepthStencilState* depthStencilState,
                            RasterizerState* rasterizerState)
    {
        Begin(sortMode, blendState, samplerState, depthStencilState, rasterizerState,
              nullptr, Matrix::getIdentityProperty());
    }

    void SpriteBatch::Begin(SpriteSortMode sortMode,
                            const BlendState* blendState,
                            SamplerState* samplerState,
                            DepthStencilState* depthStencilState,
                            RasterizerState* rasterizerState)
    {
        Begin(sortMode, blendState, samplerState, depthStencilState, rasterizerState,
              nullptr, Matrix::getIdentityProperty());
    }

    void SpriteBatch::Begin(SpriteSortMode sortMode,
                            BlendState blendState,
                            SamplerState* samplerState,
                            DepthStencilState* depthStencilState,
                            RasterizerState* rasterizerState,
                            Effect* effect)
    {
        Begin(sortMode, blendState, samplerState, depthStencilState, rasterizerState,
              effect, Matrix::getIdentityProperty());
    }

    void SpriteBatch::Begin(SpriteSortMode sortMode,
                            const BlendState* blendState,
                            SamplerState* samplerState,
                            DepthStencilState* depthStencilState,
                            RasterizerState* rasterizerState,
                            Effect* effect)
    {
        Begin(sortMode, blendState, samplerState, depthStencilState, rasterizerState,
              effect, Matrix::getIdentityProperty());
    }

    void SpriteBatch::Begin(SpriteSortMode sortMode,
                            BlendState blendState,
                            SamplerState* samplerState,
                            DepthStencilState* depthStencilState,
                            RasterizerState* rasterizerState,
                            Effect* effect,
                            Matrix transformMatrix)
    {
        Begin(sortMode, &blendState, samplerState, depthStencilState, rasterizerState,
              effect, transformMatrix);
    }

    void SpriteBatch::Begin(SpriteSortMode sortMode,
                            const BlendState* blendState,
                            SamplerState* samplerState,
                            DepthStencilState* depthStencilState,
                            RasterizerState* rasterizerState,
                            Effect* effect,
                            Matrix transformMatrix)
    {
        if (begun)
            throw std::runtime_error("Begin has been called before calling End.");

        if (graphicsDevice_)
        {
            graphicsDevice_->setBlendStateProperty(
                blendState ? *blendState : BlendState::AlphaBlend);
            // Task 803 finding: this parameter was previously entirely unused -- SpriteBatch
            // draws silently inherited whatever DepthStencilState the game's own 3D rendering
            // last configured (or each renderer's own construction-time default), instead of
            // FNA's real default of DepthStencilState.None when the caller passes null. Matches
            // FNA's SpriteBatch.Begin(): a null depthStencilState always means None, the state is
            // always (re-)applied here, never left over from a previous Begin() (mirrors the
            // samplerState handling immediately below).
            graphicsDevice_->setDepthStencilStateProperty(
                depthStencilState ? *depthStencilState : DepthStencilState::None);
            // REMED-GFX-081: this parameter was previously discarded (`/*rasterizerState*/`), so a
            // RasterizerState supplied to Begin -- e.g. one with ScissorTestEnable/CullMode/FillMode
            // set -- never reached the device or renderer; the only way to affect sprite rasterizer
            // state was to assign GraphicsDevice.RasterizerState directly. FNA's PrepRenderState
            // applies `rasterizerState ?? RasterizerState.CullCounterClockwise`; do the same here
            // (a null rasterizerState always resolves to the SpriteBatch default and is always
            // (re-)applied, never left over from a previous Begin -- mirrors the blend/depth/sampler
            // handling). Applied via the GraphicsDevice property so it routes through the same
            // ApplyRasterizerState path every renderer already uses; the state is copied (no raw
            // pointer to the caller's RasterizerState is retained).
            graphicsDevice_->setRasterizerStateProperty(
                rasterizerState ? *rasterizerState : RasterizerState::CullCounterClockwise);
        }

        customEffect_    = effect;
        transformMatrix_ = transformMatrix;
        sortMode_        = sortMode;
        spriteQueue_.clear();

        if (renderer_)
        {
            try
            {
                renderer_->SetCustomEffect(customEffect_);
                renderer_->SetTransformMatrix(transformMatrix_);
                // Matches FNA: a null samplerState defaults to SamplerState.LinearClamp, and the
                // resolved state is always (re-)applied — never left over from a previous Begin().
                const SamplerState& effectiveSampler = samplerState ? *samplerState : SamplerState::LinearClamp;
                renderer_->SetSamplerFilter(static_cast<int>(effectiveSampler.getFilterProperty()));
                renderer_->SetSamplerAddressMode(static_cast<int>(effectiveSampler.getAddressUProperty()),
                                                static_cast<int>(effectiveSampler.getAddressVProperty()));
                renderer_->SetImmediateMode(sortMode_ == SpriteSortMode::Immediate);
                renderer_->Begin();
            }
            catch (...)
            {
                // A rejected custom effect used to leave `begun` true even though the renderer never
                // began. Clear the retained front-end state as well, so callers can catch the
                // honest capability exception and begin a valid batch immediately afterwards.
                try { renderer_->SetCustomEffect(nullptr); }
                catch (...) {}
                customEffect_ = nullptr;
                spriteQueue_.clear();
                begun = false;
                throw;
            }
        }
        // Renderer setup can reject an unsupported requested state (for example Skia's mip-only
        // sampler filters). Publish a successful Begin only after that setup completes, so the
        // same SpriteBatch remains reusable after the caller catches the exception.
        begun = true;
    }

    void SpriteBatch::End()
    {
        if (!begun)
            throw std::runtime_error("End was called, but Begin has not yet been called.");
        if (renderer_)
        {
            if (sortMode_ != SpriteSortMode::Immediate)
                flushBatch();
            renderer_->End();
            renderer_->SetCustomEffect(nullptr);
            // Deferred renderers may submit their final texture group only from End(). Retain
            // every queued texture renderer through that call, then release the queue.
            spriteQueue_.clear();
        }
        begun         = false;
        customEffect_ = nullptr;
    }

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    void SpriteBatch::pushSprite(const Texture2D& texture,
                                 const Rectangle& dest, const Rectangle& src,
                                 Color color, float rotation, Vector2 origin,
                                 SpriteEffects effects, float layerDepth)
    {
        pushSprite(texture,
                   static_cast<float>(dest.X), static_cast<float>(dest.Y),
                   static_cast<float>(dest.Width), static_cast<float>(dest.Height),
                   src, color, rotation, origin, effects, layerDepth);
    }

    void SpriteBatch::pushSprite(const Texture2D& texture,
                                 float destX, float destY, float destWidth, float destHeight,
                                 const Rectangle& src,
                                 Color color, float rotation, Vector2 origin,
                                 SpriteEffects effects, float layerDepth)
    {
        // Task 717 finding: without this guard, a fully-disposed Texture2D (its last shared_ptr
        // reference released, renderer_ now null) reaching flushSingle/flushBatch would dereference
        // a null ITextureRenderer& via GetRenderer() -- a guaranteed crash, not a graceful failure.
        // FNA itself doesn't guard SpriteBatch.Draw's texture argument, but its managed runtime
        // fails more safely there than a raw C++ null-reference dereference would here, so this is
        // a genuine hardening fix, not just an FNA-parity gap.
        System::ObjectDisposedException::ThrowIf(texture.getIsDisposedProperty(),
                                                  texture.getNameProperty());
        std::shared_ptr<ITextureRenderer> textureRenderer = texture.GetRendererWeak().lock();
        if (!textureRenderer)
            throw System::ObjectDisposedException(texture.getNameProperty());
        // A texture belongs to the GraphicsDevice that created it, and its renderer-side handle
        // usually means nothing to another device's renderer -- on NANOVG it means something
        // WORSE than nothing, because NanoVG image handles are small per-context integers that
        // collide across contexts, so a foreign texture names a valid but different image and
        // draws the wrong picture in silence.
        //
        // Rejected here, at Draw(), rather than left to the renderer seam. A renderer-side refusal
        // reached from flushBatch() would throw out of End(), which leaves `begun` true AND the
        // offending sprite in the queue: the next End() refuses it again and the SpriteBatch is
        // unusable for good. Refusing before the sprite is queued keeps the batch consistent in
        // both sort modes -- Immediate forwards straight through, Deferred never queues it.
        //
        // Both sides must be known before this can mean anything: a Texture2D built through
        // CreateWithRendererForTests has no device, and a SpriteBatch may be constructed without
        // one, so an unknown device is not treated as a mismatch.
        if (graphicsDevice_ != nullptr && texture.getGraphicsDeviceProperty() != nullptr &&
            texture.getGraphicsDeviceProperty() != graphicsDevice_)
        {
            throw System::InvalidOperationException(
                "SpriteBatch.Draw: the texture belongs to a different GraphicsDevice than this "
                "SpriteBatch. A resource may only be drawn by the device that created it.");
        }
        SpriteInfo info;
        info.texture    = std::move(textureRenderer);
        info.destX      = destX;
        info.destY      = destY;
        info.destWidth  = destWidth;
        info.destHeight = destHeight;
        info.srcRect    = src;
        info.color      = color;
        info.rotation   = rotation;
        info.origin     = origin;
        info.effects    = effects;
        info.layerDepth = layerDepth;

        if (sortMode_ == SpriteSortMode::Immediate)
        {
            flushSingle(info);
        }
        else
        {
            spriteQueue_.push_back(info);
        }
    }

    void SpriteBatch::flushSingle(const SpriteInfo& s)
    {
        if (!renderer_ || !s.texture) return;
        renderer_->Draw(*s.texture,
                       s.destX, s.destY, s.destWidth, s.destHeight,
                       s.srcRect, s.color,
                       s.rotation, s.origin, s.effects, s.layerDepth);
    }

    void SpriteBatch::flushBatch()
    {
        if (spriteQueue_.empty()) return;

        if (sortMode_ == SpriteSortMode::BackToFront)
        {
            std::stable_sort(spriteQueue_.begin(), spriteQueue_.end(),
                [](const SpriteInfo& a, const SpriteInfo& b) {
                    return CompareOrdered(a.layerDepth, b.layerDepth) > 0;
                });
        }
        else if (sortMode_ == SpriteSortMode::FrontToBack)
        {
            std::stable_sort(spriteQueue_.begin(), spriteQueue_.end(),
                [](const SpriteInfo& a, const SpriteInfo& b) {
                    return CompareOrdered(a.layerDepth, b.layerDepth) < 0;
                });
        }
        else if (sortMode_ == SpriteSortMode::Texture)
        {
            std::stable_sort(spriteQueue_.begin(), spriteQueue_.end(),
                [](const SpriteInfo& a, const SpriteInfo& b) {
                    return std::less<const ITextureRenderer*>{}(a.texture.get(), b.texture.get());
                });
        }
        // Deferred: no sort, submission order

        for (const SpriteInfo& s : spriteQueue_)
            flushSingle(s);
    }

    // -----------------------------------------------------------------------
    // Draw overloads
    // -----------------------------------------------------------------------

    void SpriteBatch::Draw(const Texture2D& texture, float x, float y)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!renderer_) return;
        const float destinationX = ValidateDestinationComponent(x, "x");
        const float destinationY = ValidateDestinationComponent(y, "y");
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        pushSprite(texture,
                   destinationX, destinationY,
                   static_cast<float>(w), static_cast<float>(h),
                   Rectangle(0, 0, w, h),
                   Color(255, 255, 255, 255),
                   0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    void SpriteBatch::Draw(const Texture2D& texture,
                           const Rectangle& destinationRectangle,
                           const Rectangle& sourceRectangle,
                           Color color)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!renderer_) return;
        pushSprite(texture, destinationRectangle, sourceRectangle,
                   color, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    void SpriteBatch::Draw(const Texture2D& texture,
                           const Rectangle& destinationRectangle,
                           const Rectangle& sourceRectangle,
                           Color color,
                           float rotation_rad,
                           Vector2 origin,
                           SpriteEffects effect,
                           float layerDepth)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!renderer_) return;
        pushSprite(texture, destinationRectangle, sourceRectangle,
                   color, rotation_rad, origin, effect, layerDepth);
    }

    // -----------------------------------------------------------------------
    // DrawString overloads
    // -----------------------------------------------------------------------

    // -----------------------------------------------------------------------
    // Draw overloads — position/rectangle + optional source-rectangle variants
    // -----------------------------------------------------------------------

    void SpriteBatch::Draw(const Texture2D& texture, Vector2 position, Color color)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!renderer_) return;
        const float destinationX = ValidateDestinationComponent(position.X, "position");
        const float destinationY = ValidateDestinationComponent(position.Y, "position");
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        pushSprite(texture,
                   destinationX, destinationY,
                   static_cast<float>(w), static_cast<float>(h),
                   Rectangle(0, 0, w, h),
                   color, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    void SpriteBatch::Draw(const Texture2D& texture, Vector2 position,
                           std::optional<Rectangle> sourceRectangle, Color color)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!renderer_) return;
        const float destinationX = ValidateDestinationComponent(position.X, "position");
        const float destinationY = ValidateDestinationComponent(position.Y, "position");
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        const Rectangle src = sourceRectangle.has_value() ? sourceRectangle.value() : Rectangle(0, 0, w, h);
        const int dw = sourceRectangle.has_value() ? src.Width  : w;
        const int dh = sourceRectangle.has_value() ? src.Height : h;
        pushSprite(texture,
                   destinationX, destinationY,
                   static_cast<float>(dw), static_cast<float>(dh),
                   src, color, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    void SpriteBatch::Draw(const Texture2D& texture, Vector2 position,
                           std::optional<Rectangle> sourceRectangle, Color color,
                           float rotation, Vector2 origin, float scale,
                           SpriteEffects effects, float layerDepth)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!renderer_) return;
        const float destinationX = ValidateDestinationComponent(position.X, "position");
        const float destinationY = ValidateDestinationComponent(position.Y, "position");
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        const Rectangle src = sourceRectangle.has_value() ? sourceRectangle.value() : Rectangle(0, 0, w, h);
        const int dw = sourceRectangle.has_value() ? src.Width  : w;
        const int dh = sourceRectangle.has_value() ? src.Height : h;
        const float destinationWidth =
            ValidateDestinationComponent(static_cast<float>(dw) * scale, "scale");
        const float destinationHeight =
            ValidateDestinationComponent(static_cast<float>(dh) * scale, "scale");
        pushSprite(texture,
                   destinationX, destinationY, destinationWidth, destinationHeight,
                   src, color, rotation, origin, effects, layerDepth);
    }

    void SpriteBatch::Draw(const Texture2D& texture, Vector2 position,
                           std::optional<Rectangle> sourceRectangle, Color color,
                           float rotation, Vector2 origin, Vector2 scale,
                           SpriteEffects effects, float layerDepth)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!renderer_) return;
        const float destinationX = ValidateDestinationComponent(position.X, "position");
        const float destinationY = ValidateDestinationComponent(position.Y, "position");
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        const Rectangle src = sourceRectangle.has_value() ? sourceRectangle.value() : Rectangle(0, 0, w, h);
        const int dw = sourceRectangle.has_value() ? src.Width  : w;
        const int dh = sourceRectangle.has_value() ? src.Height : h;
        const float destinationWidth =
            ValidateDestinationComponent(static_cast<float>(dw) * scale.X, "scale");
        const float destinationHeight =
            ValidateDestinationComponent(static_cast<float>(dh) * scale.Y, "scale");
        pushSprite(texture,
                   destinationX, destinationY, destinationWidth, destinationHeight,
                   src, color, rotation, origin, effects, layerDepth);
    }

    void SpriteBatch::Draw(const Texture2D& texture,
                           const Rectangle& destinationRectangle, Color color)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!renderer_) return;
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        pushSprite(texture, destinationRectangle, Rectangle(0, 0, w, h),
                   color, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    void SpriteBatch::Draw(const Texture2D& texture,
                           const Rectangle& destinationRectangle,
                           std::optional<Rectangle> sourceRectangle, Color color)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!renderer_) return;
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        const Rectangle src = sourceRectangle.has_value() ? sourceRectangle.value() : Rectangle(0, 0, w, h);
        pushSprite(texture, destinationRectangle, src,
                   color, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    void SpriteBatch::Draw(const Texture2D& texture,
                           const Rectangle& destinationRectangle,
                           std::optional<Rectangle> sourceRectangle,
                           Color color,
                           float rotation_rad,
                           Vector2 origin,
                           SpriteEffects effect,
                           float layerDepth)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (!renderer_) return;
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        const Rectangle src = sourceRectangle.has_value() ? sourceRectangle.value() : Rectangle(0, 0, w, h);
        pushSprite(texture, destinationRectangle, src,
                   color, rotation_rad, origin, effect, layerDepth);
    }

    // -----------------------------------------------------------------------
    // DrawString overloads
    // -----------------------------------------------------------------------

    void SpriteBatch::DrawString(const SpriteFont& spriteFont,
                                 const std::string& text,
                                 Vector2 position,
                                 Color color)
    {
        DrawString(spriteFont, text, position, color, 0.0f, Vector2::Zero,
                   Vector2(1.0f, 1.0f), SpriteEffects::None, 0.0f);
    }

    void SpriteBatch::DrawString(const SpriteFont& spriteFont,
                                 const std::string& text,
                                 Vector2 position,
                                 Color color,
                                 float rotation,
                                 Vector2 origin,
                                 float scale,
                                 SpriteEffects effects,
                                 float layerDepth)
    {
        DrawString(spriteFont, text, position, color, rotation, origin,
                   Vector2(scale, scale), effects, layerDepth);
    }

    void SpriteBatch::DrawString(const SpriteFont& spriteFont,
                                 const std::string& text,
                                 Vector2 position,
                                 Color color,
                                 float rotation,
                                 Vector2 origin,
                                 Vector2 scale,
                                 SpriteEffects effects,
                                 float layerDepth)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::DrawString called before Begin().");
        if (!renderer_ || text.empty()) return;

        const Texture2D& texture = spriteFont.textureValue_;
        if (texture.getWidthProperty() == 0) return;

        // CABI-7b: layerDepth was the one float every DrawString overload let through, while every
        // Draw overload refused it. It is also the value flushBatch's BackToFront/FrontToBack
        // comparators order by, and a NaN there breaks the strict weak ordering std::stable_sort
        // requires -- undefined behaviour, not a wrong sort.

        const float sinR = std::sin(rotation);
        const float cosR = std::cos(rotation);

        // Mirrors FNA's SpriteBatch.DrawString axis-direction tables, indexed by (int)effects
        // (None=0, FlipHorizontally=1, FlipVertically=2, FlipHorizontally|FlipVertically=3 --
        // SpriteEffects is a composable [Flags] enum, so the 4th, combined value is real and
        // reachable; REMED-GFX-003: these tables were previously sized for 3 entries, an OOB
        // stack read for effIdx=3). When effects != None, the whole string is measured up front
        // and `origin` is shifted by the measured size on the mirrored axis, so the flip pivots
        // around the correct edge of the text block -- otherwise each glyph would individually
        // flip in place without the character SEQUENCE itself mirroring (previously CNA's own
        // bug: effects was forwarded to pushSprite for intra-glyph texture flip only, never
        // affecting glyph placement/order at all).
        static constexpr float axisDirX[4]        = {-1.0f, 1.0f, -1.0f, 1.0f};
        static constexpr float axisDirY[4]        = {-1.0f, -1.0f, 1.0f, 1.0f};
        static constexpr float axisIsMirroredX[4] = { 0.0f, 1.0f,  0.0f, 1.0f};
        static constexpr float axisIsMirroredY[4] = { 0.0f, 0.0f,  1.0f, 1.0f};
        // Matches FNA's own `effects &= (SpriteEffects) 0x03;`: only the two low bits are
        // meaningful, masked defensively before use as a table index.
        effects = effects & static_cast<SpriteEffects>(0x03);
        const int effIdx = static_cast<int>(effects);

        Vector2 baseOffset = origin;
        if (effects != SpriteEffects::None)
        {
            const Vector2 size = spriteFont.MeasureString(text);
            baseOffset.X -= size.X * axisIsMirroredX[effIdx];
            baseOffset.Y -= size.Y * axisIsMirroredY[effIdx];
        }

        Vector2 curOffset(0.0f, 0.0f);
        bool firstInLine = true;

        for (std::size_t i = 0; i < text.size();)
        {
            const charcs c = CNA::Internal::DecodeUtf8CodePoint(text, i);

            if (c == u'\r') continue;
            if (c == u'\n')
            {
                curOffset.X = 0.0f;
                curOffset.Y += static_cast<float>(spriteFont.lineSpacing_);
                firstInLine = true;
                continue;
            }

            auto it = spriteFont.characterIndexMap_.find(c);
            if (it == spriteFont.characterIndexMap_.end())
            {
                if (!spriteFont.defaultCharacter_.has_value())
                    throw std::invalid_argument(
                        "Text contains characters that cannot be resolved by this SpriteFont.");
                it = spriteFont.characterIndexMap_.find(spriteFont.defaultCharacter_.value());
                // REMED-GFX-002: defaultCharacter is validated on construction/set (SpriteFont.cpp),
                // so this cannot fail in practice -- checked anyway rather than dereferencing
                // end(), matching FNA's characterIndexMap[DefaultCharacter.Value] Dictionary
                // indexer, which throws KeyNotFoundException on a miss.
                if (it == spriteFont.characterIndexMap_.end())
                {
                    throw System::Collections::Generic::KeyNotFoundException(
                        "defaultCharacter is not present in characters.");
                }
            }
            const int index = it->second;

            const Vector3& cKern = spriteFont.kerning_[index];
            if (firstInLine)
            {
                // The first glyph of a line takes only a POSITIVE left side bearing, and
                // no Spacing. Measured against a live XNA 4.0 build (SAMPLE-031): with the
                // sample's 'A' and 'X', whose kerning.X is -1, XNA advances 0 and places the
                // glyph flush at the draw position; with 'B', whose kerning.X is +1, it
                // advances 1. MeasureString returns the same single-character width whether
                // that font's Spacing is 0, 3 or -2, so Spacing is not applied to a line's
                // first glyph either. FNA writes Math.Abs(cKern.X) here, which pushes a
                // negative bearing RIGHT by its magnitude instead of clamping it away.
                curOffset.X += std::max(cKern.X, 0.0f);
                firstInLine = false;
            }
            else
            {
                curOffset.X += spriteFont.spacing_ + cKern.X;
            }

            const Rectangle& cCrop  = spriteFont.croppingData_[index];
            const Rectangle& cGlyph = spriteFont.glyphData_[index];

            float offsetX = baseOffset.X + (curOffset.X + static_cast<float>(cCrop.X)) * axisDirX[effIdx];
            float offsetY = baseOffset.Y + (curOffset.Y + static_cast<float>(cCrop.Y)) * axisDirY[effIdx];
            if (effects != SpriteEffects::None)
            {
                offsetX += static_cast<float>(cGlyph.Width)  * axisIsMirroredX[effIdx];
                offsetY += static_cast<float>(cGlyph.Height) * axisIsMirroredY[effIdx];
            }
            const float localX  = -offsetX;
            const float localY  = -offsetY;
            const float scaledX = localX * scale.X;
            const float scaledY = localY * scale.Y;
            const float rotX    = scaledX * cosR - scaledY * sinR;
            const float rotY    = scaledX * sinR + scaledY * cosR;

            // A glyph's destination stays in floating point, exactly as Draw()'s does. Measured
            // against a live XNA 4.0 build (SAMPLE-031): the sample's own overlay text drawn at
            // (64.5, 64.5) comes out of XNA filtered ACROSS the half pixel -- the row through
            // its 'A' reads 121, 162, 174, 174, 162, 121 where a whole-pixel glyph reads
            // 255, 255, 255, 255 -- and the same is visible at a fractional scale. Quantising
            // here (which is what CNA did until this was measured) snapped every glyph onto a
            // whole pixel and lost that, while Draw() had already been corrected.
            const float destinationX =
                ValidateDestinationComponent(position.X + rotX, "position");
            const float destinationY =
                ValidateDestinationComponent(position.Y + rotY, "position");
            const float destinationWidth =
                ValidateDestinationComponent(static_cast<float>(cGlyph.Width) * scale.X, "scale");
            const float destinationHeight =
                ValidateDestinationComponent(static_cast<float>(cGlyph.Height) * scale.Y, "scale");

            pushSprite(texture, destinationX, destinationY, destinationWidth, destinationHeight,
                       cGlyph, color, rotation, Vector2::Zero, effects, layerDepth);

            curOffset.X += cKern.Y + cKern.Z;
        }
    }

    void SpriteBatch::DrawString(const SpriteFont& spriteFont,
                                 const System::Text::StringBuilder& text,
                                 Vector2 position, Color color)
    {
        DrawString(spriteFont, text.ToString(), position, color);
    }

    void SpriteBatch::DrawString(const SpriteFont& spriteFont,
                                 const System::Text::StringBuilder& text,
                                 Vector2 position, Color color,
                                 float rotation, Vector2 origin, float scale,
                                 SpriteEffects effects, float layerDepth)
    {
        DrawString(spriteFont, text.ToString(), position, color,
                   rotation, origin, scale, effects, layerDepth);
    }

    void SpriteBatch::DrawString(const SpriteFont& spriteFont,
                                 const System::Text::StringBuilder& text,
                                 Vector2 position, Color color,
                                 float rotation, Vector2 origin, Vector2 scale,
                                 SpriteEffects effects, float layerDepth)
    {
        DrawString(spriteFont, text.ToString(), position, color,
                   rotation, origin, scale, effects, layerDepth);
    }

    void SpriteBatch::DrawMeshEXT(Effect& effect,
                                  const Vector2* positions, const Color* colors, const Vector2* uvs,
                                  int vertexCount, const std::uint16_t* indices, int indexCount)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::DrawMeshEXT called before Begin().");
        // A mesh draw does not participate in the deferred sort/batch queue -- a declared, tested
        // scope boundary (docs/skia-vertices-2d-effect-contract.md), not a silent misbatch.
        if (sortMode_ != SpriteSortMode::Immediate)
        {
            throw std::runtime_error(
                "SpriteBatch::DrawMeshEXT requires SpriteSortMode::Immediate; it does not "
                "participate in the deferred sprite sort/batch queue.");
        }
        if (!renderer_) return;
        renderer_->DrawMeshEXT(effect, positions, colors, uvs, vertexCount, indices, indexCount);
    }
}
