// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/SsaoPass.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"
#include "CNA/GraphicsCapability.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "PostProcessShaderPackages.hpp"
#include "shaders/post_process/PostProcessShaderPackage.generated.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    namespace {

        constexpr int kMinSamples  = 8;
        constexpr int kMaxSamples  = 64;
        constexpr int kNoiseExtent = 4;

    } // namespace

    std::string SsaoPass::getOcclusionGlsl(const bool packed)
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        std::string source(kSsaoOcclusionEsFragmentSource);
        constexpr std::string_view policy = "uSsaoScalars[4]";
        for (std::size_t at = source.find(policy); at != std::string::npos;
             at = source.find(policy, at))
        {
            const std::string_view replacement = packed ? "1.0" : "0.0";
            source.replace(at, policy.size(), replacement);
            at += replacement.size();
        }
        return source;
    }

    SsaoPass::SsaoPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
        , pool_(device)
        , packedDepth_(DepthNormalPrepass::usesPackedDepthEXT(device))
    {
        const ShaderPackageEXT occlusionPackage = detail::CreateSsaoOcclusionShaderPackage();
        if (occlusionPackage.selectFor(device).isUsable())
            occlusionEffect_ = std::make_unique<ShaderEffect>(device, occlusionPackage);
        const ShaderPackageEXT composePackage = detail::CreateSsaoComposeShaderPackage();
        if (composePackage.selectFor(device).isUsable())
            composeEffect_ = std::make_unique<ShaderEffect>(device, composePackage);

        // plans/plan_modern.md MOD-219: a failed compile makes this pass copy its input through, which is
        // correct and completely silent. This names the pass and prints the compiler's log once.
        bool logged = false;
        detail::reportShaderCompileFailure(device, "SsaoPass (occlusion)", occlusionEffect_.get(),
                                           logged);
        detail::reportShaderCompileFailure(device, "SsaoPass (compose)", composeEffect_.get(),
                                           logged);

        generateKernel();

        // A deterministic 4x4 rotation texture. Deterministic on purpose: a seeded pattern makes
        // the pass reproducible frame to frame and test to test, and randomness here buys nothing
        // that a fixed well-distributed set does not.
        noiseTexture_ = std::make_unique<Texture2D>(device, kNoiseExtent, kNoiseExtent);
        std::vector<Color> noise;
        noise.reserve(static_cast<std::size_t>(kNoiseExtent) * kNoiseExtent);
        for (int index = 0; index < kNoiseExtent * kNoiseExtent; ++index)
        {
            const float angle = static_cast<float>(index) * 0.39269908f;   // 22.5 degrees apart
            const float x = std::cos(angle) * 0.5f + 0.5f;
            const float y = std::sin(angle) * 0.5f + 0.5f;
            noise.emplace_back(static_cast<int>(x * 255.0f), static_cast<int>(y * 255.0f), 0, 255);
        }
        noiseTexture_->SetData(noise.data(), static_cast<int>(noise.size()));
    }

    SsaoPass::~SsaoPass() = default;

    void SsaoPass::generateKernel()
    {
        kernel_.clear();
        kernel_.reserve(kMaxSamples);

        // A deterministic low-discrepancy set rather than a random one, for the same reason the
        // noise texture is fixed: the pass must produce the same image twice. The scale term biases
        // samples toward the origin so nearby geometry dominates -- without it, contact shadows
        // wash out into a uniform grey.
        for (int index = 0; index < kMaxSamples; ++index)
        {
            const float u1 = (static_cast<float>(index) + 0.5f) / static_cast<float>(kMaxSamples);
            // Van der Corput radical inverse, base 2.
            unsigned int bits = static_cast<unsigned int>(index);
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
            bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
            bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
            const float u2 = static_cast<float>(bits) * 2.3283064365386963e-10f;

            const float phi      = 6.2831853f * u1;
            const float cosTheta = u2;                       // biased toward the pole (+Z)
            const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));

            Vector3 sample(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);

            float scale = static_cast<float>(index) / static_cast<float>(kMaxSamples);
            scale = 0.1f + 0.9f * scale * scale;
            sample.X *= scale;
            sample.Y *= scale;
            sample.Z *= scale;
            kernel_.push_back(sample);
        }
    }

    void SsaoPass::apply(const PostProcessContext& context)
    {
        float radius    = radius_;
        float intensity = intensity_;
        int   samples   = sampleCount_;
        if (context.settings != nullptr)
        {
            radius    = context.settings->getSSAORadius();
            intensity = context.settings->getSSAOIntensity();
            samples   = context.settings->getSSAOSampleCount();
        }
        samples = std::clamp(samples, kMinSamples, kMaxSamples);

        const bool ready = occlusionEffect_ && occlusionEffect_->IsEffectValid()
                        && composeEffect_ && composeEffect_->IsEffectValid();
        const bool haveInputs = context.sourceDepth != nullptr && context.sourceNormals != nullptr;

        if (!ready || !haveInputs)
        {
            // Documented fallback: an unoccluded frame, not a failure. A pipeline that enables SSAO
            // without running a depth/normal prepass is misconfigured, not broken.
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        // MOD-523: the occlusion buffer, optionally at half resolution. AO is a low-frequency
        // signal -- it is a blurred estimate of a neighbourhood -- so halving the resolution costs
        // much less than it looks like it should, and the compose pass's bilinear read is the
        // upsample. It is off by default because "less than it looks like" is not "nothing": thin
        // contact shadows lose definition, which is exactly where AO earns its keep.
        const int occlusionWidth  = halfResolution_ ? std::max(1, context.width / 2) : context.width;
        const int occlusionHeight = halfResolution_ ? std::max(1, context.height / 2)
                                                    : context.height;
        RenderTarget2D* occlusion = pool_.acquire(occlusionWidth, occlusionHeight,
                                                  SurfaceFormat::Color, DepthFormat::None, 0);

        occlusionEffect_->Apply();
        occlusionEffect_->SetUniformInt("uNormalSampler", 1);
        occlusionEffect_->SetTexture(1, *context.sourceNormals);
        occlusionEffect_->SetUniformInt("uNoiseSampler", 2);
        occlusionEffect_->SetTexture(2, *noiseTexture_);
        occlusionEffect_->SetUniformVec3Array("uSsaoKernel", &kernel_[0].X, kMaxSamples);
        // Tiled against the *occlusion* buffer, not the frame: at half resolution a frame-sized
        // scale would repeat the 4x4 rotation twice as often and turn its pattern into visible
        // cross-hatching.
        const std::array noiseScale{
            static_cast<float>(occlusionWidth) / static_cast<float>(kNoiseExtent),
            static_cast<float>(occlusionHeight) / static_cast<float>(kNoiseExtent)};
        // The depth-side companion to the screen-space radius: how far, in the depth texture's own
        // 0..1 units, an occluder may be and still count. Tied to the radius so one setting still
        // controls "how big is the ambient neighbourhood", but never used as a UV offset.
        const std::array ssaoScalars{
            radius,
            0.005f,
            std::max(radius * 0.25f, 0.01f),
            static_cast<float>(samples),
            packedDepth_ ? 1.0f : 0.0f,
        };
        occlusionEffect_->SetUniformVec2Array("uSsaoVectors", noiseScale.data(), 1);
        occlusionEffect_->SetUniformFloatArray("uSsaoScalars", ssaoScalars.data(),
                                               static_cast<int>(ssaoScalars.size()));

        fullscreen_->draw(context.sourceDepth, occlusion, occlusionEffect_.get(),
                          occlusionWidth, occlusionHeight);

        composeEffect_->Apply();
        composeEffect_->SetUniformInt("uOcclusionSampler", 1);
        composeEffect_->SetTexture(1, *occlusion);
        // The blur folded into the compose pass steps by the occlusion buffer's texels, which at
        // half resolution are twice as wide -- so the blur covers the same *screen* distance either
        // way rather than halving with the buffer.
        const std::array texelSize{1.0f / static_cast<float>(occlusionWidth),
                                   1.0f / static_cast<float>(occlusionHeight)};
        const std::array composeScalars{intensity};
        composeEffect_->SetUniformVec2Array("uSsaoComposeVectors", texelSize.data(), 1);
        composeEffect_->SetUniformFloatArray("uSsaoComposeScalars", composeScalars.data(), 1);

        fullscreen_->draw(context.source, context.destination, composeEffect_.get(),
                          context.width, context.height);
    }

    int SsaoPass::sampleCountForQuality(const RenderQuality quality)
    {
        switch (quality)
        {
        case RenderQuality::Low:    return 8;
        case RenderQuality::High:   return 32;
        case RenderQuality::Ultra:  return 64;
        case RenderQuality::Medium:
        default:                    return 16;
        }
    }

    bool SsaoPass::isHalfResolution() const { return halfResolution_; }

    void SsaoPass::setHalfResolution(const bool value) { halfResolution_ = value; }

    const std::string& SsaoPass::getName() const
    {
        static const std::string name = "SSAO";
        return name;
    }

    bool SsaoPass::isSupported(GraphicsDevice& device) const
    {
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && occlusionEffect_ && occlusionEffect_->IsEffectValid()
            && composeEffect_ && composeEffect_->IsEffectValid();
    }

    float SsaoPass::getRadius() const            { return radius_; }
    void  SsaoPass::setRadius(const float value) { radius_ = value; }

    float SsaoPass::getIntensity() const            { return intensity_; }
    void  SsaoPass::setIntensity(const float value) { intensity_ = value; }

    int  SsaoPass::getSampleCount() const          { return sampleCount_; }
    void SsaoPass::setSampleCount(const int value) { sampleCount_ = value; }

    void SsaoPass::resetTargets()
    {
        pool_.reset();
    }

    const std::vector<Vector3>& SsaoPass::getKernel() const
    {
        return kernel_;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
