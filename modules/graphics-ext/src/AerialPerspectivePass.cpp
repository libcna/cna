// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/AerialPerspectivePass.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"
#include "CNA/GraphicsCapability.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/PostProcessContext.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "AerialPerspectiveShaderPackage.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    namespace {

        float AirMassAlongDirection(const float upwards)
        {
            const float up = std::clamp(upwards, 0.0f, 1.0f);
            const float zenithDegrees = std::acos(up) * 57.29577951308232f;
            return 1.0f / std::max(up + 0.50572f * std::pow(std::max(96.07995f - zenithDegrees,
                                                                     1e-3f), -1.6364f), 1e-4f);
        }

    } // namespace

    AerialPerspectivePass::AerialPerspectivePass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
        , packedDepth_(DepthNormalPrepass::usesPackedDepthEXT(device))
    {
        const ShaderPackageEXT package = detail::CreateAerialPerspectiveShaderPackage();
        if (package.selectFor(device).isUsable())
            effect_ = std::make_unique<ShaderEffect>(device, package);

        bool logged = false;
        detail::reportShaderCompileFailure(device, "AerialPerspectivePass", effect_.get(), logged);
    }

    AerialPerspectivePass::~AerialPerspectivePass() = default;

    float AerialPerspectivePass::airMassForDistance(const Vector3& viewDirection,
                                                    const float distance, const float scaleHeight)
    {
        const float length = std::sqrt(viewDirection.X * viewDirection.X
                                     + viewDirection.Y * viewDirection.Y
                                     + viewDirection.Z * viewDirection.Z);
        const float upwards = length > 1e-6f ? viewDirection.Y / length : 1.0f;
        const float full = AirMassAlongDirection(upwards);
        return std::min(std::max(distance, 0.0f) / std::max(scaleHeight, 1e-3f), full);
    }

    Vector3 AerialPerspectivePass::transmittance(const float turbidity, const float airMass)
    {
        const Vector3 rayleigh(0.0464f, 0.1085f, 0.2650f);
        const float mie = 0.021f * std::max(turbidity - 1.0f, 0.0f);
        return Vector3(std::exp(-(rayleigh.X + mie) * airMass),
                       std::exp(-(rayleigh.Y + mie) * airMass),
                       std::exp(-(rayleigh.Z + mie) * airMass));
    }

    void AerialPerspectivePass::apply(const PostProcessContext& context)
    {
        fallbackReason_.clear();

        if (!effect_ || !effect_->IsEffectValid())
            fallbackReason_ = "the pass shader did not compile";
        else if (context.sourceDepth == nullptr)
            fallbackReason_ = "no depth image was supplied, so there is no distance to put air over";
        else if (context.farPlane <= 0.0f)
            fallbackReason_ = "no far plane was supplied, so the stored depth has no world scale";
        else if (context.inverseView.M44 == 0.0f || context.inverseProjection.M44 == 0.0f)
            fallbackReason_ = "no camera matrices were supplied, so there is no view ray to look "
                              "along";

        if (!fallbackReason_.empty())
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        // The camera's *rotation* only, which is the same trick `AtmosphericSky` uses and for the
        // same reason: this pass wants a direction, and a direction must not move when the camera
        // does. Zeroing the translation row of the inverse view leaves exactly the inverse of the
        // view rotation, so no matrix has to be inverted here.
        Microsoft::Xna::Framework::Matrix rotationOnly = context.inverseView;
        rotationOnly.M41 = 0.0f;
        rotationOnly.M42 = 0.0f;
        rotationOnly.M43 = 0.0f;
        const Microsoft::Xna::Framework::Matrix inverseViewProjection =
            context.inverseProjection * rotationOnly;

        effect_->Apply();
        effect_->SetUniformInt("uDepthSampler", 1);
        effect_->SetTexture(1, *context.sourceDepth);
        std::array<float, 32> matrices{};
        inverseViewProjection.ToColumnMajor(matrices.data());
        context.inverseProjection.ToColumnMajor(matrices.data() + 16);
        const std::array sunDirection{
            sunDirection_.X, sunDirection_.Y, sunDirection_.Z};
        const std::array scalars{
            std::max(turbidity_, 1.0f),
            std::max(intensity_, 0.0f),
            std::max(scaleHeight_, 1e-3f),
            context.farPlane,
            packedDepth_ ? 1.0f : 0.0f,
        };
        effect_->SetUniformMat4Array("uAerialMatrices", matrices.data(), 2);
        effect_->SetUniformVec3Array("uAerialVectors", sunDirection.data(), 1);
        effect_->SetUniformFloatArray("uAerialScalars", scalars.data(),
                                      static_cast<int>(scalars.size()));

        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    const std::string& AerialPerspectivePass::getName() const
    {
        static const std::string name = "AerialPerspective";
        return name;
    }

    bool AerialPerspectivePass::isSupported(GraphicsDevice& device) const
    {
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && effect_ && effect_->IsEffectValid();
    }

    Vector3 AerialPerspectivePass::getSunDirection() const { return sunDirection_; }

    void AerialPerspectivePass::setSunDirection(const Vector3& value) { sunDirection_ = value; }

    float AerialPerspectivePass::getTurbidity() const { return turbidity_; }

    void AerialPerspectivePass::setTurbidity(const float value)
    {
        turbidity_ = std::max(value, 1.0f);
    }

    float AerialPerspectivePass::getIntensity() const { return intensity_; }

    void AerialPerspectivePass::setIntensity(const float value)
    {
        if (value >= 0.0f) intensity_ = value;
    }

    float AerialPerspectivePass::getScaleHeight() const { return scaleHeight_; }

    void AerialPerspectivePass::setScaleHeight(const float value)
    {
        scaleHeight_ = std::max(value, 1e-3f);
    }

    const std::string& AerialPerspectivePass::getFallbackReason() const { return fallbackReason_; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
