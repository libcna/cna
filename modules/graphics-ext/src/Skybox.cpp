// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/Skybox.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "shaders/skybox/SkyboxShaderPackage.generated.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using Microsoft::Xna::Framework::Graphics::TextureCube;

    namespace {

        [[nodiscard]] std::vector<std::uint8_t> ToBytes(
            const std::uint32_t* words, const std::size_t byteSize)
        {
            const auto* begin = reinterpret_cast<const std::uint8_t*>(words);
            return std::vector<std::uint8_t>(begin, begin + byteSize);
        }

        [[nodiscard]] ShaderPackageEXT CreateSkyboxShaderPackage()
        {
            using ShaderCodeEXT = CNA::Graphics::ShaderCodeEXT;
            using namespace CNA::Graphics::detail::SkyboxGenerated;
            return ShaderPackageEXT(
                {
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "skybox/skybox.es.vert.glsl",
                                  std::string(kEsVertexSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  "skybox/skybox.es.frag.glsl",
                                  std::string(kEsFragmentSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "skybox/skybox.desktop.vert.glsl",
                                  std::string(kDesktopVertexSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  "skybox/skybox.desktop.frag.glsl",
                                  std::string(kDesktopFragmentSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "skybox/skybox.vulkan.vert.spv",
                                  ToBytes(kVulkanVertexSpirV,
                                          kVulkanVertexSpirVByteSize)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  "skybox/skybox.vulkan.frag.spv",
                                  ToBytes(kVulkanFragmentSpirV,
                                          kVulkanFragmentSpirVByteSize)),
                },
                {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment},
                {ShaderBindingRequirementEXT(
                    "uEnvironment", 1, ShaderBindingTypeEXT::SampledTextureCube,
                    CNA::ShaderStageEXT::Fragment)});
        }

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

        // MOD-2238: exact package selection is the executable-shader question. GLSL ES/desktop
        // renderers keep source variants, Vulkan selects checked-in SPIR-V, and fixed-path
        // renderers remain unsupported without pretending to compile source they ignore.
        const ShaderPackageEXT package = CreateSkyboxShaderPackage();
        if (device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && package.selectFor(device).isUsable())
            effect_ = std::make_unique<ShaderEffect>(device, package);
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
        // GLSL samplers need the cube's texture unit. Vulkan's fixed custom-effect contract has
        // no named integer uniforms, so set it before the scalar push slot's real sky value.
        effect_->SetUniformInt("uEnvironment", 1);
        effect_->SetUniformMat4("uInvViewProj", &inverse.M11);
        // Combining tint and intensity leaves the portable fixed push block with exactly one
        // vec3 and one scalar. The shader computes yaw's sine/cosine from that scalar, preserving
        // the source path's result without requiring renderer-specific extra uniforms.
        effect_->SetUniformVec3("uTintIntensity", tint_.X * intensity_,
                                tint_.Y * intensity_, tint_.Z * intensity_);
        effect_->SetUniformFloat("uYaw", yaw_);
        effect_->SetTexture(1, *environment_);

        // Over whatever is already bound: the scene target inside a pipeline frame, the back
        // buffer outside one. Binding a destination here would mean the caller had to know which
        // of the two it currently was.
        fullscreen_->drawOverCurrentTarget(dummySource_.get(), effect_.get(), width, height);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
