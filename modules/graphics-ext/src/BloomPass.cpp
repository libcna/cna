// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/BloomPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"
#include "CNA/GraphicsCapability.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "PostProcessShaderPackages.hpp"

#include <algorithm>
#include <cmath>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::SamplerState;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    namespace {

        constexpr int kMinIterations = 1;
        constexpr int kMaxIterations = 8;
        /// Below this, a chain step contributes nothing but a draw call.
        constexpr int kMinChainExtent = 2;

        /// Pool slots. The pool keys a target on its shape *and* its slot, and this chain holds
        /// several targets of the same shape alive at once -- a level, the horizontal scratch that
        /// produced it, and the sum written back into it -- so the slots must not collide. Bases
        /// rather than fixed numbers because there is one level and one sum per iteration.
        constexpr int kExtractSlot       = 0;
        constexpr int kBlurSlot          = 1;
        constexpr int kLevelSlotBase     = 100;
        constexpr int kUpsampleSlotBase  = 200;

        class ScopedSamplerStateOverride final
        {
        public:
            ScopedSamplerStateOverride(GraphicsDevice& device, const int slot,
                                       const SamplerState& replacement)
                : device_(device), slot_(slot), previous_(device.getSamplerStatesProperty()[slot])
            {
                device_.getSamplerStatesProperty()[slot_] = replacement;
            }

            ~ScopedSamplerStateOverride()
            {
                device_.getSamplerStatesProperty()[slot_] = previous_;
            }

            ScopedSamplerStateOverride(const ScopedSamplerStateOverride&) = delete;
            ScopedSamplerStateOverride& operator=(const ScopedSamplerStateOverride&) = delete;

        private:
            GraphicsDevice& device_;
            int slot_;
            SamplerState previous_;
        };

        void SetGlDualSamplerOrientation(ShaderEffect& effect, const Texture2D* primary,
                                         const Texture2D* secondary)
        {
            const CNA::ShaderLanguageEXT language = effect.GetSelectedShaderLanguageEXT();
            if (language != CNA::ShaderLanguageEXT::GlslEs
                && language != CNA::ShaderLanguageEXT::GlslDesktop)
                return;

            const float primaryFlip = dynamic_cast<const RenderTarget2D*>(primary) ? 1.0f : 0.0f;
            const float secondaryFlip = dynamic_cast<const RenderTarget2D*>(secondary) ? 1.0f : 0.0f;
            effect.SetUniformVec4("uRtFlipV", primaryFlip, secondaryFlip, 0.0f, 0.0f);
        }

    } // namespace

    BloomPass::BloomPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device)), pool_(device)
    {
        const auto makeEffect = [&device](const ShaderPackageEXT& package) {
            return package.selectFor(device).isUsable()
                ? std::make_unique<ShaderEffect>(device, package)
                : nullptr;
        };
        extractEffect_  = makeEffect(detail::CreateBloomExtractShaderPackage());
        blurEffect_     = makeEffect(detail::CreateBloomBlurShaderPackage());
        upsampleEffect_ = makeEffect(detail::CreateBloomUpsampleShaderPackage());
        combineEffect_  = makeEffect(detail::CreateBloomCombineShaderPackage());

        // MOD-407: whether the hardware can filter the float textures this chain is built from.
        // Asked once, at construction: it is a property of the renderer, not of the frame.
        manualFilter_ = !device.SupportsCapability(
            CNA::GraphicsCapability::HalfFloatTextureLinearFiltering);

        // plans/plan_modern.md MOD-219, reported here rather than in apply(): the failure happens once,
        // at construction, and a pass that discovered it per frame would either spam the log or
        // need a flag to avoid doing so. Falling back to a copy is silent by design, and this is
        // what turns "bloom looks weak" into a line naming the pass and the compiler's own log.
        bool logged = false;
        detail::reportShaderCompileFailure(device, "BloomPass (extract)", extractEffect_.get(),
                                           logged);
        detail::reportShaderCompileFailure(device, "BloomPass (blur)", blurEffect_.get(), logged);
        detail::reportShaderCompileFailure(device, "BloomPass (upsample)", upsampleEffect_.get(),
                                           logged);
        detail::reportShaderCompileFailure(device, "BloomPass (combine)", combineEffect_.get(),
                                           logged);
    }

    BloomPass::~BloomPass() = default;

    float BloomPass::extractChannel(const float value, const float threshold)
    {
        const float knee = std::max(threshold * 0.5f, 1e-4f);
        float contribution = std::clamp((value - threshold + knee) / (2.0f * knee), 0.0f, 1.0f);
        contribution *= contribution;
        return value * contribution;
    }

    void BloomPass::apply(const PostProcessContext& context)
    {
        float threshold  = threshold_;
        float intensity  = intensity_;
        int   iterations = iterations_;
        if (context.settings != nullptr)
        {
            threshold  = context.settings->getBloomThreshold();
            intensity  = context.settings->getBloomIntensity();
            iterations = context.settings->getBloomIterations();
        }

        const bool shadersReady = extractEffect_ && extractEffect_->IsEffectValid()
                               && blurEffect_ && blurEffect_->IsEffectValid()
                               && upsampleEffect_ && upsampleEffect_->IsEffectValid()
                               && combineEffect_ && combineEffect_->IsEffectValid();
        if (!shadersReady)
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        iterations = std::clamp(iterations, kMinIterations, kMaxIterations);

        // plans/plan_modern.md MOD-220: bloom's requirement, stated by the pass. Every stage here reads a
        // target at a *different* resolution from the one it writes, so point filtering would
        // sample one texel of four and turn the pyramid into a mosaic -- an image that still looks
        // like bloom, just wrong, with nothing in the frame to say why. Clamp matters at the edges
        // for the same reason: wrapping pulls the opposite side of the screen into the blur.
        //
        // This does not *change* behaviour: SpriteBatch::Begin already documents a null sampler as
        // meaning LinearClamp, so bloom was getting the right filtering by inheritance. That is the
        // point of stating it -- the requirement was being met by a default nothing tied to bloom,
        // and a change to that default would have degraded the pyramid silently.
        //
        // The const_cast is forced by the XNA-shaped API: SamplerState::LinearClamp is a static
        // const, and SpriteBatch::Begin takes a non-const pointer. Nothing writes through it.
        SamplerState* const linearClamp =
            const_cast<SamplerState*>(&SamplerState::LinearClamp);

        // SpriteBatch publishes the sampler passed to Begin only in slot 0. Bloom's upsample and
        // combine shaders also read slot 1, whose XNA default is LinearWrap. Leaving that default
        // in place wraps an edge highlight onto the opposite edge and makes a narrow pyramid look
        // frame-wide. The manual-filter fallback needs point sampling; otherwise both inputs use
        // the linear clamp the algorithm promises. Restore the game's slot after the queued draws
        // have captured it so an internal post-process choice does not leak into later rendering.
        GraphicsDevice* const device = extractEffect_->getGraphicsDeviceProperty();
        const SamplerState& secondarySampler =
            manualFilter_ ? SamplerState::PointClamp : SamplerState::LinearClamp;
        ScopedSamplerStateOverride secondarySamplerScope(*device, 1, secondarySampler);

        // Intermediates carry the source's format so an HDR scene stays HDR through the chain;
        // clamping here would remove exactly the highlights bloom exists to spread.
        const auto format = context.source->getFormatProperty();

        // ---- Down: extract, then a blurred half-resolution pyramid --------------------------
        //
        // Each level is half the previous one and holds the blur of everything above it. Keeping
        // every level rather than only the smallest is what MOD-405's upward walk needs: a single
        // composite of the smallest level alone gives a wide but flat glow, because the detail the
        // larger levels still carry was thrown away on the way down.
        struct Level { RenderTarget2D* target; int width; int height; };
        std::vector<Level> levels;
        levels.reserve(static_cast<std::size_t>(iterations) + 1);

        int chainWidth  = std::max(kMinChainExtent, context.width / 2);
        int chainHeight = std::max(kMinChainExtent, context.height / 2);

        RenderTarget2D* extracted =
            pool_.acquire(chainWidth, chainHeight, format, DepthFormat::None, kExtractSlot);
        extractEffect_->Apply();
        extractEffect_->SetUniformVec4("uBloomParams", threshold, 0.0f, 0.0f, 0.0f);
        fullscreen_->draw(context.source, extracted, extractEffect_.get(), chainWidth, chainHeight,
                          linearClamp);
        levels.push_back({extracted, chainWidth, chainHeight});

        for (int iteration = 0; iteration < iterations; ++iteration)
        {
            const int nextWidth  = std::max(kMinChainExtent, chainWidth / 2);
            const int nextHeight = std::max(kMinChainExtent, chainHeight / 2);
            if (nextWidth == chainWidth && nextHeight == chainHeight)
                break;   // nothing left to halve

            const int slot = kLevelSlotBase + iteration;
            RenderTarget2D* horizontal =
                pool_.acquire(nextWidth, nextHeight, format, DepthFormat::None, kBlurSlot);
            blurEffect_->Apply();
            blurEffect_->SetUniformVec4("uBloomParams",
                                        1.0f / static_cast<float>(nextWidth), 0.0f,
                                        0.0f, 0.0f);
            fullscreen_->draw(levels.back().target, horizontal, blurEffect_.get(), nextWidth,
                              nextHeight, linearClamp);

            RenderTarget2D* vertical =
                pool_.acquire(nextWidth, nextHeight, format, DepthFormat::None, slot);
            blurEffect_->Apply();
            blurEffect_->SetUniformVec4("uBloomParams", 0.0f,
                                        1.0f / static_cast<float>(nextHeight),
                                        0.0f, 0.0f);
            fullscreen_->draw(horizontal, vertical, blurEffect_.get(), nextWidth, nextHeight,
                              linearClamp);

            levels.push_back({vertical, nextWidth, nextHeight});
            chainWidth  = nextWidth;
            chainHeight = nextHeight;
        }

        // ---- Up: add each level into the one above it (MOD-405) -------------------------------
        //
        // Progressive, not a single composite of the smallest level. Walking back up means the
        // finished glow carries every scale at once -- a tight core from the large levels and a
        // wide halo from the small ones -- which is the difference between bloom that looks like a
        // lens and bloom that looks like a blur.
        RenderTarget2D* accumulated = levels.back().target;
        if (upsampleEffect_ && upsampleEffect_->IsEffectValid())
        {
            for (std::size_t index = levels.size() - 1; index > 0; --index)
            {
                const Level& larger  = levels[index - 1];
                const Level& smaller = levels[index];

                RenderTarget2D* summed =
                    pool_.acquire(larger.width, larger.height, format, DepthFormat::None,
                                  kUpsampleSlotBase + static_cast<int>(index));
                upsampleEffect_->Apply();
                upsampleEffect_->SetUniformInt("uSmallerSampler", 1);
                upsampleEffect_->SetTexture(1, *accumulated);
                upsampleEffect_->SetUniformVec4(
                    "uBloomParams", 1.0f / static_cast<float>(smaller.width),
                    1.0f / static_cast<float>(smaller.height), manualFilter_ ? 1.0f : 0.0f,
                    0.0f);
                SetGlDualSamplerOrientation(
                    *upsampleEffect_, larger.target, accumulated);
                fullscreen_->draw(larger.target, summed, upsampleEffect_.get(), larger.width,
                                  larger.height, linearClamp);
                accumulated = summed;
            }
        }

        // ---- Composite the finished glow back onto the untouched scene ------------------------
        combineEffect_->Apply();
        combineEffect_->SetUniformInt("uBloomSampler", 1);
        combineEffect_->SetTexture(1, *accumulated);
        combineEffect_->SetUniformVec4("uBloomParams", intensity, 0.0f, 0.0f, 0.0f);
        SetGlDualSamplerOrientation(*combineEffect_, context.source, accumulated);
        fullscreen_->draw(context.source, context.destination, combineEffect_.get(),
                          context.width, context.height, linearClamp);
    }

    int BloomPass::iterationsForQuality(const RenderQuality quality)
    {
        switch (quality)
        {
        case RenderQuality::Low:    return 2;
        case RenderQuality::High:   return 5;
        case RenderQuality::Ultra:  return 7;
        case RenderQuality::Medium:
        default:                    return 3;
        }
    }

    const std::string& BloomPass::getName() const
    {
        static const std::string name = "Bloom";
        return name;
    }

    bool BloomPass::isSupported(GraphicsDevice& device) const
    {
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && extractEffect_ && extractEffect_->IsEffectValid()
            && blurEffect_ && blurEffect_->IsEffectValid()
            && upsampleEffect_ && upsampleEffect_->IsEffectValid()
            && combineEffect_ && combineEffect_->IsEffectValid();
    }

    float BloomPass::getThreshold() const            { return threshold_; }
    void  BloomPass::setThreshold(const float value) { threshold_ = value; }

    float BloomPass::getIntensity() const            { return intensity_; }
    void  BloomPass::setIntensity(const float value) { intensity_ = value; }

    int  BloomPass::getIterations() const          { return iterations_; }
    void BloomPass::setIterations(const int value) { iterations_ = value; }

    void BloomPass::resetTargets()
    {
        pool_.reset();
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
