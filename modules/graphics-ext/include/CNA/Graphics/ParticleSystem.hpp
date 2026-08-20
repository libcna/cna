// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class BasicEffect;
    class GraphicsDevice;
    class IndexBuffer;
    class ShaderEffect;
    class Texture2D;
    class VertexBuffer;
}

namespace CNA::Graphics {

    class ComputeShader;
    class StorageBuffer;

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief What an emitter throws, how fast, and what happens to it afterwards.
     */
    struct ParticleEmitterSettings
    {
        /** @brief Where particles are born, in world space. */
        Microsoft::Xna::Framework::Vector3 Position{0.0f, 0.0f, 0.0f};
        /** @brief The centre of the emission cone; normalised internally. */
        Microsoft::Xna::Framework::Vector3 Direction{0.0f, 1.0f, 0.0f};
        /** @brief The cone's half angle in radians; 0 emits a straight line, pi a full sphere. */
        float ConeAngle = 0.35f;
        /** @brief How fast a particle leaves, in units per second. */
        float Speed = 4.0f;
        /** @brief How much that speed varies, as a fraction of it. */
        float SpeedVariance = 0.25f;
        /** @brief How long a particle lives, in seconds. */
        float Lifetime = 2.0f;
        /** @brief How much that lifetime varies, as a fraction of it. */
        float LifetimeVariance = 0.25f;
        /** @brief Constant acceleration, in units per second squared. */
        Microsoft::Xna::Framework::Vector3 Gravity{0.0f, -9.81f, 0.0f};
        /** @brief Linear drag per second; 0 is a vacuum. */
        float Drag = 0.0f;
        /** @brief How many particles are born per second. */
        float EmissionRate = 200.0f;
        /** @brief A particle's size at birth, in world units. */
        float StartSize = 0.25f;
        /** @brief Its size at death. */
        float EndSize = 0.0f;
        /** @brief Its colour at birth, unclamped so it can be an HDR emitter. */
        Microsoft::Xna::Framework::Vector4 StartColor{1.0f, 0.8f, 0.4f, 1.0f};
        /** @brief Its colour at death. */
        Microsoft::Xna::Framework::Vector4 EndColor{1.0f, 0.1f, 0.0f, 0.0f};
    };

    /**
     * @brief One particle, in the layout both the compute shader and the CPU simulation use.
     */
    struct Particle
    {
        /** @brief Its position in world space; the fourth component is padding std430 requires. */
        Microsoft::Xna::Framework::Vector4 Position{0.0f, 0.0f, 0.0f, 0.0f};
        /** @brief Its velocity in world space; the fourth component is padding. */
        Microsoft::Xna::Framework::Vector4 Velocity{0.0f, 0.0f, 0.0f, 0.0f};
        /** @brief Age, lifetime, the seed its last spawn used, and how many times it has respawned. */
        Microsoft::Xna::Framework::Vector4 State{0.0f, 1.0f, 0.0f, 0.0f};
    };

    /**
     * @brief An emitter, a simulation and a draw -- the subsystem `MOD-1550` left as a demo.
     *
     * plan_modern.md `MOD-2095`. Particles are simulated **on the GPU where the device has compute
     * and on the CPU where it does not**, and the two are written to the same specification rather
     * than to the same intent: the spawn values come from an integer hash that is bit-identical in
     * GLSL and C++, and the integration is the same four lines in the same order. A test steps both
     * and compares them, which is the only way to know they are one simulation and not two.
     *
     * **Nothing is read back.** The GPU path never brings particles to the CPU to draw them -- the
     * vertex shader reads the same buffer the compute shader wrote, indexed by `gl_InstanceID`, and
     * the whole system is one instanced draw. That needs a storage buffer readable from a vertex
     * shader, which GL ES 3.1 permits a device to lack; where it is missing the system falls back
     * to the CPU simulation, which draws from an array it already holds.
     *
     * **Falling back is right here, unlike `GpuInstanceCuller`.** The CPU path produces the same
     * particles, only more slowly -- so a device without compute gets a correct effect rather than
     * an empty screen. @ref usesCompute says which path ran.
     *
     * Particles never die permanently: a slot whose age passes its lifetime respawns at the
     * emitter. `ParticleEmitterSettings::EmissionRate` decides how many slots are in use, so the rate and the lifetime
     * together decide how many particles are on screen -- a rate the capacity cannot sustain is
     * clamped rather than silently dropped, and @ref isEmissionRateClamped says so.
     */
    class ParticleSystem
    {
    public:
        /** @brief The most particles a system holds unless it is asked for another number. */
        static constexpr int kDefaultCapacity = 1024;

        /**
         * @brief The storage-buffer binding the particle buffer is bound to for the draw.
         */
        static constexpr int kParticleBinding = 7;

        /**
         * @brief Creates the system, its buffers and whichever simulation the device can run.
         *
         * @param device   The device to allocate and compile on.
         * @param capacity How many particles at most; must be positive.
         * @throws std::invalid_argument If @p capacity is not positive.
         */
        explicit ParticleSystem(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                                int capacity = kDefaultCapacity);

        /** @brief Destroys the system and everything it owns. */
        ~ParticleSystem();

        ParticleSystem(const ParticleSystem&)            = delete;
        ParticleSystem& operator=(const ParticleSystem&) = delete;

        /** @brief Returns how many particles the system can hold. */
        [[nodiscard]] int getCapacity() const;

        /** @brief Returns the emitter's settings. */
        [[nodiscard]] const ParticleEmitterSettings& getSettings() const;

        /**
         * @brief Replaces the emitter's settings.
         *
         * Takes effect from the next particle born; particles already in flight keep the lifetime
         * and speed they were given, which is what stops a settings change snapping the whole
         * effect into a new shape.
         *
         * @param value The new settings.
         */
        void setSettings(const ParticleEmitterSettings& value);

        /**
         * @brief Puts every particle back at the emitter, with ages staggered across a lifetime.
         *
         * Staggering rather than zeroing is what makes emission continuous from the first frame
         * instead of arriving as one puff.
         */
        void reset();

        /**
         * @brief Advances the simulation.
         *
         * @param elapsedSeconds How far to step; a non-positive value does nothing.
         */
        void update(float elapsedSeconds);

        /**
         * @brief Draws every live particle as a camera-facing quad.
         *
         * @param view       The camera's view matrix.
         * @param projection The camera's projection matrix.
         * @param texture    The particle image; its alpha is the shape.
         * @throws std::invalid_argument If @p texture is null.
         */
        void draw(const Microsoft::Xna::Framework::Matrix& view,
                  const Microsoft::Xna::Framework::Matrix& projection,
                  Microsoft::Xna::Framework::Graphics::Texture2D* texture);

        /** @brief Returns whether the last @ref update ran on the GPU. */
        [[nodiscard]] bool usesCompute() const;

        /** @brief Returns whether the simulation has been pinned to the CPU. */
        [[nodiscard]] bool isSimulationOnCpuEXT() const;

        /**
         * @brief Pins the simulation to the CPU even where the GPU path is available.
         *
         * Two reasons to want this, and neither is hypothetical: a tile GPU where a dispatch plus
         * its barrier costs more than stepping a few hundred particles, and a test that has to be
         * able to run the fallback on a device that does not need it -- a fallback nothing exercises
         * is a fallback nobody knows is broken.
         *
         * Switching in either direction carries the current particles with it.
         *
         * @param value True to simulate on the CPU, false to use the GPU where it exists.
         */
        void setSimulationOnCpuEXT(bool value);

        /** @brief Returns why the GPU path is unavailable, or an empty string when it is. */
        [[nodiscard]] const std::string& getUnsupportedReason() const;

        /** @brief Returns how many particle slots the emission rate keeps in use. */
        [[nodiscard]] int getActiveCount() const;

        /** @brief Returns whether the emission rate asks for more particles than the capacity holds. */
        [[nodiscard]] bool isEmissionRateClamped() const;

        /**
         * @brief Returns the particles, simulating on the CPU first if the GPU path is running.
         *
         * **This reads back from the GPU when the GPU path is in use**, which is a stall; it is
         * here for tests and tools, and the drawing path never calls it.
         *
         * @return Every particle, in slot order.
         */
        [[nodiscard]] std::vector<Particle> readParticlesEXT() const;

        /**
         * @brief Advances one particle by one step, exactly as the shader does.
         *
         * The CPU half of the pair the layer's tests compare. Public because a comparison that can
         * only be made through a whole frame is not a comparison anyone runs.
         *
         * @param particle       The particle to advance, updated in place.
         * @param index          Its slot, which seeds its respawns.
         * @param settings       The emitter's settings.
         * @param elapsedSeconds The step.
         */
        static void step(Particle& particle, int index,
                         const ParticleEmitterSettings& settings, float elapsedSeconds);

        /**
         * @brief Returns the same pseudo-random value the shader's hash returns.
         *
         * Integer arithmetic wraps identically in GLSL and C++, so this is bit-identical to the
         * GPU's answer rather than merely similar -- which is what lets the two simulations be
         * compared at all.
         *
         * @param seed The seed.
         * @return A value in [0, 1).
         */
        [[nodiscard]] static float random(std::uint32_t seed);

        /**
         * @brief Returns the GLSL a vertex shader includes to read a particle.
         *
         * @return The buffer declaration and the accessors, for a `#version 310 es` shader.
         */
        [[nodiscard]] static std::string getParticleLookupGlsl();

    private:
        void spawn(Particle& particle, int index, std::uint32_t generation) const;
        void uploadToGpu();

        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        std::unique_ptr<ComputeShader> program_;
        std::unique_ptr<StorageBuffer> buffer_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> fallbackEffect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> quad_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> quadIndices_;

        ParticleEmitterSettings settings_{};
        std::vector<Particle> particles_;
        std::string unsupportedReason_;
        int  capacity_     = 0;
        bool usesCompute_  = false;
        bool forceCpu_     = false;
        bool gpuStateValid_ = false;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
