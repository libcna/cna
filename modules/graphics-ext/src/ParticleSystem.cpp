// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ParticleSystem.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/GraphicsMemoryBarrier.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
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

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Vector4;
    using Microsoft::Xna::Framework::Graphics::BasicEffect;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::IndexBuffer;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using Microsoft::Xna::Framework::Graphics::VertexBuffer;
    using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
    using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;

    namespace {

        /// The hash and the spawn, written once in GLSL and once in C++ below. Integer arithmetic
        /// wraps identically in both languages, so every value a spawn draws is bit-identical
        /// across the two simulations; only the integration can differ, and only by rounding.
        constexpr const char* kSharedGlsl = R"(
uint cnaParticleHash(uint x) {
    x ^= x >> 16u; x *= 0x7feb352du;
    x ^= x >> 15u; x *= 0x846ca68bu;
    x ^= x >> 16u; return x;
}
float cnaParticleRandom(uint seed) {
    return float(cnaParticleHash(seed) & 0x00ffffffu) / 16777216.0;
}
)";

        constexpr const char* kComputeSource = R"(#version 310 es
layout(local_size_x = 64) in;
layout(std430, binding = 0) buffer CnaParticles { vec4 cnaParticles[]; };
uniform int   uCount;
uniform float uElapsed;
uniform float uConeAngle;
uniform float uSpeed;
uniform float uSpeedVariance;
uniform float uLifetime;
uniform float uLifetimeVariance;
uniform float uDrag;
uniform float uOriginX; uniform float uOriginY; uniform float uOriginZ;
uniform float uDirectionX; uniform float uDirectionY; uniform float uDirectionZ;
uniform float uGravityX; uniform float uGravityY; uniform float uGravityZ;
)";

        constexpr const char* kComputeBody = R"(
void cnaSpawn(uint index, uint generation, out vec3 position, out vec3 velocity, out float lifetime) {
    uint seed = cnaParticleHash(index * 747796405u + generation * 2891336453u);
    float u = cnaParticleRandom(seed);
    float v = cnaParticleRandom(seed + 1u);
    float w = cnaParticleRandom(seed + 2u);
    float x = cnaParticleRandom(seed + 3u);

    // A direction inside the cone, uniformly over the cap rather than over the angle -- sampling
    // the angle uniformly crowds the axis, which reads as a beam with a bright core.
    // Written as the CPU writes it, not as mix(), so the two agree bit for bit rather
    // than nearly: mix(x, y, a) is x*(1-a) + y*a, which is a different float expression.
    float cosTheta = 1.0 + (cos(uConeAngle) - 1.0) * u;
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    float phi = 6.28318530718 * v;

    vec3 axis = normalize(vec3(uDirectionX, uDirectionY, uDirectionZ));
    vec3 helper = abs(axis.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(helper, axis));
    vec3 up = cross(axis, right);
    vec3 direction = axis * cosTheta + (right * cos(phi) + up * sin(phi)) * sinTheta;

    float speed = uSpeed * (1.0 + uSpeedVariance * (w * 2.0 - 1.0));
    position = vec3(uOriginX, uOriginY, uOriginZ);
    velocity = direction * speed;
    lifetime = max(uLifetime * (1.0 + uLifetimeVariance * (x * 2.0 - 1.0)), 1e-3);
}

void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= uint(uCount)) return;
    uint base = index * 3u;

    vec3  position = cnaParticles[base].xyz;
    vec3  velocity = cnaParticles[base + 1u].xyz;
    vec4  state    = cnaParticles[base + 2u];
    float age      = state.x;
    float lifetime = state.y;
    float generation = state.w;

    age += uElapsed;
    if (age >= lifetime) {
        generation += 1.0;
        // The overshoot is carried into the new particle's age, so a long step does not quietly
        // shorten every lifetime it spans.
        age -= lifetime;
        cnaSpawn(index, uint(generation), position, velocity, lifetime);
    }

    velocity += vec3(uGravityX, uGravityY, uGravityZ) * uElapsed;
    velocity -= velocity * min(uDrag * uElapsed, 1.0);
    position += velocity * uElapsed;

    cnaParticles[base]      = vec4(position, 0.0);
    cnaParticles[base + 1u] = vec4(velocity, 0.0);
    cnaParticles[base + 2u] = vec4(age, lifetime, state.z, generation);
}
)";

        constexpr const char* kDrawVertexBody = R"(
layout(location = 0) in vec3 aPos;
out vec2 vTexCoord;
out vec4 vColor;
out float vViewDepth;
uniform mat4 View;
uniform mat4 Projection;
uniform vec4 uStartColor;
uniform vec4 uEndColor;
uniform float uStartSize;
uniform float uEndSize;
uniform int  uActiveCount;

void main() {
    int base = gl_InstanceID * 3;
    vec3 position = cnaParticles[base].xyz;
    vec4 state    = cnaParticles[base + 2];
    float t = clamp(state.x / max(state.y, 1e-4), 0.0, 1.0);
    float size = mix(uStartSize, uEndSize, t);
    if (gl_InstanceID >= uActiveCount) size = 0.0;

    // Billboarded in view space: the quad's corners are added after the view transform, so it
    // faces the camera without any per-particle rotation and without the CPU knowing the camera.
    vec3 viewPosition = (View * vec4(position, 1.0)).xyz;
    viewPosition.xy += aPos.xy * size;
    gl_Position = Projection * vec4(viewPosition, 1.0);
    vTexCoord = aPos.xy + 0.5;
    vColor = mix(uStartColor, uEndColor, t);
    // MOD-2109: the billboard's own distance along the view, which the fragment compares against
    // whatever the depth image says is behind it.
    vViewDepth = -viewPosition.z;
}
)";

        constexpr const char* kDrawFragmentBody = R"(
in vec2 vTexCoord;
in vec4 vColor;
in float vViewDepth;
out vec4 FragColor;
uniform sampler2D texture1;
uniform sampler2D uSceneDepth;
uniform vec2  uViewport;
uniform float uHasDepth;
uniform float uSoftness;
uniform float uDepthFarPlane;

void main() {
    vec4 colour = texture(texture1, vTexCoord) * vColor;
    if (uHasDepth > 0.5 && uSoftness > 0.0) {
        // The depth image is screen-sized, so the sample point is this fragment's own position in
        // it. Both images are render targets in every path that supplies one, which is what makes
        // gl_FragCoord the right coordinate rather than an orientation gamble.
        vec2 uv = gl_FragCoord.xy / max(uViewport, vec2(1.0));
        float behind = cnaDecodeLinearDepth(texture(uSceneDepth, uv)) * uDepthFarPlane;
        // A particle touching the surface behind it vanishes; one a full softness in front of it is
        // untouched. Linear between, which is what makes an intersecting billboard read as volume
        // rather than as a cut.
        colour.a *= clamp((behind - vViewDepth) / uSoftness, 0.0, 1.0);
    }
    FragColor = colour;
}
)";

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
        else if (!device.ExecutesShaderEffectSourceEXT())
            unsupportedReason_ =
                "this renderer accepts effect source without running it, so the particle shader "
                "would never execute";

        if (unsupportedReason_.empty())
        {
            std::string compute = std::string(kComputeSource) + kSharedGlsl + kComputeBody;
            program_ = std::make_unique<ComputeShader>(device, compute);
            std::string vertex = "#version 310 es\nprecision highp float;\n";
            vertex += getParticleLookupGlsl();
            vertex += kDrawVertexBody;
            std::string fragment = "#version 310 es\nprecision highp float;\n";
            fragment += DepthNormalPrepass::getDepthDecodeGlsl(
                DepthNormalPrepass::usesPackedDepthEXT(device));
            fragment += kDrawFragmentBody;
            effect_ = std::make_unique<ShaderEffect>(device, vertex, fragment);
            bool logged = false;
            detail::reportShaderCompileFailure(device, "ParticleSystem", effect_.get(), logged);

            if (!program_->isValid() || effect_ == nullptr || !effect_->IsEffectValid())
            {
                unsupportedReason_ = "the particle shaders did not compile on this device";
                program_.reset();
                effect_.reset();
            }
            else
            {
                buffer_ = std::make_unique<StorageBuffer>(
                    device, static_cast<std::size_t>(capacity_) * sizeof(Particle));

                // One unit quad, drawn once per particle. Wound clockwise, like every other piece
                // of geometry this layer's tests draw, so the default rasterizer keeps it.
                const std::array<VertexPositionColor, 4> corners{
                    VertexPositionColor(Vector3(-0.5f, -0.5f, 0.0f), Color::White),
                    VertexPositionColor(Vector3(-0.5f, 0.5f, 0.0f), Color::White),
                    VertexPositionColor(Vector3(0.5f, 0.5f, 0.0f), Color::White),
                    VertexPositionColor(Vector3(0.5f, -0.5f, 0.0f), Color::White)};
                quad_ = std::make_unique<VertexBuffer>(device, 4);
                quad_->SetData(corners.data(), 4);
                const std::array<std::uint16_t, 6> order{0, 1, 2, 0, 2, 3};
                quadIndices_ = std::make_unique<IndexBuffer>(device, 6);
                quadIndices_->SetData(order.data(), 6);
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

        if (!forceCpu_ && program_ != nullptr && buffer_ != nullptr)
        {
            if (!gpuStateValid_) uploadToGpu();
            program_->bindStorageBuffer(0, *buffer_);
            program_->setUniform("uCount", getActiveCount());
            program_->setUniform("uElapsed", elapsedSeconds);
            program_->setUniform("uConeAngle", settings_.ConeAngle);
            program_->setUniform("uSpeed", settings_.Speed);
            program_->setUniform("uSpeedVariance", settings_.SpeedVariance);
            program_->setUniform("uLifetime", settings_.Lifetime);
            program_->setUniform("uLifetimeVariance", settings_.LifetimeVariance);
            program_->setUniform("uDrag", settings_.Drag);
            program_->setUniform("uOriginX", settings_.Position.X);
            program_->setUniform("uOriginY", settings_.Position.Y);
            program_->setUniform("uOriginZ", settings_.Position.Z);
            program_->setUniform("uDirectionX", settings_.Direction.X);
            program_->setUniform("uDirectionY", settings_.Direction.Y);
            program_->setUniform("uDirectionZ", settings_.Direction.Z);
            program_->setUniform("uGravityX", settings_.Gravity.X);
            program_->setUniform("uGravityY", settings_.Gravity.Y);
            program_->setUniform("uGravityZ", settings_.Gravity.Z);
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
            effect_->SetUniformVec4("uStartColor", settings_.StartColor.X, settings_.StartColor.Y,
                                    settings_.StartColor.Z, settings_.StartColor.W);
            effect_->SetUniformVec4("uEndColor", settings_.EndColor.X, settings_.EndColor.Y,
                                    settings_.EndColor.Z, settings_.EndColor.W);
            effect_->SetUniformFloat("uStartSize", settings_.StartSize);
            effect_->SetUniformFloat("uEndSize", settings_.EndSize);
            effect_->SetUniformInt("uActiveCount", active);
            effect_->SetUniformInt("texture1", 0);
            effect_->SetTexture(0, *texture);
            const bool fading = sceneDepth_ != nullptr && depthFarPlane_ > 0.0f && softness_ > 0.0f;
            effect_->SetUniformFloat("uHasDepth", fading ? 1.0f : 0.0f);
            effect_->SetUniformFloat("uSoftness", softness_);
            effect_->SetUniformFloat("uDepthFarPlane", depthFarPlane_);
            effect_->SetUniformVec2("uViewport",
                                    static_cast<float>(device_.getViewportProperty().getWidthProperty()),
                                    static_cast<float>(device_.getViewportProperty().getHeightProperty()));
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
