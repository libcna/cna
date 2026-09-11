// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/XnaStateConversion.hpp"
#include "CNA/Internal/Utf8Decode.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    using namespace CNA::Internal::Renderers;

    namespace
    {
        // XNA delegates to .NET Framework 4's unstable Array.Sort<T> quicksort. Its depth
        // comparer also treats NaN as equal to every value, so a standard C++ sort would have
        // undefined behavior; reproducing the original partition loop keeps both cases defined.
        template <typename T, typename Compare>
        void XnaArraySort(std::vector<T>& items, Compare compare)
        {
            if (items.size() < 2)
            {
                return;
            }

            const auto swapIfGreater = [&](std::ptrdiff_t first, std::ptrdiff_t second)
            {
                if (first != second && compare(items[static_cast<std::size_t>(first)],
                                               items[static_cast<std::size_t>(second)]) > 0)
                {
                    std::swap(items[static_cast<std::size_t>(first)],
                              items[static_cast<std::size_t>(second)]);
                }
            };

            const auto quickSort = [&](auto&& self, std::ptrdiff_t left, std::ptrdiff_t right) -> void
            {
                do
                {
                    std::ptrdiff_t first = left;
                    std::ptrdiff_t last = right;
                    const std::ptrdiff_t middle = first + ((last - first) >> 1);

                    swapIfGreater(first, middle);
                    swapIfGreater(first, last);
                    swapIfGreater(middle, last);

                    const T pivot = items[static_cast<std::size_t>(middle)];
                    do
                    {
                        while (compare(items[static_cast<std::size_t>(first)], pivot) < 0)
                        {
                            ++first;
                        }
                        while (compare(pivot, items[static_cast<std::size_t>(last)]) < 0)
                        {
                            --last;
                        }
                        if (first > last)
                        {
                            break;
                        }
                        if (first < last)
                        {
                            std::swap(items[static_cast<std::size_t>(first)],
                                      items[static_cast<std::size_t>(last)]);
                        }
                        ++first;
                        --last;
                    }
                    while (first <= last);

                    if (last - left <= right - first)
                    {
                        if (left < last)
                        {
                            self(self, left, last);
                        }
                        left = first;
                    }
                    else
                    {
                        if (first < right)
                        {
                            self(self, first, right);
                        }
                        right = last;
                    }
                }
                while (left < right);
            };

            quickSort(quickSort, 0, static_cast<std::ptrdiff_t>(items.size() - 1));
        }

        [[nodiscard]] int CompareXnaDepth(float first, float second,
                                          bool frontToBack) noexcept
        {
            if (first > second)
            {
                return frontToBack ? 1 : -1;
            }
            if (first < second)
            {
                return frontToBack ? -1 : 1;
            }
            return 0;
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

    void SpriteBatch::Dispose(bool disposing)
    {
        if (!isDisposed_)
        {
            renderer_.reset();
            spriteQueue_.clear();
            customEffect_ = nullptr;
            begun = false;
        }
        GraphicsResource::Dispose(disposing);
    }

    void SpriteBatch::throwIfDisposed() const
    {
        if (getIsDisposedProperty())
        {
            throw System::ObjectDisposedException(
                getNameProperty().empty() ? "SpriteBatch" : getNameProperty());
        }
    }

    void SpriteBatch::applyRenderState()
    {
        if (graphicsDevice_ != nullptr)
        {
            graphicsDevice_->setBlendStateProperty(blendState_);
            graphicsDevice_->getSamplerStatesProperty()[0] = samplerState_;
            graphicsDevice_->setDepthStencilStateProperty(depthStencilState_);
            graphicsDevice_->setRasterizerStateProperty(rasterizerState_);
        }

        // A deferred batch retains the caller's state object until this flush boundary. Its
        // properties are still mutable between Begin and End, so refresh the renderer's private
        // SpriteBatch sampler channel from the retained payload immediately before drawing.
        if (renderer_ != nullptr)
        {
            renderer_->SetSamplerFilter(NormalizeXnaTextureFilterOrdinal(
                static_cast<int>(samplerState_.getFilterProperty())));
            renderer_->SetSamplerMaxAnisotropy(samplerState_.getMaxAnisotropyProperty());
            renderer_->SetSamplerMipState(samplerState_.getMaxMipLevelProperty(),
                                          samplerState_.getMipMapLevelOfDetailBiasProperty());
            renderer_->SetSamplerAddressMode(
                NormalizeXnaTextureAddressModeOrdinal(
                    static_cast<int>(samplerState_.getAddressUProperty())),
                NormalizeXnaTextureAddressModeOrdinal(
                    static_cast<int>(samplerState_.getAddressVProperty())));
        }
    }

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
                            const SamplerState* samplerState,
                            const DepthStencilState* depthStencilState,
                            const RasterizerState* rasterizerState)
    {
        Begin(sortMode, blendState, samplerState, depthStencilState, rasterizerState,
              nullptr, Matrix::getIdentityProperty());
    }

    void SpriteBatch::Begin(SpriteSortMode sortMode,
                            const BlendState* blendState,
                            const SamplerState* samplerState,
                            const DepthStencilState* depthStencilState,
                            const RasterizerState* rasterizerState)
    {
        Begin(sortMode, blendState, samplerState, depthStencilState, rasterizerState,
              nullptr, Matrix::getIdentityProperty());
    }

    void SpriteBatch::Begin(SpriteSortMode sortMode,
                            BlendState blendState,
                            const SamplerState* samplerState,
                            const DepthStencilState* depthStencilState,
                            const RasterizerState* rasterizerState,
                            Effect* effect)
    {
        Begin(sortMode, blendState, samplerState, depthStencilState, rasterizerState,
              effect, Matrix::getIdentityProperty());
    }

    void SpriteBatch::Begin(SpriteSortMode sortMode,
                            const BlendState* blendState,
                            const SamplerState* samplerState,
                            const DepthStencilState* depthStencilState,
                            const RasterizerState* rasterizerState,
                            Effect* effect)
    {
        Begin(sortMode, blendState, samplerState, depthStencilState, rasterizerState,
              effect, Matrix::getIdentityProperty());
    }

    void SpriteBatch::Begin(SpriteSortMode sortMode,
                            BlendState blendState,
                            const SamplerState* samplerState,
                            const DepthStencilState* depthStencilState,
                            const RasterizerState* rasterizerState,
                            Effect* effect,
                            Matrix transformMatrix)
    {
        Begin(sortMode, &blendState, samplerState, depthStencilState, rasterizerState,
              effect, transformMatrix);
    }

    void SpriteBatch::Begin(SpriteSortMode sortMode,
                            const BlendState* blendState,
                            const SamplerState* samplerState,
                            const DepthStencilState* depthStencilState,
                            const RasterizerState* rasterizerState,
                            Effect* effect,
                            Matrix transformMatrix)
    {
        throwIfDisposed();
        if (begun)
            throw System::InvalidOperationException("Begin has been called before calling End.");

        blendState_ = blendState ? *blendState : BlendState::AlphaBlend;
        samplerState_ = samplerState ? *samplerState : SamplerState::LinearClamp;
        depthStencilState_ = depthStencilState ? *depthStencilState : DepthStencilState::None;
        rasterizerState_ =
            rasterizerState ? *rasterizerState : RasterizerState::CullCounterClockwise;
        customEffect_ = effect;
        transformMatrix_ = transformMatrix;
        sortMode_ = sortMode;
        spriteQueue_.clear();

        // Microsoft XNA coordinates every SpriteBatch attached to one GraphicsDevice. Multiple
        // deferred batches may coexist, but Immediate is mutually exclusive with all of them.
        // These checks precede Immediate state application and do not publish a failed Begin.
        if (graphicsDevice_ != nullptr)
        {
            if (sortMode_ == SpriteSortMode::Immediate)
            {
                if (graphicsDevice_->spriteBeginCount_ > 0)
                {
                    throw System::InvalidOperationException(
                        "Cannot begin an Immediate SpriteBatch while another SpriteBatch is active.");
                }
            }
            else if (graphicsDevice_->spriteImmediateBeginCount_ > 0)
            {
                throw System::InvalidOperationException(
                    "Cannot begin a SpriteBatch while an Immediate SpriteBatch is active.");
            }
        }

        // Immediate applies state before the pair and device counters become active. Every other
        // sorting mode waits until End(), including an empty batch.
        if (sortMode_ == SpriteSortMode::Immediate)
            applyRenderState();

        if (renderer_)
        {
            try
            {
                renderer_->SetCustomEffect(customEffect_);
                renderer_->SetTransformMatrix(transformMatrix_);
                // Matches FNA: a null samplerState defaults to SamplerState.LinearClamp, and the
                // resolved state is always (re-)applied — never left over from a previous Begin().
                renderer_->SetSamplerFilter(NormalizeXnaTextureFilterOrdinal(
                    static_cast<int>(samplerState_.getFilterProperty())));
                renderer_->SetSamplerMaxAnisotropy(samplerState_.getMaxAnisotropyProperty());
                renderer_->SetSamplerMipState(samplerState_.getMaxMipLevelProperty(),
                                              samplerState_.getMipMapLevelOfDetailBiasProperty());
                renderer_->SetSamplerAddressMode(
                    NormalizeXnaTextureAddressModeOrdinal(
                        static_cast<int>(samplerState_.getAddressUProperty())),
                    NormalizeXnaTextureAddressModeOrdinal(
                        static_cast<int>(samplerState_.getAddressVProperty())));
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
        // Renderer setup can reject an unsupported requested state. Publish a successful Begin
        // and the Microsoft device-level accounting only after that setup completes.
        if (graphicsDevice_ != nullptr)
        {
            if (sortMode_ == SpriteSortMode::Immediate)
                ++graphicsDevice_->spriteImmediateBeginCount_;
            ++graphicsDevice_->spriteBeginCount_;
        }
        begun = true;
    }

    void SpriteBatch::End()
    {
        throwIfDisposed();
        if (!begun)
            throw System::InvalidOperationException("End was called, but Begin has not yet been called.");
        bool rendererEndAttempted = false;
        const auto releaseDeviceAccounting = [this]()
        {
            if (graphicsDevice_ == nullptr)
                return;
            if (sortMode_ == SpriteSortMode::Immediate &&
                graphicsDevice_->spriteImmediateBeginCount_ > 0)
            {
                --graphicsDevice_->spriteImmediateBeginCount_;
            }
            if (graphicsDevice_->spriteBeginCount_ > 0)
                --graphicsDevice_->spriteBeginCount_;
        };
        try
        {
            if (sortMode_ != SpriteSortMode::Immediate)
                applyRenderState();
            if (renderer_)
            {
                if (sortMode_ != SpriteSortMode::Immediate)
                    flushBatch();
                rendererEndAttempted = true;
                renderer_->End();
                renderer_->SetCustomEffect(nullptr);
                // Deferred renderers may submit their final texture group only from End(). Retain
                // every queued texture renderer through that call, then release the queue.
                spriteQueue_.clear();
            }
            customEffect_ = nullptr;
        }
        catch (...)
        {
            // Microsoft leaves the Begin/End pair and device counters active when deferred state
            // application or Flush fails. A repeated End retries the same work, and Immediate
            // remains blocked on this device. A CNA-private renderer End failure is different: its
            // backend pair cannot safely be retried, so retain the established recoverable seam.
            if (!rendererEndAttempted)
                throw;

            spriteQueue_.clear();
            if (renderer_)
            {
                try { renderer_->SetCustomEffect(nullptr); }
                catch (...) {}
            }
            customEffect_ = nullptr;
            begun = false;
            releaseDeviceAccounting();
            throw;
        }
        begun = false;
        releaseDeviceAccounting();
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
        if (graphicsDevice_ != nullptr)
        {
            GpuDrawParams params;
            params.texture0 = s.texture.get();
            graphicsDevice_->validateDrawState(&params);
        }
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
            XnaArraySort(spriteQueue_,
                [](const SpriteInfo& a, const SpriteInfo& b) {
                    return CompareXnaDepth(a.layerDepth, b.layerDepth, false);
                });
        }
        else if (sortMode_ == SpriteSortMode::FrontToBack)
        {
            XnaArraySort(spriteQueue_,
                [](const SpriteInfo& a, const SpriteInfo& b) {
                    return CompareXnaDepth(a.layerDepth, b.layerDepth, true);
                });
        }
        else if (sortMode_ == SpriteSortMode::Texture)
        {
            XnaArraySort(spriteQueue_,
                [](const SpriteInfo& a, const SpriteInfo& b) {
                    const auto less = std::less<const ITextureRenderer*>{};
                    if (less(b.texture.get(), a.texture.get())) return -1;
                    if (less(a.texture.get(), b.texture.get())) return 1;
                    return 0;
                });
        }
        else if (sortMode_ != SpriteSortMode::Deferred)
        {
            throw System::NotSupportedException();
        }
        // Deferred: no sort, submission order.

        for (const SpriteInfo& s : spriteQueue_)
            flushSingle(s);
    }

    // -----------------------------------------------------------------------
    // Draw overloads
    // -----------------------------------------------------------------------

    void SpriteBatch::Draw(const Texture2D& texture, float x, float y)
    {
        throwIfDisposed();
        if (!begun) throw System::InvalidOperationException("SpriteBatch::Draw called before Begin().");
        if (!renderer_) return;
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        pushSprite(texture,
                   x, y,
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
        throwIfDisposed();
        if (!begun) throw System::InvalidOperationException("SpriteBatch::Draw called before Begin().");
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
        throwIfDisposed();
        if (!begun) throw System::InvalidOperationException("SpriteBatch::Draw called before Begin().");
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
        throwIfDisposed();
        if (!begun) throw System::InvalidOperationException("SpriteBatch::Draw called before Begin().");
        if (!renderer_) return;
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        pushSprite(texture,
                   position.X, position.Y,
                   static_cast<float>(w), static_cast<float>(h),
                   Rectangle(0, 0, w, h),
                   color, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    void SpriteBatch::Draw(const Texture2D& texture, Vector2 position,
                           std::optional<Rectangle> sourceRectangle, Color color)
    {
        throwIfDisposed();
        if (!begun) throw System::InvalidOperationException("SpriteBatch::Draw called before Begin().");
        if (!renderer_) return;
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        const Rectangle src = sourceRectangle.has_value() ? sourceRectangle.value() : Rectangle(0, 0, w, h);
        const int dw = sourceRectangle.has_value() ? src.Width  : w;
        const int dh = sourceRectangle.has_value() ? src.Height : h;
        pushSprite(texture,
                   position.X, position.Y,
                   static_cast<float>(dw), static_cast<float>(dh),
                   src, color, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    void SpriteBatch::Draw(const Texture2D& texture, Vector2 position,
                           std::optional<Rectangle> sourceRectangle, Color color,
                           float rotation, Vector2 origin, float scale,
                           SpriteEffects effects, float layerDepth)
    {
        throwIfDisposed();
        if (!begun) throw System::InvalidOperationException("SpriteBatch::Draw called before Begin().");
        if (!renderer_) return;
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        const Rectangle src = sourceRectangle.has_value() ? sourceRectangle.value() : Rectangle(0, 0, w, h);
        const int dw = sourceRectangle.has_value() ? src.Width  : w;
        const int dh = sourceRectangle.has_value() ? src.Height : h;
        const float destinationWidth = static_cast<float>(dw) * scale;
        const float destinationHeight = static_cast<float>(dh) * scale;
        pushSprite(texture,
                   position.X, position.Y, destinationWidth, destinationHeight,
                   src, color, rotation, origin, effects, layerDepth);
    }

    void SpriteBatch::Draw(const Texture2D& texture, Vector2 position,
                           std::optional<Rectangle> sourceRectangle, Color color,
                           float rotation, Vector2 origin, Vector2 scale,
                           SpriteEffects effects, float layerDepth)
    {
        throwIfDisposed();
        if (!begun) throw System::InvalidOperationException("SpriteBatch::Draw called before Begin().");
        if (!renderer_) return;
        const int w = texture.getWidthProperty();
        const int h = texture.getHeightProperty();
        const Rectangle src = sourceRectangle.has_value() ? sourceRectangle.value() : Rectangle(0, 0, w, h);
        const int dw = sourceRectangle.has_value() ? src.Width  : w;
        const int dh = sourceRectangle.has_value() ? src.Height : h;
        const float destinationWidth = static_cast<float>(dw) * scale.X;
        const float destinationHeight = static_cast<float>(dh) * scale.Y;
        pushSprite(texture,
                   position.X, position.Y, destinationWidth, destinationHeight,
                   src, color, rotation, origin, effects, layerDepth);
    }

    void SpriteBatch::Draw(const Texture2D& texture,
                           const Rectangle& destinationRectangle, Color color)
    {
        throwIfDisposed();
        if (!begun) throw System::InvalidOperationException("SpriteBatch::Draw called before Begin().");
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
        throwIfDisposed();
        if (!begun) throw System::InvalidOperationException("SpriteBatch::Draw called before Begin().");
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
        throwIfDisposed();
        if (!begun) throw System::InvalidOperationException("SpriteBatch::Draw called before Begin().");
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
        throwIfDisposed();
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
        throwIfDisposed();
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
        throwIfDisposed();
        if (!begun) throw System::InvalidOperationException("SpriteBatch::DrawString called before Begin().");
        if (!renderer_ || text.empty()) return;

        const Texture2D& texture = spriteFont.textureValue_;
        if (texture.getWidthProperty() == 0) return;

        // Keep layerDepth in the original Single domain. The sorted modes reproduce XNA's
        // Array.Sort partitioning explicitly, including its unordered NaN comparisons.

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

            const int index = spriteFont.getIndexForCharacter(c);

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
            const float destinationX = position.X + rotX;
            const float destinationY = position.Y + rotY;
            const float destinationWidth = static_cast<float>(cGlyph.Width) * scale.X;
            const float destinationHeight = static_cast<float>(cGlyph.Height) * scale.Y;

            pushSprite(texture, destinationX, destinationY, destinationWidth, destinationHeight,
                       cGlyph, color, rotation, Vector2::Zero, effects, layerDepth);

            curOffset.X += cKern.Y + cKern.Z;
        }
    }

    void SpriteBatch::DrawString(const SpriteFont& spriteFont,
                                 const System::Text::StringBuilder& text,
                                 Vector2 position, Color color)
    {
        throwIfDisposed();
        DrawString(spriteFont, text.ToString(), position, color);
    }

    void SpriteBatch::DrawString(const SpriteFont& spriteFont,
                                 const System::Text::StringBuilder& text,
                                 Vector2 position, Color color,
                                 float rotation, Vector2 origin, float scale,
                                 SpriteEffects effects, float layerDepth)
    {
        throwIfDisposed();
        DrawString(spriteFont, text.ToString(), position, color,
                   rotation, origin, scale, effects, layerDepth);
    }

    void SpriteBatch::DrawString(const SpriteFont& spriteFont,
                                 const System::Text::StringBuilder& text,
                                 Vector2 position, Color color,
                                 float rotation, Vector2 origin, Vector2 scale,
                                 SpriteEffects effects, float layerDepth)
    {
        throwIfDisposed();
        DrawString(spriteFont, text.ToString(), position, color,
                   rotation, origin, scale, effects, layerDepth);
    }

    void SpriteBatch::DrawMeshEXT(Effect& effect,
                                  const Vector2* positions, const Color* colors, const Vector2* uvs,
                                  int vertexCount, const std::uint16_t* indices, int indexCount)
    {
        throwIfDisposed();
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
