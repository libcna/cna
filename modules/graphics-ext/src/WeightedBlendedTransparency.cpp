// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/WeightedBlendedTransparency.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "LensPassVertexSource.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Graphics::Blend;
    using Microsoft::Xna::Framework::Graphics::BlendFunction;
    using Microsoft::Xna::Framework::Graphics::BlendState;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::DepthStencilState;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::RenderTargetBinding;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    namespace {

        constexpr const char* kVertexSource = detail::kLensVertexSource;

        /// The smallest transmission a surface is allowed to have. A fully opaque surface would
        /// otherwise accumulate log(0), and one of those poisons the whole pixel to black.
        constexpr float kMinTransmission = 1e-4f;

        constexpr const char* kResolveSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;        // accumulation: rgb = sum(colour * alpha * weight), a = sum(alpha * weight)
uniform sampler2D uRevealage;      // r = sum(log(1 - alpha))

void main() {
    vec4 accumulation = texture(texture1, TexCoord);
    // exp of the accumulated logs is the product of the transmissions -- the revealage the
    // published technique accumulates multiplicatively. See the header for why it is a sum here.
    float revealage = clamp(exp(texture(uRevealage, TexCoord).r), 0.0, 1.0);

    // Nothing transparent covered this pixel: leave the frame exactly as it was. Not "blend with a
    // zero contribution" -- an exact early-out, because a pass that perturbs untouched pixels
    // cannot be left in a chain.
    if (revealage > 0.9999) discard;

    // The weights cancel: the ratio is a weighted average of the surfaces' colours, and the weight
    // itself only decided how much each one counted towards it.
    vec3 colour = accumulation.rgb / max(accumulation.a, 1e-5);
    FragColor = vec4(colour, 1.0 - revealage);
}
)";

    } // namespace

    std::string WeightedBlendedTransparency::getAccumulationGlsl()
    {
        return R"(
layout(location = 0) out vec4 cnaOitAccumulation;
layout(location = 1) out vec4 cnaOitRevealage;
uniform float uCnaOitFarPlane;

/// McGuire and Bavoil's depth weight: near surfaces count for more, and the range is clamped at
/// both ends so a distant surface still contributes something and a near one cannot swamp the
/// buffer's precision.
float cnaOitWeight(float viewDepth, float alpha) {
    float z = clamp(viewDepth / max(uCnaOitFarPlane, 1e-4), 0.0, 1.0);
    return alpha * clamp(0.03 / (1e-5 + pow(z, 4.0)), 1e-2, 3e3);
}

/// What a transparent shader writes instead of FragColor.
void cnaOitEmit(vec3 colour, float alpha, float viewDepth) {
    float w = cnaOitWeight(viewDepth, alpha);
    cnaOitAccumulation = vec4(colour * alpha * w, alpha * w);
    // Summed rather than multiplied, so both targets share one blend state and the geometry is
    // drawn once; the resolve exponentiates it back.
    cnaOitRevealage = vec4(log(max(1.0 - alpha, 1e-4)), 0.0, 0.0, 0.0);
}
)";
    }

    float WeightedBlendedTransparency::weight(const float viewDepth, const float alpha,
                                              const float farPlane)
    {
        // Line for line with cnaOitWeight above, including the order of the operations and the
        // guards: this exists to be compared against it on the GPU, and a twin that reassociated
        // the arithmetic would disagree in the last bits for a reason nobody could then locate.
        const float z = std::clamp(viewDepth / std::max(farPlane, 1e-4f), 0.0f, 1.0f);
        return alpha * std::clamp(0.03f / (1e-5f + std::pow(z, 4.0f)), 1e-2f, 3e3f);
    }

    WeightedBlendedTransparency::WeightedBlendedTransparency(GraphicsDevice& device,
                                                             const int width, const int height)
        : device_(device), fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        if (width <= 0 || height <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::WeightedBlendedTransparency: the target size must be positive");
        width_  = width;
        height_ = height;

        if (!device.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets))
            unsupportedReason_ =
                "this renderer has no multiple render targets, and the accumulation needs two";
        else if (!device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable))
            unsupportedReason_ =
                "this renderer has no half-float render target, and the accumulation sums values "
                "far outside 0..1";
        else if (!device.ExecutesShaderEffectSourceEXT())
            unsupportedReason_ =
                "this renderer accepts effect source without running it, so neither the "
                "accumulation nor the resolve would execute";

        allocateTargets();

        if (unsupportedReason_.empty())
        {
            resolveEffect_ = std::make_unique<ShaderEffect>(device, kVertexSource, kResolveSource);
            bool logged = false;
            detail::reportShaderCompileFailure(device, "WeightedBlendedTransparency",
                                               resolveEffect_.get(), logged);
            if (!resolveEffect_->IsEffectValid())
            {
                unsupportedReason_ = "the resolve shader did not compile on this device";
                resolveEffect_.reset();
            }
        }

        // Additive on both channels and both targets -- One/One rather than BlendState::Additive,
        // which is SourceAlpha/One and would scale every contribution by its own alpha a second
        // time on top of the premultiply the emit already did.
        accumulateBlend_ = std::make_unique<BlendState>();
        accumulateBlend_->setColorSourceBlendProperty(Blend::One);
        accumulateBlend_->setColorDestinationBlendProperty(Blend::One);
        accumulateBlend_->setAlphaSourceBlendProperty(Blend::One);
        accumulateBlend_->setAlphaDestinationBlendProperty(Blend::One);
        accumulateBlend_->setColorBlendFunctionProperty(BlendFunction::Add);
        accumulateBlend_->setAlphaBlendFunctionProperty(BlendFunction::Add);
    }

    WeightedBlendedTransparency::~WeightedBlendedTransparency() = default;

    void WeightedBlendedTransparency::allocateTargets()
    {
        if (!unsupportedReason_.empty()) return;
        accumulation_ = std::make_unique<RenderTarget2D>(device_, width_, height_, false,
                                                         SurfaceFormat::HdrBlendable,
                                                         DepthFormat::Depth24);
        revealage_ = std::make_unique<RenderTarget2D>(device_, width_, height_, false,
                                                       SurfaceFormat::HdrBlendable,
                                                       DepthFormat::Depth24);
    }

    bool WeightedBlendedTransparency::isSupported() const
    {
        return unsupportedReason_.empty() && resolveEffect_ != nullptr;
    }

    const std::string& WeightedBlendedTransparency::getUnsupportedReason() const
    {
        return unsupportedReason_;
    }

    bool WeightedBlendedTransparency::isAccumulating() const { return accumulating_; }

    Texture2D* WeightedBlendedTransparency::getAccumulationTextureEXT() const
    {
        return accumulation_.get();
    }

    Texture2D* WeightedBlendedTransparency::getRevealageTextureEXT() const
    {
        return revealage_.get();
    }

    void WeightedBlendedTransparency::resize(const int width, const int height)
    {
        if (width <= 0 || height <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::WeightedBlendedTransparency::resize: the size must be positive");
        if (accumulating_)
            throw std::logic_error(
                "CNA::Graphics::WeightedBlendedTransparency::resize: accumulation is open");
        if (width == width_ && height == height_) return;
        width_  = width;
        height_ = height;
        allocateTargets();
    }

    void WeightedBlendedTransparency::begin(const float farPlane)
    {
        if (farPlane <= 0.0f)
            throw std::invalid_argument(
                "CNA::Graphics::WeightedBlendedTransparency::begin: the far plane must be positive");
        if (accumulating_)
            throw std::logic_error(
                "CNA::Graphics::WeightedBlendedTransparency::begin: accumulation is already open");

        // The bracket opens whether or not the resolve can run -- the same correction ShadowMap
        // needed (plans/plan_modern.md MOD-1697) and for the same reason. Returning here before
        // accumulating_ was set left a renderer that cannot resolve with a begin() that reported
        // success and an end() that threw "accumulation is not open", so the second half of a
        // correctly paired call failed, on exactly the renderers least able to explain why. What
        // the unsupported path skips is the device work, not the bracket.
        if (isSupported())
        {
            try
            {
                const std::vector<RenderTargetBinding> bindings = {
                    RenderTargetBinding(accumulation_.get()),
                    RenderTargetBinding(revealage_.get()),
                };
                device_.SetRenderTargets(bindings);
                // Both targets clear to zero: an empty accumulation is a zero sum, and a zero sum
                // of logs exponentiates to a revealage of 1 -- "nothing covered this pixel", which
                // is what the resolve early-outs on.
                device_.Clear(Color(0, 0, 0, 0));

                device_.setBlendStateProperty(*accumulateBlend_);
                // Depth testing on, depth writing off: a transparent surface must be hidden by the
                // opaque geometry in front of it and must not hide the transparent ones behind it.
                device_.setDepthStencilStateProperty(DepthStencilState::DepthRead);
            }
            catch (...)
            {
                // A renderer that refuses the two-target bind leaves the object usable rather than
                // permanently half-open: nothing is recorded as bound, the bracket stays closed,
                // and a later begin() starts from the beginning instead of reporting "already
                // open".
                boundTargets_ = false;
                try { device_.SetRenderTarget(nullptr); } catch (...) { /* best-effort cleanup */ }
                throw;
            }
            boundTargets_ = true;
        }
        accumulating_ = true;
    }

    void WeightedBlendedTransparency::end()
    {
        if (!accumulating_)
            throw std::logic_error(
                "CNA::Graphics::WeightedBlendedTransparency::end: accumulation is not open");
        accumulating_ = false;
        // Restore exactly what the matching begin() changed. Where it bound nothing there is
        // nothing to unbind, and resetting the blend and depth states here would clobber whatever
        // the caller had set -- a side effect the supported path does not have, since there it is
        // undoing its own.
        if (!boundTargets_) return;
        boundTargets_ = false;
        device_.SetRenderTarget(nullptr);
        device_.setBlendStateProperty(BlendState::Opaque);
        device_.setDepthStencilStateProperty(DepthStencilState::Default);
    }

    void WeightedBlendedTransparency::resolve(const int width, const int height)
    {
        if (width <= 0 || height <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::WeightedBlendedTransparency::resolve: the size must be positive");
        if (accumulating_)
            throw std::logic_error(
                "CNA::Graphics::WeightedBlendedTransparency::resolve: accumulation is still open");
        if (!isSupported()) return;

        resolveEffect_->Apply();
        resolveEffect_->SetUniformInt("uRevealage", 1);
        resolveEffect_->SetTexture(1, *revealage_);
        fullscreen_->drawOverCurrentTarget(accumulation_.get(), resolveEffect_.get(), width, height);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
