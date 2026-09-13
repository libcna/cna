// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ParticleSystem.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/GraphicsMemoryBarrier.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "shaders/particle_system/ParticleSystemShaderPackage.generated.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Vector4;
    using Microsoft::Xna::Framework::Graphics::BasicEffect;
    using Microsoft::Xna::Framework::Graphics::BufferUsage;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::IndexBuffer;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using Microsoft::Xna::Framework::Graphics::VertexBuffer;
    using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
    using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;

    namespace {

        struct ParticleSimulationParameters
        {
            std::array<float, 4> Simulation0{};
            std::array<float, 4> Simulation1{};
            std::array<float, 4> Origin{};
            std::array<float, 4> Direction{};
            std::array<float, 4> Gravity{};
        };
        static_assert(sizeof(ParticleSimulationParameters) == 20 * sizeof(float));

        template <std::size_t N>
        [[nodiscard]] std::vector<std::uint8_t> ToBytes(const std::uint32_t (&words)[N])
        {
            const auto* begin = reinterpret_cast<const std::uint8_t*>(words);
            return std::vector<std::uint8_t>(begin, begin + sizeof(words));
        }

        [[nodiscard]] ShaderPackageEXT CreateParticleComputePackage()
        {
            using namespace detail::ParticleSystemGenerated;
            return ShaderPackageEXT(
                {
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Compute, "main",
                                  "particle_system/simulate.es.comp.glsl",
                                  std::string(kSimulateEsComputeSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Compute, "main",
                                  "particle_system/simulate.desktop.comp.glsl",
                                  std::string(kSimulateDesktopComputeSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Compute, "main",
                                  "particle_system/simulate.vulkan.comp.spv",
                                  ToBytes(kSimulateVulkanComputeSpirV)),
                },
                {CNA::ShaderStageEXT::Compute},
                {
                    ShaderBindingRequirementEXT(
                        "CnaParticles", 0, ShaderBindingTypeEXT::StorageBuffer,
                        CNA::ShaderStageEXT::Compute),
                    ShaderBindingRequirementEXT(
                        "ParticleSimulationParameters", 1,
                        ShaderBindingTypeEXT::ConstantBuffer,
                        CNA::ShaderStageEXT::Compute),
                });
        }

        [[nodiscard]] ShaderPackageEXT CreateParticleDrawPackage()
        {
            using namespace detail::ParticleSystemGenerated;
            return ShaderPackageEXT(
                {
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "particle_system/draw.es.vert.glsl",
                                  std::string(kDrawEsVertexSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  "particle_system/draw.es.frag.glsl",
                                  std::string(kDrawEsFragmentSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "particle_system/draw.desktop.vert.glsl",
                                  std::string(kDrawDesktopVertexSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  "particle_system/draw.desktop.frag.glsl",
                                  std::string(kDrawDesktopFragmentSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "particle_system/draw.vulkan.vert.spv",
                                  ToBytes(kDrawVulkanVertexSpirV)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  "particle_system/draw.vulkan.frag.spv",
                                  ToBytes(kDrawVulkanFragmentSpirV)),
                },
                {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment},
                {
                    ShaderBindingRequirementEXT(
                        "texture1", 0, ShaderBindingTypeEXT::SampledTexture2D,
                        CNA::ShaderStageEXT::Fragment),
                    ShaderBindingRequirementEXT(
                        "uSceneDepth", 1, ShaderBindingTypeEXT::SampledTexture2D,
                        CNA::ShaderStageEXT::Fragment),
                    ShaderBindingRequirementEXT(
                        "CnaParticleBuffer", ParticleSystem::kParticleBinding,
                        ShaderBindingTypeEXT::StorageBuffer,
                        CNA::ShaderStageEXT::Vertex),
                });
        }

        std::uint32_t Hash(std::uint32_t x)
        {
            x ^= x >> 16; x *= 0x7feb352du;
            x ^= x >> 15; x *= 0x846ca68bu;
            x ^= x >> 16; return x;
        }

        Vector3 Normalize(const Vector3& v, const Vector3& fallback)
        {
            const float length = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
            if (length <= 1e-6f) return fallback;
            return Vector3(v.X / length, v.Y / length, v.Z / length);
        }

        Vector3 Cross(const Vector3& a, const Vector3& b)
        {
            return Vector3(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);
        }

    } // namespace

    float ParticleSystem::random(const std::uint32_t seed)
    {
        return static_cast<float>(Hash(seed) & 0x00ffffffu) / 16777216.0f;
    }

    void ParticleSystem::step(Particle& particle, const int index,
                              const ParticleEmitterSettings& settings, const float elapsedSeconds)
    {
        float age = particle.State.X + elapsedSeconds;
        float lifetime = particle.State.Y;
        float generation = particle.State.W;

        if (age >= lifetime)
        {
            generation += 1.0f;
            age -= lifetime;

            const std::uint32_t seed = Hash(static_cast<std::uint32_t>(index) * 747796405u +
                                            static_cast<std::uint32_t>(generation) * 2891336453u);
            const float u = random(seed);
            const float v = random(seed + 1u);
            const float w = random(seed + 2u);
            const float x = random(seed + 3u);

            const float cosTheta = 1.0f + (std::cos(settings.ConeAngle) - 1.0f) * u;
            const float sinTheta = std::sqrt(std::max(1.0f - cosTheta * cosTheta, 0.0f));
            const float phi = 6.28318530718f * v;

            const Vector3 axis = Normalize(settings.Direction, Vector3(0.0f, 1.0f, 0.0f));
            const Vector3 helper = std::abs(axis.Y) < 0.99f ? Vector3(0.0f, 1.0f, 0.0f)
                                                            : Vector3(1.0f, 0.0f, 0.0f);
            const Vector3 right = Normalize(Cross(helper, axis), Vector3(1.0f, 0.0f, 0.0f));
            const Vector3 up = Cross(axis, right);
            const float cosPhi = std::cos(phi);
            const float sinPhi = std::sin(phi);
            const Vector3 direction(
                axis.X * cosTheta + (right.X * cosPhi + up.X * sinPhi) * sinTheta,
                axis.Y * cosTheta + (right.Y * cosPhi + up.Y * sinPhi) * sinTheta,
                axis.Z * cosTheta + (right.Z * cosPhi + up.Z * sinPhi) * sinTheta);

            const float speed = settings.Speed * (1.0f + settings.SpeedVariance * (w * 2.0f - 1.0f));
            particle.Position = Vector4(settings.Position.X, settings.Position.Y,
                                        settings.Position.Z, 0.0f);
            particle.Velocity = Vector4(direction.X * speed, direction.Y * speed,
                                        direction.Z * speed, 0.0f);
            lifetime = std::max(settings.Lifetime *
                                    (1.0f + settings.LifetimeVariance * (x * 2.0f - 1.0f)),
                                1e-3f);
        }

        Vector3 velocity(particle.Velocity.X, particle.Velocity.Y, particle.Velocity.Z);
        velocity = Vector3(velocity.X + settings.Gravity.X * elapsedSeconds,
                           velocity.Y + settings.Gravity.Y * elapsedSeconds,
                           velocity.Z + settings.Gravity.Z * elapsedSeconds);
        const float drag = std::min(settings.Drag * elapsedSeconds, 1.0f);
        velocity = Vector3(velocity.X - velocity.X * drag, velocity.Y - velocity.Y * drag,
                           velocity.Z - velocity.Z * drag);

        particle.Position = Vector4(particle.Position.X + velocity.X * elapsedSeconds,
                                    particle.Position.Y + velocity.Y * elapsedSeconds,
                                    particle.Position.Z + velocity.Z * elapsedSeconds, 0.0f);
        particle.Velocity = Vector4(velocity.X, velocity.Y, velocity.Z, 0.0f);
        particle.State = Vector4(age, lifetime, particle.State.Z, generation);
    }

    std::string ParticleSystem::getParticleLookupGlsl()
    {
        return R"(
layout(std430, binding = 7) readonly buffer CnaParticleBuffer { vec4 cnaParticles[]; };
)";
    }

    ParticleSystem::ParticleSystem(GraphicsDevice& device, const int capacity)
        : device_(device), capacity_(capacity)
    {
        if (capacity <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::ParticleSystem: the capacity must be positive");

        particles_.resize(static_cast<std::size_t>(capacity_));

        if (!device.SupportsCapability(CNA::GraphicsCapability::ComputeShaders))
            unsupportedReason_ = "this renderer has no compute shaders";
        else if (!device.SupportsCapability(CNA::GraphicsCapability::Instancing))
            unsupportedReason_ = "this renderer has no hardware instancing";
        else if (device.GetRenderer().GetMaxVertexShaderStorageBlocksEXT() < 1)
            unsupportedReason_ =
                "this context allows no storage buffer in a vertex shader, so the draw could not "
                "read what the simulation wrote";
        else if (!device.SupportsCapability(CNA::GraphicsCapability::CustomEffects))
            unsupportedReason_ = "this renderer cannot execute a custom particle draw effect";

        if (unsupportedReason_.empty())
        {
            const ShaderPackageEXT computePackage = CreateParticleComputePackage();
            const ShaderPackageEXT drawPackage = CreateParticleDrawPackage();
            if (!computePackage.selectFor(device).isUsable()
                || !drawPackage.selectFor(device).isUsable())
            {
                unsupportedReason_ =
                    "this renderer has no usable portable particle shader variants";
            }
            else
            {
                try
                {
                    program_ = std::make_unique<ComputeShader>(device, computePackage);
                    effect_ = std::make_unique<ShaderEffect>(device, drawPackage);
                    bool logged = false;
                    detail::reportShaderCompileFailure(
                        device, "ParticleSystem", effect_.get(), logged);
                    if (!program_->isValid() || !effect_->IsEffectValid())
                        throw std::runtime_error(
                            "the selected portable particle shader did not compile");

                    buffer_ = std::make_unique<StorageBuffer>(
                        device, static_cast<std::size_t>(capacity_) * sizeof(Particle));
                    computeParameters_ = std::make_unique<StorageBuffer>(
                        device,
                        StorageBufferDescriptor(
                            sizeof(ParticleSimulationParameters),
                            StorageBufferUsage::Constant,
                            StorageBufferCpuAccess::Write));

                    // One unit quad, drawn once per particle. Its own declaration is required by a
                    // portable custom effect; inferring inputs from stride is not valid.
                    const std::array<VertexPositionColor, 4> corners{
                        VertexPositionColor(Vector3(-0.5f, -0.5f, 0.0f), Color::White),
                        VertexPositionColor(Vector3(-0.5f, 0.5f, 0.0f), Color::White),
                        VertexPositionColor(Vector3(0.5f, 0.5f, 0.0f), Color::White),
                        VertexPositionColor(Vector3(0.5f, -0.5f, 0.0f), Color::White)};
                    quad_ = std::make_unique<VertexBuffer>(
                        device, VertexPositionColor::getVertexDeclarationStatic(), 4,
                        BufferUsage::None);
                    quad_->SetData(corners.data(), 4);
                    const std::array<std::uint16_t, 6> order{0, 1, 2, 0, 2, 3};
                    quadIndices_ = std::make_unique<IndexBuffer>(device, 6);
                    quadIndices_->SetData(order.data(), 6);
                }
                catch (const std::exception& error)
                {
                    unsupportedReason_ =
                        "the portable particle shaders did not initialise: "
                        + std::string(error.what());
                    program_.reset();
                    effect_.reset();
                    buffer_.reset();
                    computeParameters_.reset();
                }
            }
        }

        // Always built, not only where the GPU path is missing: setSimulationOnCpuEXT can select
        // this path on a device that has both.
        fallbackEffect_ = std::make_unique<BasicEffect>(device);
        fallbackEffect_->VertexColorEnabled = true;
        fallbackEffect_->setTextureEnabledProperty(true);

        reset();
    }

    ParticleSystem::~ParticleSystem() = default;

    int ParticleSystem::getCapacity() const { return capacity_; }

    const ParticleEmitterSettings& ParticleSystem::getSettings() const { return settings_; }

    void ParticleSystem::setSettings(const ParticleEmitterSettings& value) { settings_ = value; }

    bool ParticleSystem::usesCompute() const { return usesCompute_; }

    void ParticleSystem::setDepthInputEXT(Texture2D* depth, const float farPlane)
    {
        sceneDepth_ = depth;
        depthFarPlane_ = farPlane;
    }

    float ParticleSystem::getSoftnessEXT() const { return softness_; }
    void  ParticleSystem::setSoftnessEXT(const float value)
    {
        softness_ = std::max(value, 0.0f);
    }

    bool ParticleSystem::isSimulationOnCpuEXT() const { return forceCpu_; }

    void ParticleSystem::setSimulationOnCpuEXT(const bool value)
    {
        if (forceCpu_ == value) return;
        // Carry the particles across rather than restarting them: a simulation that snapped back to
        // the emitter because a setting changed would be a visible glitch and an invisible bug.
        if (value) particles_ = readParticlesEXT();
        forceCpu_ = value;
        if (!value) uploadToGpu();
    }

    const std::string& ParticleSystem::getUnsupportedReason() const { return unsupportedReason_; }

    int ParticleSystem::getActiveCount() const
    {
        const float wanted = settings_.EmissionRate * settings_.Lifetime;
        if (wanted <= 0.0f) return 0;
        return std::min(static_cast<int>(wanted + 0.5f), capacity_);
    }

    bool ParticleSystem::isEmissionRateClamped() const
    {
        return settings_.EmissionRate * settings_.Lifetime > static_cast<float>(capacity_);
    }

    void ParticleSystem::spawn(Particle& particle, const int index,
                               const std::uint32_t generation) const
    {
        // Spawning is the step's own respawn branch, reached by making the particle exactly as old
        // as it is allowed to be. Writing it twice would be two chances to disagree.
        particle.State = Vector4(0.0f, 0.0f, 0.0f, static_cast<float>(generation) - 1.0f);
        particle.Position = Vector4(settings_.Position.X, settings_.Position.Y,
                                    settings_.Position.Z, 0.0f);
        particle.Velocity = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
        step(particle, index, settings_, 0.0f);
    }

    void ParticleSystem::reset()
    {
        for (int i = 0; i < capacity_; ++i)
        {
            Particle& particle = particles_[static_cast<std::size_t>(i)];
            spawn(particle, i, 1u);
            // Ages staggered across one lifetime, so emission is continuous from the first frame
            // instead of arriving as a single puff and then nothing for two seconds.
            const float fraction = capacity_ > 1
                ? static_cast<float>(i) / static_cast<float>(capacity_)
                : 0.0f;
            particle.State.X = particle.State.Y * fraction;
        }
        uploadToGpu();
    }

    void ParticleSystem::uploadToGpu()
    {
        if (buffer_ == nullptr) return;
        buffer_->setBytes(particles_.data(), particles_.size() * sizeof(Particle));
        gpuStateValid_ = true;
    }

    void ParticleSystem::update(const float elapsedSeconds)
    {
        if (elapsedSeconds <= 0.0f) return;

        if (!forceCpu_ && program_ != nullptr && buffer_ != nullptr
            && computeParameters_ != nullptr)
        {
            if (!gpuStateValid_) uploadToGpu();
            const ParticleSimulationParameters parameters{
                {static_cast<float>(getActiveCount()), elapsedSeconds,
                 settings_.ConeAngle, settings_.Speed},
                {settings_.SpeedVariance, settings_.Lifetime,
                 settings_.LifetimeVariance, settings_.Drag},
                {settings_.Position.X, settings_.Position.Y, settings_.Position.Z, 0.0f},
                {settings_.Direction.X, settings_.Direction.Y, settings_.Direction.Z, 0.0f},
                {settings_.Gravity.X, settings_.Gravity.Y, settings_.Gravity.Z, 0.0f},
            };
            computeParameters_->setBytes(&parameters, sizeof(parameters));
            program_->bindStorageBuffer(0, *buffer_);
            program_->bindConstantBuffer(1, *computeParameters_);
            program_->dispatch((getActiveCount() + 63) / 64);
            program_->barrier(CNA::GraphicsMemoryBarrier::ShaderStorage);
            usesCompute_ = true;
            return;
        }

        const int active = getActiveCount();
        for (int i = 0; i < active; ++i)
            step(particles_[static_cast<std::size_t>(i)], i, settings_, elapsedSeconds);
        usesCompute_ = false;
        // The buffer now holds an older frame than the array does, and saying so is what makes
        // switching back to the GPU pick up where the CPU left off.
        gpuStateValid_ = false;
    }

    std::vector<Particle> ParticleSystem::readParticlesEXT() const
    {
        if (forceCpu_ || buffer_ == nullptr || !gpuStateValid_) return particles_;
        std::vector<Particle> data(static_cast<std::size_t>(capacity_));
        buffer_->getBytes(data.data(), data.size() * sizeof(Particle));
        return data;
    }

    void ParticleSystem::draw(const Matrix& view, const Matrix& projection, Texture2D* texture)
    {
        if (texture == nullptr)
            throw std::invalid_argument(
                "CNA::Graphics::ParticleSystem::draw: there is no particle texture");

        const int active = getActiveCount();
        if (active <= 0) return;

        if (!forceCpu_ && effect_ != nullptr && buffer_ != nullptr && gpuStateValid_)
        {
            effect_->setViewProperty(view);
            effect_->setProjectionProperty(projection);
            effect_->Apply();
            std::array<float, 32> matrices{};
            view.ToColumnMajor(matrices.data());
            projection.ToColumnMajor(matrices.data() + 16);
            const std::array<float, 6> colours{
                settings_.StartColor.X, settings_.StartColor.Y, settings_.StartColor.Z,
                settings_.EndColor.X, settings_.EndColor.Y, settings_.EndColor.Z,
            };
            const bool fading =
                sceneDepth_ != nullptr && depthFarPlane_ > 0.0f && softness_ > 0.0f;
            const std::array<float, 11> scalars{
                settings_.StartColor.W,
                settings_.EndColor.W,
                settings_.StartSize,
                settings_.EndSize,
                static_cast<float>(active),
                fading ? 1.0f : 0.0f,
                softness_,
                depthFarPlane_,
                static_cast<float>(device_.getViewportProperty().getWidthProperty()),
                static_cast<float>(device_.getViewportProperty().getHeightProperty()),
                DepthNormalPrepass::usesPackedDepthEXT(device_) ? 1.0f : 0.0f,
            };
            effect_->SetUniformMat4Array("uParticleMatrices", matrices.data(), 2);
            effect_->SetUniformVec3Array("uParticleColours", colours.data(), 2);
            effect_->SetUniformFloatArray(
                "uParticleScalars", scalars.data(), static_cast<int>(scalars.size()));
            effect_->SetUniformInt("texture1", 0);
            effect_->SetTexture(0, *texture);
            if (fading)
            {
                effect_->SetUniformInt("uSceneDepth", 1);
                effect_->SetTexture(1, *sceneDepth_);
            }

            device_.GetRenderer().BindStorageBufferForDrawEXT(kParticleBinding,
                                                              *buffer_->getRendererEXT());
            device_.SetVertexBuffer(quad_.get());
            device_.SetIndexBuffer(quadIndices_.get());
            device_.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, active);
            device_.SetIndexBuffer(nullptr);
            device_.SetVertexBuffer(nullptr);
            return;
        }

        // The CPU path builds the billboards itself and draws them through the stock textured,
        // vertex-coloured effect -- no shader of its own, because the whole point of this path is
        // that it runs where custom shaders and compute do not.
        const Matrix inverseView = Matrix::Invert(view);
        const Vector3 right(inverseView.M11, inverseView.M12, inverseView.M13);
        const Vector3 up(inverseView.M21, inverseView.M22, inverseView.M23);

        std::vector<VertexPositionColorTexture> vertices;
        vertices.reserve(static_cast<std::size_t>(active) * 6);
        for (int i = 0; i < active; ++i)
        {
            const Particle& particle = particles_[static_cast<std::size_t>(i)];
            const float t = std::clamp(particle.State.X / std::max(particle.State.Y, 1e-4f),
                                       0.0f, 1.0f);
            const float size = settings_.StartSize + (settings_.EndSize - settings_.StartSize) * t;
            const Vector4& start = settings_.StartColor;
            const Vector4& end = settings_.EndColor;
            const Color colour(start.X + (end.X - start.X) * t, start.Y + (end.Y - start.Y) * t,
                               start.Z + (end.Z - start.Z) * t, start.W + (end.W - start.W) * t);

            const Vector3 centre(particle.Position.X, particle.Position.Y, particle.Position.Z);
            const auto corner = [&](const float x, const float y) {
                return Vector3(centre.X + (right.X * x + up.X * y) * size,
                               centre.Y + (right.Y * x + up.Y * y) * size,
                               centre.Z + (right.Z * x + up.Z * y) * size);
            };
            const VertexPositionColorTexture a(corner(-0.5f, -0.5f), colour, Vector2(0.0f, 0.0f));
            const VertexPositionColorTexture b(corner(-0.5f, 0.5f), colour, Vector2(0.0f, 1.0f));
            const VertexPositionColorTexture c(corner(0.5f, 0.5f), colour, Vector2(1.0f, 1.0f));
            const VertexPositionColorTexture d(corner(0.5f, -0.5f), colour, Vector2(1.0f, 0.0f));
            vertices.push_back(a); vertices.push_back(b); vertices.push_back(c);
            vertices.push_back(a); vertices.push_back(c); vertices.push_back(d);
        }
        if (vertices.empty() || fallbackEffect_ == nullptr) return;

        fallbackEffect_->setWorldProperty(Matrix::getIdentityProperty());
        fallbackEffect_->setViewProperty(view);
        fallbackEffect_->setProjectionProperty(projection);
        fallbackEffect_->setTextureProperty(texture);
        fallbackEffect_->Apply();
        device_.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0,
                                   static_cast<int>(vertices.size() / 3),
                                   VertexPositionColorTexture::getVertexDeclarationStatic());
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
