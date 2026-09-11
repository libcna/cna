// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ContactShadowPass.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"
#include "CNA/GraphicsCapability.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/PostProcessContext.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "PostProcessShaderPackages.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    namespace {

        constexpr int kMinSteps = 4;
        constexpr int kMaxSteps = 64;

    } // namespace

    ContactShadowPass::ContactShadowPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
        , packedDepth_(DepthNormalPrepass::usesPackedDepthEXT(device))
    {
        const ShaderPackageEXT package = detail::CreateContactShadowShaderPackage();
        if (package.selectFor(device).isUsable())
            effect_ = std::make_unique<ShaderEffect>(device, package);

        bool logged = false;
        detail::reportShaderCompileFailure(device, "ContactShadowPass", effect_.get(), logged);
    }

    ContactShadowPass::~ContactShadowPass() = default;

    std::string ContactShadowPass::getOcclusionTestGlsl()
    {
        return R"(
bool cnaContactOccluded(float rayViewDepth, float sceneViewDepth, float bias, float thickness) {
    float difference = rayViewDepth - sceneViewDepth;
    return difference > bias && difference < thickness;
}
)";
    }

    bool ContactShadowPass::isOccluded(const float rayViewDepth, const float sceneViewDepth,
                                       const float bias, const float thickness)
    {
        const float difference = rayViewDepth - sceneViewDepth;
        return difference > bias && difference < thickness;
    }

    float ContactShadowPass::combineVisibility(const float shadowMapVisibility,
                                               const float contactVisibility)
    {
        return std::clamp(shadowMapVisibility, 0.0f, 1.0f)
             * std::clamp(contactVisibility, 0.0f, 1.0f);
    }

    void ContactShadowPass::apply(const PostProcessContext& context)
    {
        fallbackReason_.clear();

        if (!effect_ || !effect_->IsEffectValid())
            fallbackReason_ = "the pass shader did not compile";
        else if (context.sourceDepth == nullptr)
            fallbackReason_ = "no depth image was supplied, so there is nothing to march through";
        else if (context.farPlane <= 0.0f)
            fallbackReason_ = "no far plane was supplied, so the stored depth has no world scale";
        else if (context.inverseView.M44 == 0.0f)
            fallbackReason_ = "no inverse view matrix was supplied, so the light direction cannot "
                              "be brought into view space";

        if (!fallbackReason_.empty())
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        // The march wants the direction *toward* the light; the setter takes the direction light
        // travels, so the two differ by a sign. TransformNormal rather than Transform because a
        // direction must not pick up the camera's translation.
        Vector3 toLight(-lightDirection_.X, -lightDirection_.Y, -lightDirection_.Z);
        const float length = std::sqrt(toLight.X * toLight.X + toLight.Y * toLight.Y
                                     + toLight.Z * toLight.Z);
        if (length < 1e-6f)
        {
            fallbackReason_ = "the light direction is degenerate, so there is no ray to march";
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }
        toLight.X /= length;
        toLight.Y /= length;
        toLight.Z /= length;

        const Matrix view = Matrix::Invert(context.inverseView);
        Vector3 viewLight = Vector3::TransformNormal(toLight, view);
        const float viewLength = std::sqrt(viewLight.X * viewLight.X + viewLight.Y * viewLight.Y
                                         + viewLight.Z * viewLight.Z);
        if (viewLength > 1e-6f)
        {
            viewLight.X /= viewLength;
            viewLight.Y /= viewLength;
            viewLight.Z /= viewLength;
        }

        const int steps = std::clamp(stepCount_, kMinSteps, kMaxSteps);

        effect_->Apply();
        effect_->SetUniformInt("uDepthSampler", 1);
        effect_->SetTexture(1, *context.sourceDepth);
        std::array<float, 32> matrices{};
        context.projection.ToColumnMajor(matrices.data());
        context.inverseProjection.ToColumnMajor(matrices.data() + 16);
        const std::array direction{viewLight.X, viewLight.Y, viewLight.Z};
        const std::array depthSize{
            static_cast<float>(context.sourceDepth->getWidthProperty()),
            static_cast<float>(context.sourceDepth->getHeightProperty()),
        };
        // The bounded step count is exactly representable as a float in its 4..64 range and every
        // package variant converts it back to the loop bound it needs.
        const std::array scalars{
            context.farPlane,
            std::max(maxDistance_, 1e-4f),
            std::max(thickness_, 0.0f),
            std::max(bias_, 0.0f),
            std::clamp(intensity_, 0.0f, 1.0f),
            static_cast<float>(steps),
            packedDepth_ ? 1.0f : 0.0f,
        };
        effect_->SetUniformMat4Array("uContactMatrices", matrices.data(), 2);
        effect_->SetUniformVec3Array("uContactDirections", direction.data(), 1);
        effect_->SetUniformVec2Array("uContactVectors", depthSize.data(), 1);
        effect_->SetUniformFloatArray("uContactScalars", scalars.data(),
                                      static_cast<int>(scalars.size()));

        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    const std::string& ContactShadowPass::getName() const
    {
        static const std::string name = "ContactShadow";
        return name;
    }

    bool ContactShadowPass::isSupported(GraphicsDevice& device) const
    {
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && effect_ && effect_->IsEffectValid();
    }

    Vector3 ContactShadowPass::getLightDirection() const { return lightDirection_; }

    void ContactShadowPass::setLightDirection(const Vector3& value) { lightDirection_ = value; }

    float ContactShadowPass::getMaxDistance() const            { return maxDistance_; }
    void  ContactShadowPass::setMaxDistance(const float value) { maxDistance_ = value; }

    int  ContactShadowPass::getStepCount() const          { return stepCount_; }
    void ContactShadowPass::setStepCount(const int value) { stepCount_ = value; }

    float ContactShadowPass::getThickness() const            { return thickness_; }
    void  ContactShadowPass::setThickness(const float value) { thickness_ = value; }

    float ContactShadowPass::getIntensity() const            { return intensity_; }
    void  ContactShadowPass::setIntensity(const float value) { intensity_ = value; }

    float ContactShadowPass::getBias() const            { return bias_; }
    void  ContactShadowPass::setBias(const float value) { bias_ = value; }

    const std::string& ContactShadowPass::getFallbackReason() const { return fallbackReason_; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
