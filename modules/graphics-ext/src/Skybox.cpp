// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/Skybox.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using Microsoft::Xna::Framework::Graphics::TextureCube;

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

        // The ray reconstruction. uInvViewProj is the inverse of (rotation-only view) * projection,
        // so a point on the near plane taken back through it is already a direction from the origin
        // -- there is no camera position left in it to subtract.
        constexpr const char* kFragmentSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform samplerCube uEnvironment;
uniform mat4 uInvViewProj;
uniform float uYawSin;
uniform float uYawCos;
uniform float uIntensity;
uniform vec3 uTint;
void main() {
    vec2 ndc = TexCoord * 2.0 - 1.0;
    vec4 farPoint = uInvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 direction = normalize(farPoint.xyz / max(abs(farPoint.w), 1e-6) * sign(farPoint.w));
    // Yaw about Y, applied to the lookup rather than to the matrix, so the same environment can be
    // turned per draw without rebuilding anything.
    vec3 rotated = vec3(direction.x * uYawCos + direction.z * uYawSin,
                        direction.y,
                        -direction.x * uYawSin + direction.z * uYawCos);
    vec3 sky = texture(uEnvironment, rotated).rgb;
    FragColor = vec4(sky * uIntensity * uTint, 1.0);
    // texture1 is SpriteBatch's own source and is deliberately unused; referencing it keeps the
    // sampler from being optimised away, which would leave SpriteBatch binding to a dead uniform.
    FragColor.a = 1.0 + texture(texture1, TexCoord).a * 0.0;
}
)";

        Vector3 TransformPerspective(const Matrix& m, float x, float y, float z, float w)
        {
            const float rx = x * m.M11 + y * m.M21 + z * m.M31 + w * m.M41;
            const float ry = x * m.M12 + y * m.M22 + z * m.M32 + w * m.M42;
            const float rz = x * m.M13 + y * m.M23 + z * m.M33 + w * m.M43;
            const float rw = x * m.M14 + y * m.M24 + z * m.M34 + w * m.M44;
            const float inverseW = std::abs(rw) > 1e-6f ? 1.0f / std::abs(rw) : 1.0f;
            const float sign = rw < 0.0f ? -1.0f : 1.0f;
            return Vector3(rx * inverseW * sign, ry * inverseW * sign, rz * inverseW * sign);
        }

        Matrix RotationOnly(const Matrix& view)
        {
            Matrix result = view;
            result.M41 = 0.0f;
            result.M42 = 0.0f;
            result.M43 = 0.0f;
            return result;
        }

    } // namespace

    Vector3 Skybox::computeViewRay(const Matrix& view, const Matrix& projection, const float ndcX,
                                    const float ndcY, const float yaw)
    {
        const Matrix inverse = Matrix::Invert(RotationOnly(view) * projection);
        const Vector3 direction = TransformPerspective(inverse, ndcX, ndcY, 1.0f, 1.0f);

        const float length = std::sqrt(direction.X * direction.X + direction.Y * direction.Y +
                                       direction.Z * direction.Z);
        const Vector3 unit = length > 1e-6f
                                 ? Vector3(direction.X / length, direction.Y / length,
                                           direction.Z / length)
                                 : Vector3(0.0f, 0.0f, -1.0f);

        const float s = std::sin(yaw);
        const float c = std::cos(yaw);
        return Vector3(unit.X * c + unit.Z * s, unit.Y, -unit.X * s + unit.Z * c);
    }

    Skybox::Skybox(GraphicsDevice& device, TextureCube* environment)
        : device_(device), environment_(environment)
    {
        fullscreen_ = std::make_unique<FullscreenPass>(device);

        // SpriteBatch needs something to draw; the sky shader ignores it. One texel, so the
        // placeholder costs a texture object and nothing else.
        dummySource_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color white = Color::White;
        dummySource_->SetData(&white, 1);

        // Two questions, not one (plans/plan_modern.md `MOD-1699`): `CustomEffects` only means the
        // renderer *accepts* an effect. SOFTWARE and HEADLESS accept the sky shader and then draw
        // the fullscreen quad with their own fixed path, which fills the frame with the placeholder
        // texture's white -- a sky that is "supported" and shows no environment at all.
        if (device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && device.ExecutesShaderEffectSourceEXT())
            effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, kFragmentSource);
        supported_ = effect_ != nullptr && effect_->IsEffectValid();
    }

    Skybox::~Skybox() = default;

    bool Skybox::isSupported() const { return supported_; }

    TextureCube* Skybox::getEnvironment() const { return environment_; }

    void Skybox::setEnvironment(TextureCube* environment)
    {
        // Releasing the owned one first: attaching a borrowed cube over an owned one would
        // otherwise keep the owned one alive with nothing referring to it.
        ownedEnvironment_.reset();
        environment_ = environment;
    }

    void Skybox::setOwnedEnvironment(std::unique_ptr<TextureCube> environment)
    {
        ownedEnvironment_ = std::move(environment);
        environment_ = ownedEnvironment_.get();
    }

    float Skybox::getYaw() const { return yaw_; }

    void Skybox::setYaw(const float radians) { yaw_ = radians; }

    float Skybox::getIntensity() const { return intensity_; }

    void Skybox::setIntensity(const float intensity)
    {
        intensity_ = std::max(0.0f, intensity);
    }

    Vector3 Skybox::getTint() const { return tint_; }

    void Skybox::setTint(const Vector3& tint) { tint_ = tint; }

    void Skybox::draw(const Matrix& view, const Matrix& projection, const int width,
                      const int height)
    {
        if (width <= 0 || height <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::Skybox::draw: the target size must be positive");

        if (!supported_ || environment_ == nullptr)
        {
            if (!warned_)
            {
                warned_ = true;
                CNA::Logger::Info(
                    supported_
                        ? "CNA::Graphics::Skybox: no environment attached; the sky is skipped."
                        : "CNA::Graphics::Skybox: this renderer cannot compile the sky shader; "
                          "the sky is skipped and the scene renders without one.",
                    CNA::LogCategory::RENDER);
            }
            return;
        }

        const Matrix inverse = Matrix::Invert(RotationOnly(view) * projection);
        effect_->Apply();
        effect_->SetUniformMat4("uInvViewProj", &inverse.M11);
        effect_->SetUniformFloat("uYawSin", std::sin(yaw_));
        effect_->SetUniformFloat("uYawCos", std::cos(yaw_));
        effect_->SetUniformFloat("uIntensity", intensity_);
        effect_->SetUniformVec3("uTint", tint_.X, tint_.Y, tint_.Z);
        effect_->SetUniformInt("uEnvironment", 1);
        effect_->SetTexture(1, *environment_);

        // Over whatever is already bound: the scene target inside a pipeline frame, the back
        // buffer outside one. Binding a destination here would mean the caller had to know which
        // of the two it currently was.
        fullscreen_->drawOverCurrentTarget(dummySource_.get(), effect_.get(), width, height);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
