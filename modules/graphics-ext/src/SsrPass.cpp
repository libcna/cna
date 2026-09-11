// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/SsrPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"
#include "CNA/GraphicsCapability.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "PostProcessShaderPackages.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    SsrPass::SsrPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
        , packedDepth_(DepthNormalPrepass::usesPackedDepthEXT(device))
    {
        const ShaderPackageEXT package = detail::CreateSsrShaderPackage();
        if (package.selectFor(device).isUsable())
            effect_ = std::make_unique<ShaderEffect>(device, package);
        bool logged = false;
        detail::reportShaderCompileFailure(device, "SsrPass", effect_.get(), logged);
    }

    SsrPass::~SsrPass() = default;

    void SsrPass::apply(const PostProcessContext& context)
    {
        // A camera is as much an input as the two images: without a projection there is no view
        // space to march in, and a pass that guessed one would reflect the scene through an
        // invented lens. The pipeline supplies it from `setCamera`; a caller who never called that
        // gets its frame back rather than a wrong reflection.
        const bool haveInputs = context.sourceDepth != nullptr &&
                                context.sourceNormals != nullptr && context.farPlane > 0.0f;
        if (effect_ == nullptr || !effect_->IsEffectValid() || !haveInputs)
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        const float far = context.farPlane;

        // The settings bag wins where one is supplied, matching every other pass: a pipeline that
        // applied a quality preset must not be overruled by a pass-local default nobody set.
        const RenderPipelineSettings* settings = context.settings;
        const float maxDistance = settings != nullptr ? settings->getSSRMaxDistance() : maxDistance_;
        const float thickness   = settings != nullptr ? settings->getSSRThickness()   : thickness_;
        const float depthBias   = settings != nullptr ? settings->getSSRDepthBias()   : depthBias_;
        const float edgeFade    = settings != nullptr ? settings->getSSREdgeFade()    : edgeFade_;
        const float roughnessBlur =
            settings != nullptr ? settings->getSSRRoughnessBlur() : roughnessBlur_;
        const float intensity   = settings != nullptr ? settings->getSSRIntensity()   : intensity_;
        const int   stepCount   = settings != nullptr ? settings->getSSRStepCount()   : stepCount_;

        effect_->Apply();
        effect_->SetUniformInt("uDepthSampler", 1);
        effect_->SetTexture(1, *context.sourceDepth);
        effect_->SetUniformInt("uNormalSampler", 2);
        effect_->SetTexture(2, *context.sourceNormals);

        std::array<float, 32> matrices{};
        context.projection.ToColumnMajor(matrices.data());
        context.inverseProjection.ToColumnMajor(matrices.data() + 16);
        const std::array depthSize{
            static_cast<float>(context.width), static_cast<float>(context.height)};
        // Every world-space distance crosses into the prepass's far-plane-normalised view space
        // here and nowhere else. The step count is exactly representable as a float in its 4..64
        // range and converted back to an integer by every shader variant.
        const std::array scalars{
            far,
            maxDistance / far,
            depthBias / far,
            thickness / far,
            intensity,
            edgeFade,
            roughnessBlur,
            static_cast<float>(std::clamp(stepCount, kMinStepCount, kMaxStepCount)),
            packedDepth_ ? 1.0f : 0.0f,
        };
        effect_->SetUniformMat4Array("uSsrMatrices", matrices.data(), 2);
        effect_->SetUniformVec2Array("uSsrVectors", depthSize.data(), 1);
        effect_->SetUniformFloatArray("uSsrScalars", scalars.data(),
                                      static_cast<int>(scalars.size()));

        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    const std::string& SsrPass::getName() const
    {
        static const std::string name = "SSR";
        return name;
    }

    bool SsrPass::isSupported(GraphicsDevice& device) const
    {
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && effect_ && effect_->IsEffectValid();
    }

    float SsrPass::getMaxDistance() const { return maxDistance_; }
    void  SsrPass::setMaxDistance(const float value)
    {
        if (value > 0.0f) maxDistance_ = value;
    }

    int  SsrPass::getStepCount() const { return stepCount_; }
    void SsrPass::setStepCount(const int value) { stepCount_ = value; }

    float SsrPass::getThickness() const { return thickness_; }
    void  SsrPass::setThickness(const float value)
    {
        if (value > 0.0f) thickness_ = value;
    }

    float SsrPass::getDepthBias() const { return depthBias_; }
    void  SsrPass::setDepthBias(const float value)
    {
        if (value > 0.0f) depthBias_ = value;
    }

    float SsrPass::getRoughnessBlur() const { return roughnessBlur_; }
    void  SsrPass::setRoughnessBlur(const float value)
    {
        roughnessBlur_ = std::clamp(value, 0.0f, 0.25f);
    }

    float SsrPass::getEdgeFade() const { return edgeFade_; }
    void  SsrPass::setEdgeFade(const float value)
    {
        edgeFade_ = std::clamp(value, 0.0f, 0.5f);
    }

    float SsrPass::getIntensity() const { return intensity_; }
    void  SsrPass::setIntensity(const float value) { intensity_ = value; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
