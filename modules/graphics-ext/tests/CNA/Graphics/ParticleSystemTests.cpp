// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2095: particles as a subsystem.
//
// The simulation is written twice -- once in GLSL and once in C++ -- so the question that decides
// whether this is one subsystem or two is whether the two agree. They are compared here directly:
// the same particles, the same steps, and positions checked against each other. The spawn values
// come from an integer hash that wraps identically in both languages, so a disagreement can only be
// float rounding in the integration, and anything larger is a real divergence.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "EngineTestSupport.hpp"

#include "CNA/Graphics/ParticleSystem.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::Particle;
using CNA::Graphics::ParticleEmitterSettings;
using CNA::Graphics::ParticleSystem;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::MathHelper;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr int kSize = 64;

ParticleEmitterSettings QuietSettings()
{
    ParticleEmitterSettings settings;
    settings.Position = Vector3(0.0f, 0.0f, 0.0f);
    settings.Direction = Vector3(0.0f, 1.0f, 0.0f);
    settings.ConeAngle = 0.4f;
    settings.Speed = 3.0f;
    settings.SpeedVariance = 0.2f;
    settings.Lifetime = 2.0f;
    settings.LifetimeVariance = 0.2f;
    settings.Gravity = Vector3(0.0f, -9.81f, 0.0f);
    settings.Drag = 0.1f;
    settings.EmissionRate = 64.0f;
    settings.StartSize = 0.5f;
    settings.EndSize = 0.5f;
    return settings;
}

std::unique_ptr<Texture2D> WhiteTexture(GraphicsDevice& device)
{
    auto texture = std::make_unique<Texture2D>(device, 2, 2);
    const std::array<Color, 4> texels{Color::White, Color::White, Color::White, Color::White};
    texture->SetData(texels.data(), 4);
    return texture;
}

TEST(ParticleSystemTest, ACapacityThatCannotHoldAParticleIsRefused)
{
    GraphicsDevice device;
    EXPECT_THROW(ParticleSystem(device, 0), std::invalid_argument);
    EXPECT_THROW(ParticleSystem(device, -8), std::invalid_argument);
}

TEST(ParticleSystemTest, TheHashIsInRangeAndDependsOnItsSeed)
{
    // The bridge between the two simulations: if this were not deterministic, nothing else in this
    // file could be compared at all.
    std::set<float> seen;
    for (std::uint32_t seed = 0; seed < 256; ++seed)
    {
        const float value = ParticleSystem::random(seed);
        EXPECT_GE(value, 0.0f);
        EXPECT_LT(value, 1.0f);
        seen.insert(value);
    }
    EXPECT_GT(seen.size(), 200u) << "the hash is collapsing distinct seeds onto the same value";
    EXPECT_FLOAT_EQ(ParticleSystem::random(12345u), ParticleSystem::random(12345u));
}

TEST(ParticleSystemTest, TheEmissionRateAndLifetimeDecideHowManyAreInFlight)
{
    GraphicsDevice device;
    ParticleSystem system(device, 256);

    ParticleEmitterSettings settings = QuietSettings();
    settings.EmissionRate = 50.0f;
    settings.Lifetime = 2.0f;
    system.setSettings(settings);
    EXPECT_EQ(system.getActiveCount(), 100);
    EXPECT_FALSE(system.isEmissionRateClamped());

    // A rate the capacity cannot sustain is clamped and says so, rather than quietly emitting
    // fewer particles than the settings claim.
    settings.EmissionRate = 1000.0f;
    system.setSettings(settings);
    EXPECT_EQ(system.getActiveCount(), 256);
    EXPECT_TRUE(system.isEmissionRateClamped());

    settings.EmissionRate = 0.0f;
    system.setSettings(settings);
    EXPECT_EQ(system.getActiveCount(), 0);
}

TEST(ParticleSystemTest, ResetStaggersAgesAcrossOneLifetime)
{
    // A system whose particles all start at age zero emits one puff and then nothing for a whole
    // lifetime, which is the single most visible way to get this wrong.
    GraphicsDevice device;
    ParticleSystem system(device, 64);
    system.setSettings(QuietSettings());
    system.reset();

    const std::vector<Particle> particles = system.readParticlesEXT();
    ASSERT_EQ(particles.size(), 64u);
    float youngest = 1e9f;
    float oldest = -1.0f;
    for (const Particle& particle : particles)
    {
        EXPECT_GE(particle.State.X, 0.0f);
        EXPECT_LE(particle.State.X, particle.State.Y);
        youngest = std::min(youngest, particle.State.X);
        oldest = std::max(oldest, particle.State.X);
    }
    EXPECT_NEAR(youngest, 0.0f, 1e-5f);
    EXPECT_GT(oldest, 1.0f) << "the ages are not spread across a lifetime";
}

TEST(ParticleSystemTest, GravityPullsAParticleDownAndDragSlowsIt)
{
    ParticleEmitterSettings settings = QuietSettings();
    settings.Speed = 0.0f;
    settings.SpeedVariance = 0.0f;
    settings.Drag = 0.0f;

    Particle particle;
    particle.State = Microsoft::Xna::Framework::Vector4(0.0f, 10.0f, 0.0f, 1.0f);
    ParticleSystem::step(particle, 0, settings, 0.5f);
    EXPECT_LT(particle.Velocity.Y, 0.0f);
    EXPECT_LT(particle.Position.Y, 0.0f);

    // The same step with drag must leave it moving more slowly than without.
    Particle dragged;
    dragged.State = Microsoft::Xna::Framework::Vector4(0.0f, 10.0f, 0.0f, 1.0f);
    settings.Drag = 2.0f;
    ParticleSystem::step(dragged, 0, settings, 0.5f);
    EXPECT_GT(dragged.Velocity.Y, particle.Velocity.Y);
}

TEST(ParticleSystemTest, AParticleThatOutlivesItsLifetimeIsBornAgainAtTheEmitter)
{
    ParticleEmitterSettings settings = QuietSettings();
    settings.Position = Vector3(5.0f, 6.0f, 7.0f);
    settings.Gravity = Vector3(0.0f, 0.0f, 0.0f);
    settings.Drag = 0.0f;

    Particle particle;
    particle.Position = Microsoft::Xna::Framework::Vector4(100.0f, 100.0f, 100.0f, 0.0f);
    particle.State = Microsoft::Xna::Framework::Vector4(0.9f, 1.0f, 0.0f, 3.0f);

    ParticleSystem::step(particle, 0, settings, 0.2f);
    // Born at the emitter, one generation on, with the overshoot carried into its new age rather
    // than thrown away -- otherwise a long step silently shortens every lifetime it spans.
    EXPECT_FLOAT_EQ(particle.State.W, 4.0f);
    EXPECT_NEAR(particle.State.X, 0.1f, 1e-5f);
    const float travelled = 0.1f * QuietSettings().Speed * 1.5f;
    EXPECT_NEAR(particle.Position.X, 5.0f, travelled + 0.01f);
    EXPECT_NEAR(particle.Position.Z, 7.0f, travelled + 0.01f);
}

TEST(ParticleSystemTest, TheGpuSimulationAgreesWithTheCpuOne)
{
    GraphicsDevice device;
    ParticleSystem system(device, 128);
    if (!system.getUnsupportedReason().empty())
        GTEST_SKIP() << system.getUnsupportedReason();

    system.setSettings(QuietSettings());
    system.reset();

    std::vector<Particle> mirror = system.readParticlesEXT();
    const int active = system.getActiveCount();
    ASSERT_GT(active, 0);

    constexpr float kStep = 1.0f / 60.0f;
    for (int frame = 0; frame < 40; ++frame)
    {
        system.update(kStep);
        for (int i = 0; i < active; ++i)
            ParticleSystem::step(mirror[static_cast<std::size_t>(i)], i, system.getSettings(),
                                 kStep);
    }
    ASSERT_TRUE(system.usesCompute()) << "the GPU path did not run, so nothing was compared";

    const std::vector<Particle> gpu = system.readParticlesEXT();
    ASSERT_EQ(gpu.size(), mirror.size());

    int respawned = 0;
    for (int i = 0; i < active; ++i)
    {
        const Particle& a = gpu[static_cast<std::size_t>(i)];
        const Particle& b = mirror[static_cast<std::size_t>(i)];
        // The generation is an integer count, so it must match exactly: a difference here means the
        // two simulations disagreed about WHEN a particle died, not merely about where it was.
        ASSERT_FLOAT_EQ(a.State.W, b.State.W) << "particle " << i << " respawned at different times";
        if (a.State.W > 1.0f) ++respawned;
        EXPECT_NEAR(a.State.X, b.State.X, 1e-4f) << "particle " << i;
        EXPECT_NEAR(a.Position.X, b.Position.X, 1e-3f) << "particle " << i;
        EXPECT_NEAR(a.Position.Y, b.Position.Y, 1e-3f) << "particle " << i;
        EXPECT_NEAR(a.Position.Z, b.Position.Z, 1e-3f) << "particle " << i;
    }
    EXPECT_GT(respawned, 0)
        << "no particle lived out its lifetime, so the respawn half was never compared";
}

TEST(ParticleSystemTest, PinningTheSimulationToTheCpuCarriesTheParticlesAcross)
{
    // The fallback has to be runnable on a device that does not need it, or nothing ever runs it.
    GraphicsDevice device;
    ParticleSystem system(device, 64);
    system.setSettings(QuietSettings());
    system.reset();

    system.update(1.0f / 60.0f);
    const std::vector<Particle> before = system.readParticlesEXT();

    system.setSimulationOnCpuEXT(true);
    EXPECT_TRUE(system.isSimulationOnCpuEXT());
    const std::vector<Particle> carried = system.readParticlesEXT();
    ASSERT_EQ(carried.size(), before.size());
    for (std::size_t i = 0; i < carried.size(); ++i)
        EXPECT_FLOAT_EQ(carried[i].Position.Y, before[i].Position.Y)
            << "switching path restarted particle " << i;

    system.update(1.0f / 60.0f);
    EXPECT_FALSE(system.usesCompute());

    system.setSimulationOnCpuEXT(false);
    system.update(1.0f / 60.0f);
    if (system.getUnsupportedReason().empty())
        EXPECT_TRUE(system.usesCompute()) << "the GPU path did not resume";
}

TEST(ParticleSystemTest, TheCpuPathDrawsTheSameParticlesTheGpuPathWouldHave)
{
    GraphicsDevice device;
    ParticleSystem system(device, 128);
    ParticleEmitterSettings settings = QuietSettings();
    settings.Gravity = Vector3(0.0f, 0.0f, 0.0f);
    settings.Speed = 0.0f;
    settings.SpeedVariance = 0.0f;
    settings.Drag = 0.0f;
    settings.StartSize = 1.0f;
    settings.EndSize = 1.0f;
    system.setSettings(settings);
    system.setSimulationOnCpuEXT(true);
    system.reset();

    const auto texture = WhiteTexture(device);
    RenderTarget2D target(device, kSize, kSize);
    const Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 10.0f), Vector3::Zero,
                                             Vector3(0.0f, 1.0f, 0.0f));
    const Matrix projection =
        Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f);

    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setBlendStateProperty(BlendState::Opaque);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    system.draw(view, projection, texture.get());
    device.SetRenderTarget(nullptr);

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    int lit = 0;
    for (const Color& texel : pixels) if (texel.getRProperty() > 32) ++lit;
    EXPECT_GT(lit, 0) << "the CPU draw path put nothing on screen";
}

TEST(ParticleSystemTest, SoftnessDefaultsToOffAndIsClamped)
{
    GraphicsDevice device;
    ParticleSystem system(device, 32);
    EXPECT_FLOAT_EQ(system.getSoftnessEXT(), 0.0f)
        << "a game that never asked for soft particles must get the hard edges it had";
    system.setSoftnessEXT(2.5f);
    EXPECT_FLOAT_EQ(system.getSoftnessEXT(), 2.5f);
    system.setSoftnessEXT(-1.0f);
    EXPECT_FLOAT_EQ(system.getSoftnessEXT(), 0.0f);
}

TEST(ParticleSystemTest, AParticleTouchingGeometryFadesAndOneInFrontOfItDoesNot)
{
    // MOD-2109. The whole point of the feature is a *difference*, so the test measures one: the
    // same particles, the same camera, the same texture, and only the distance between them and
    // the surface behind them changes. A draw that ignored the depth image would produce the same
    // brightness twice.
    GraphicsDevice device;
    ParticleSystem system(device, 64);
    if (!system.getUnsupportedReason().empty()) GTEST_SKIP() << system.getUnsupportedReason();

    ParticleEmitterSettings settings = QuietSettings();
    settings.Gravity = Vector3(0.0f, 0.0f, 0.0f);
    settings.Speed = 0.0f;
    settings.SpeedVariance = 0.0f;
    settings.Drag = 0.0f;
    settings.StartSize = 2.0f;
    settings.EndSize = 2.0f;
    // ONE particle. Sixty-four of them land on the same eight-by-eight patch of screen, and alpha
    // compounds: a 2% contribution repeated sixty-four times is `1 - 0.98^64`, which is 47% and
    // looks exactly like a fade that is not working. The first version of this test stacked them
    // and read 45% where it expected zero; the shader was right and the scene was wrong.
    settings.EmissionRate = 0.5f;
    settings.Lifetime = 2.0f;
    settings.LifetimeVariance = 0.0f;

    constexpr float kFar = 100.0f;
    const Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 20.0f), Vector3::Zero,
                                             Vector3(0.0f, 1.0f, 0.0f));
    const Matrix projection =
        Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, kFar);

    const auto texture = WhiteTexture(device);
    system.setSoftnessEXT(4.0f);
    settings.Position = Vector3(0.0f, 0.0f, 0.0f);   // 20 units from the eye, and it stays there
    system.setSettings(settings);
    system.reset();

    // Only the WALL moves. Moving the particles instead would change how much of the screen they
    // cover -- a nearer billboard is a bigger one -- and the test would be measuring perspective
    // rather than the fade. The first version of this test did exactly that and passed anyway.
    const auto brightnessBehindWallAt = [&](const float wallDepth) {
        std::vector<Color> depthTexels(static_cast<std::size_t>(kSize) * kSize,
                                       CnaTest::EngineLayer::DepthTexel(device, wallDepth / kFar));
        Texture2D wall(device, kSize, kSize);
        wall.SetData(depthTexels.data(), static_cast<int>(depthTexels.size()));
        system.setDepthInputEXT(&wall, kFar);

        RenderTarget2D target(device, kSize, kSize);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        // Alpha blending over black, because the fade is *in the alpha*: with opaque blending the
        // colour is written whatever the alpha says, and the whole effect would be invisible.
        device.setBlendStateProperty(BlendState::NonPremultiplied);
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        system.draw(view, projection, texture.get());
        device.SetRenderTarget(nullptr);
        device.setBlendStateProperty(BlendState::Opaque);

        std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        long sum = 0;
        for (const Color& texel : pixels) sum += texel.getRProperty();
        return sum;
    };

    // The wall exactly where the particles are, and then well behind them. Same particles, same
    // coverage, same everything else.
    const long touching = brightnessBehindWallAt(20.0f);
    const long inFront  = brightnessBehindWallAt(40.0f);

    std::printf("[ MOD-2109 ] brightness with the wall touching %ld, well behind %ld\n",
                touching, inFront);
    EXPECT_GT(inFront, 0) << "the particles never reached the frame at all";
    EXPECT_LT(touching * 8, inFront)
        << "a particle sitting on the surface behind it was not faded";
}

TEST(ParticleSystemTest, DrawingRefusesWithoutATexture)
{
    GraphicsDevice device;
    ParticleSystem system(device, 32);
    system.setSettings(QuietSettings());
    EXPECT_THROW(system.draw(Matrix::getIdentityProperty(), Matrix::getIdentityProperty(), nullptr),
                 std::invalid_argument);
}

TEST(ParticleSystemTest, ARateOfZeroDrawsNothingAtAll)
{
    GraphicsDevice device;
    ParticleSystem system(device, 32);
    ParticleEmitterSettings settings = QuietSettings();
    settings.EmissionRate = 0.0f;
    system.setSettings(settings);

    const auto texture = WhiteTexture(device);
    RenderTarget2D target(device, kSize, kSize);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    system.draw(Matrix::getIdentityProperty(), Matrix::getIdentityProperty(), texture.get());
    device.SetRenderTarget(nullptr);

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    for (const Color& texel : pixels) EXPECT_EQ(texel.getRProperty(), 0);
}

TEST(ParticleSystemTest, TheParticlesReachTheFrame)
{
    GraphicsDevice device;
    ParticleSystem system(device, 128);
    ParticleEmitterSettings settings = QuietSettings();
    settings.Gravity = Vector3(0.0f, 0.0f, 0.0f);
    settings.Speed = 0.0f;
    settings.SpeedVariance = 0.0f;
    settings.Drag = 0.0f;
    settings.StartSize = 1.0f;
    settings.EndSize = 1.0f;
    system.setSettings(settings);
    system.reset();

    const auto texture = WhiteTexture(device);
    RenderTarget2D target(device, kSize, kSize);
    const Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 10.0f), Vector3::Zero,
                                             Vector3(0.0f, 1.0f, 0.0f));
    const Matrix projection =
        Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f);

    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setBlendStateProperty(BlendState::Opaque);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    system.draw(view, projection, texture.get());
    device.SetRenderTarget(nullptr);

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    int lit = 0;
    for (const Color& texel : pixels) if (texel.getRProperty() > 32) ++lit;
    EXPECT_GT(lit, 0) << "every particle is at the origin in front of the camera; none was drawn";
}

} // namespace

#endif // CNA_CNAEXT
