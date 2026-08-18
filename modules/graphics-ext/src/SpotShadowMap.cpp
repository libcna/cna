// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/SpotShadowMap.hpp"

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::MathHelper;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    namespace {

        /// Distance over range, same as the cube caster, so a receiver applies one rule to both.
        constexpr const char* kCasterVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
uniform mat4 uLightViewProjection;
uniform mat4 uWorld;
out vec3 vWorldPos;
void main() {
    vec4 world = uWorld * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    gl_Position = uLightViewProjection * world;
}
)";

        constexpr const char* kCasterFragmentSource = R"(#version 300 es
precision highp float;
in vec3 vWorldPos;
uniform vec3 uLightPosition;
uniform float uLightRange;
out vec4 FragColor;
void main() {
    float distance = clamp(length(vWorldPos - uLightPosition) / uLightRange, 0.0, 1.0);
    FragColor = vec4(distance, distance, distance, 1.0);
}
)";

        Vector3 Normalized(const Vector3& value)
        {
            const float lengthSquared = value.X * value.X + value.Y * value.Y + value.Z * value.Z;
            if (lengthSquared <= 1e-12f)
                return Vector3(0.0f, -1.0f, 0.0f);
            const float inverse = 1.0f / std::sqrt(lengthSquared);
            return Vector3(value.X * inverse, value.Y * inverse, value.Z * inverse);
        }

    } // namespace

    Matrix SpotShadowMap::computeLightView(const SpotLightEXT& light)
    {
        const Vector3 direction = Normalized(light.Direction);
        const Vector3 target(light.Position.X + direction.X, light.Position.Y + direction.Y,
                             light.Position.Z + direction.Z);
        // A cone pointing straight down or straight up is parallel to the obvious up vector, which
        // is the common case that produces a degenerate view and a blank map.
        const Vector3 up = std::abs(direction.Y) > 0.99f ? Vector3(0.0f, 0.0f, 1.0f)
                                                         : Vector3(0.0f, 1.0f, 0.0f);
        return Matrix::CreateLookAt(light.Position, target, up);
    }

    Matrix SpotShadowMap::computeLightProjection(const SpotLightEXT& light)
    {
        // Twice the outer HALF-angle. Building this from the half-angle instead covers half the
        // cone and leaves its whole rim permanently unshadowed, which looks like a range problem.
        const float fieldOfView = std::clamp(light.OuterAngle * 2.0f, 0.02f, 3.0f);
        const float nearPlane = std::max(light.Range * 0.005f, 0.01f);
        return Matrix::CreatePerspectiveFieldOfView(fieldOfView, 1.0f, nearPlane,
                                                    std::max(light.Range, nearPlane + 0.01f));
    }

    SpotShadowMap::SpotShadowMap(GraphicsDevice& device, const ShadowQuality quality)
        : device_(device), quality_(quality), size_(ShadowMap::sizeForQuality(quality))
    {
        const SurfaceFormat format =
            device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Single)
                ? SurfaceFormat::Single
                : SurfaceFormat::Color;

        target_ = std::make_unique<RenderTarget2D>(device, size_, size_, false, format,
                                                    DepthFormat::Depth24);

        const bool canRaster  = device.SupportsCapability(CNA::GraphicsCapability::ThreeD);
        const bool canCompile = device.SupportsCapability(CNA::GraphicsCapability::CustomEffects);
        if (canRaster && canCompile)
            casterEffect_ = std::make_unique<ShaderEffect>(device, kCasterVertexSource,
                                                           kCasterFragmentSource);
        supported_ = casterEffect_ != nullptr && casterEffect_->IsEffectValid();
        if (!supported_)
        {
            CNA::Logger::Info(
                "CNA::Graphics::SpotShadowMap: spot shadows are unavailable on this renderer. "
                "Passes still open and close, and the map keeps meaning nothing occludes, so the "
                "frame renders unshadowed rather than failing.",
                CNA::LogCategory::RENDER);
        }

        lightViewProjection_ = Matrix::getIdentityProperty();
    }

    SpotShadowMap::~SpotShadowMap() = default;

    bool SpotShadowMap::isSupported() const { return supported_; }

    int SpotShadowMap::getSize() const { return size_; }

    ShadowQuality SpotShadowMap::getQuality() const { return quality_; }

    Matrix SpotShadowMap::getLightViewProjection() const { return lightViewProjection_; }

    Vector3 SpotShadowMap::getLightPosition() const { return lightPosition_; }

    float SpotShadowMap::getLightRange() const { return lightRange_; }

    float SpotShadowMap::getDepthBias() const { return depthBias_; }

    void SpotShadowMap::setDepthBias(const float value) { depthBias_ = value; }

    Texture2D* SpotShadowMap::getShadowTexture() const { return target_.get(); }

    ShaderEffect* SpotShadowMap::getCasterEffect() const
    {
        return supported_ ? casterEffect_.get() : nullptr;
    }

    void SpotShadowMap::begin(const SpotLightEXT& light)
    {
        if (passOpen_)
            throw std::logic_error("CNA::Graphics::SpotShadowMap::begin: a pass is already open");
        if (!(light.Range > 0.0f))
            throw std::invalid_argument(
                "CNA::Graphics::SpotShadowMap::begin: the light's range must be positive -- the "
                "stored distance is divided by it");
        if (!(light.OuterAngle > 0.0f) || light.OuterAngle >= MathHelper::PiOver2)
            throw std::invalid_argument(
                "CNA::Graphics::SpotShadowMap::begin: the outer half-angle must be inside "
                "(0, pi/2) -- at or past that the cone is a hemisphere and one perspective "
                "projection cannot cover it");

        passOpen_      = true;
        lightPosition_ = light.Position;
        lightRange_    = light.Range;
        lightViewProjection_ = computeLightView(light) * computeLightProjection(light);

        device_.SetRenderTarget(target_.get());
        device_.Clear(Color::White);

        if (supported_)
        {
            casterEffect_->Apply();
            casterEffect_->SetUniformMat4("uLightViewProjection", &lightViewProjection_.M11);
            const Matrix identity = Matrix::getIdentityProperty();
            casterEffect_->SetUniformMat4("uWorld", &identity.M11);
            casterEffect_->SetUniformVec3("uLightPosition", lightPosition_.X, lightPosition_.Y,
                                          lightPosition_.Z);
            casterEffect_->SetUniformFloat("uLightRange", lightRange_);
        }
    }

    void SpotShadowMap::end()
    {
        if (!passOpen_)
            throw std::logic_error("CNA::Graphics::SpotShadowMap::end: no pass is open");
        passOpen_ = false;
        device_.SetRenderTarget(nullptr);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
