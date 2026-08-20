// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/AerialPerspectivePass.hpp"
#include "CNA/Graphics/AtmosphericSky.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/PostProcessContext.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cmath>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    namespace {

        constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

        // Two matrices, because the two questions this pass asks are in different spaces. The world
        // direction decides how much atmosphere is above the ray and where the sun is relative to
        // it, and both of those are world quantities. The ray *length* is a view quantity: the
        // prepass records distance along view -Z, and a pixel at the edge of a wide frame is
        // further from the eye than its depth says. Using the depth directly would thin the
        // atmosphere toward the corners of the screen -- a vignette in reverse, which reads as a
        // lens artefact rather than as air.
        constexpr const char* kFragmentBody = R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform sampler2D uDepthSampler;
uniform mat4  uInverseViewProjection;
uniform mat4  uInverseProjection;
uniform vec3  uSunDirection;
uniform float uTurbidity;
uniform float uIntensity;
uniform float uScaleHeight;
uniform float uFarPlane;

const float kCnaSkyDepth = 0.999;

void main() {
    vec4 source = texture(texture1, TexCoord);
    float depth = cnaDecodeLinearDepth(texture(uDepthSampler, TexCoord));

    // Nothing was drawn here: this pixel *is* the sky, which already carries the whole atmosphere.
    // Adding more would double it, and the seam where geometry meets sky is exactly where an error
    // of that kind is most visible.
    if (depth <= 0.0 || depth >= kCnaSkyDepth) {
        FragColor = source;
        return;
    }

    vec2 ndc = TexCoord * 2.0 - 1.0;

    vec4 world = uInverseViewProjection * vec4(ndc, 1.0, 1.0);
    vec3 direction = normalize(world.xyz / world.w);

    vec4 viewRay = uInverseProjection * vec4(ndc, 1.0, 1.0);
    vec3 view = viewRay.xyz / viewRay.w;
    float alongRay = depth * uFarPlane * (length(view) / max(-view.z, 1e-4));

    float airMass = cnaAerialAirMass(direction, alongRay, uScaleHeight);

    // The two halves of `cnaAerialPerspective`, separated only so the intensity multiplies the
    // scattered light and not the geometry's own colour. Applying it to both -- by dividing on the
    // way in and multiplying on the way out -- gives the same answer and divides by zero when a
    // caller turns the sun off.
    vec3 graded = source.rgb * cnaAtmosphereTransmittance(uTurbidity, airMass)
                + cnaScatteringAlongPath(direction, uSunDirection, uTurbidity, airMass)
                  * uIntensity;
    FragColor = vec4(graded, source.a);
}
)";

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
    {
        std::string source = "#version 300 es\nprecision highp float;\n";
        source += DepthNormalPrepass::getDepthDecodeGlsl(
            DepthNormalPrepass::usesPackedDepthEXT(device));
        // MOD-2141: the sky's own model, emitted rather than restated here.
        source += AtmosphericSky::getModelGlsl();
        source += kFragmentBody;
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, source);

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
        effect_->SetUniformMat4("uInverseViewProjection", &inverseViewProjection.M11);
        effect_->SetUniformMat4("uInverseProjection", &context.inverseProjection.M11);
        effect_->SetUniformVec3("uSunDirection", sunDirection_.X, sunDirection_.Y, sunDirection_.Z);
        effect_->SetUniformFloat("uTurbidity", std::max(turbidity_, 1.0f));
        effect_->SetUniformFloat("uIntensity", std::max(intensity_, 0.0f));
        effect_->SetUniformFloat("uScaleHeight", std::max(scaleHeight_, 1e-3f));
        effect_->SetUniformFloat("uFarPlane", context.farPlane);

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
        return PostProcessPass::isSupported(device) && effect_ && effect_->IsEffectValid();
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
