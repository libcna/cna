// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-1550: a GPU particle system, and the honest shape of one on CNA today.
//
// 100 000 particles are integrated by a compute shader into a storage buffer. What CNA cannot yet
// do is DRAW from that buffer: a storage buffer and a vertex buffer are separate objects with no
// way to alias the same GPU memory, so the positions have to come back to the CPU before they can
// be drawn. This program measures all three costs -- the GPU simulation, the same simulation on the
// CPU, and the read-back -- so the missing piece is a number rather than an opinion.
//
// Check A -- the renderer supports compute, or the program SKIPs.
// Check B -- the GPU integration matches a CPU integration of the same steps.
// Check C -- the particles reach a frame (drawn instanced, after the read-back).
//
// `--benchmark` reports the per-frame costs (MOD-1550's own recording).
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/InstancedRendererEXT.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "System/NotSupportedException.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::GraphicsCapability;
using CNA::Graphics::ComputeShader;
using CNA::Graphics::InstancedRendererEXT;
using CNA::Graphics::StorageBufferT;

namespace
{
    constexpr int kFrame = 128;
    constexpr int kParticles = 100000;
    constexpr int kDrawn = 2000;      ///< how many are actually drawn; see the note in Draw()
    constexpr float kStep = 1.0f / 60.0f;

    /// Position in xyz, life in w -- one vec4, which is what std430 wants anyway.
    struct Particle
    {
        float position[4];
        float velocity[4];
    };

    const char* const kIntegrator = R"(#version 310 es
layout(local_size_x = 64) in;
struct Particle { vec4 position; vec4 velocity; };
layout(std430, binding = 0) buffer Particles { Particle particles[]; };
uniform int uCount;
uniform float uStep;
void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= uint(uCount)) return;
    vec3 velocity = particles[index].velocity.xyz + vec3(0.0, -9.81, 0.0) * uStep;
    vec3 position = particles[index].position.xyz + velocity * uStep;
    if (position.y < 0.0) { position.y = -position.y; velocity.y = -velocity.y * 0.5; }
    particles[index].position = vec4(position, particles[index].position.w);
    particles[index].velocity = vec4(velocity, 0.0);
}
)";

    std::vector<Particle> InitialParticles()
    {
        std::vector<Particle> particles;
        particles.reserve(kParticles);
        for (int i = 0; i < kParticles; ++i)
        {
            const auto f = static_cast<float>(i);
            particles.push_back(Particle{
                {std::sin(f * 0.017f) * 20.0f, 5.0f + std::fmod(f, 30.0f),
                 std::cos(f * 0.013f) * 20.0f, 1.0f},
                {std::cos(f * 0.011f) * 2.0f, 0.0f, std::sin(f * 0.019f) * 2.0f, 0.0f}});
        }
        return particles;
    }

    /// The same integration, on the CPU -- both the correctness oracle and the perf baseline.
    void StepOnCpu(std::vector<Particle>& particles, const float step)
    {
        for (Particle& particle : particles)
        {
            float velocity[3] = {particle.velocity[0], particle.velocity[1] - 9.81f * step,
                                 particle.velocity[2]};
            float position[3] = {particle.position[0] + velocity[0] * step,
                                 particle.position[1] + velocity[1] * step,
                                 particle.position[2] + velocity[2] * step};
            if (position[1] < 0.0f)
            {
                position[1] = -position[1];
                velocity[1] = -velocity[1] * 0.5f;
            }
            for (int i = 0; i < 3; ++i)
            {
                particle.position[i] = position[i];
                particle.velocity[i] = velocity[i];
            }
        }
    }
}

class ComputeParticlesExample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<VertexBuffer> vertices_;
    std::unique_ptr<IndexBuffer> indices_;
    std::unique_ptr<ModelMeshPart> part_;
    bool benchmark_ = false;
    int  passCount_ = 0;
    int  checkCount_ = 0;
    int  result_ = 1;

    void check(bool ok, const std::string& label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    void BuildQuad(GraphicsDevice& device)
    {
        const std::vector<VertexPositionColor> data{
            VertexPositionColor(Vector3(-0.2f, -0.2f, 0.0f), Color(255, 220, 120, 255)),
            VertexPositionColor(Vector3(0.2f, -0.2f, 0.0f), Color(255, 220, 120, 255)),
            VertexPositionColor(Vector3(0.2f, 0.2f, 0.0f), Color(255, 220, 120, 255)),
            VertexPositionColor(Vector3(-0.2f, 0.2f, 0.0f), Color(255, 220, 120, 255))};
        const std::vector<std::uint16_t> indexData{0, 1, 2, 0, 2, 3};
        vertices_ = std::make_unique<VertexBuffer>(
            device, VertexPositionColor::getVertexDeclarationStatic(), 4, BufferUsage::WriteOnly);
        vertices_->SetData(data.data(), 4);
        indices_ = std::make_unique<IndexBuffer>(device, IndexElementSize::SixteenBits, 6,
                                                 BufferUsage::WriteOnly);
        indices_->SetData(indexData.data(), 6);
        part_ = std::make_unique<ModelMeshPart>(vertices_.get(), indices_.get(), 4, 2, 0, 0);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        if (!device.SupportsCapability(GraphicsCapability::ComputeShaders))
        {
            std::printf("SKIP: this renderer does not support compute shaders (a documented "
                        "capability boundary, not a defect)\n");
            std::exit(77);
        }
        if (!device.SupportsCapability(GraphicsCapability::ThreeD) ||
            !device.SupportsCapability(GraphicsCapability::Instancing))
        {
            std::printf("SKIP: this renderer cannot raster 3D or instance\n");
            std::exit(77);
        }

        const std::vector<Particle> initial = InitialParticles();
        StorageBufferT<Particle> buffer(device, kParticles);
        buffer.setData(initial);

        ComputeShader integrator(device, kIntegrator);
        integrator.bindStorageBuffer(0, buffer.getBuffer());
        integrator.setUniform("uCount", kParticles);
        integrator.setUniform("uStep", kStep);

        constexpr int kSteps = 30;
        for (int i = 0; i < kSteps; ++i)
            integrator.dispatch((kParticles + 63) / 64);

        std::vector<Particle> cpu = initial;
        for (int i = 0; i < kSteps; ++i) StepOnCpu(cpu, kStep);

        const std::vector<Particle> gpu = buffer.getData();
        double worst = 0.0;
        for (int i = 0; i < kParticles; ++i)
            for (int axis = 0; axis < 3; ++axis)
                worst = std::max(worst,
                                 static_cast<double>(std::fabs(
                                     gpu[static_cast<std::size_t>(i)].position[axis]
                                     - cpu[static_cast<std::size_t>(i)].position[axis])));
        std::printf("    %d particles, %d steps: worst GPU-CPU position difference %.6f\n",
                    kParticles, kSteps, worst);
        // Both sides do the same arithmetic in the same order, so this is float rounding only.
        check(worst < 0.01, "the GPU integration matches the CPU one");

        // --- the particles reach a frame -------------------------------------------------------
        // Only kDrawn of them: this is where CNA's missing piece shows. A storage buffer cannot be
        // bound as a vertex stream, so every particle drawn has to be copied back to the CPU and
        // re-uploaded as instance transforms. Drawing all 100 000 that way measures the copy, not
        // the renderer.
        BuildQuad(device);
        InstancedRendererEXT renderer(device, part_.get());
        std::vector<Matrix> transforms;
        transforms.reserve(kDrawn);
        for (int i = 0; i < kDrawn; ++i)
            transforms.push_back(Matrix::CreateTranslation(
                Vector3(gpu[static_cast<std::size_t>(i)].position[0],
                        gpu[static_cast<std::size_t>(i)].position[1],
                        gpu[static_cast<std::size_t>(i)].position[2])));
        renderer.setInstances(transforms);

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 15.0f, 60.0f),
                                                    Vector3(0.0f, 5.0f, 0.0f),
                                                    Vector3(0.0f, 1.0f, 0.0f)));
        effect.setProjectionProperty(
            Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 1.0f, 500.0f));
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.VertexColorEnabled = true;

        device.Clear(Color::Black);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setBlendStateProperty(BlendState::Opaque);
        renderer.draw(effect);

        std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);
        try { device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size())); }
        catch (const System::NotSupportedException&)
        {
            std::printf("SKIP: this renderer has no readable back buffer\n");
            std::exit(77);
        }
        int lit = 0;
        for (const Color& pixel : pixels)
            if (pixel.getRProperty() > 20) ++lit;
        std::printf("    %d of %d particles drawn in %d draw call(s): %d lit pixels\n", kDrawn,
                    kParticles, renderer.getLastDrawCallCount(), lit);
        check(lit > 20 && renderer.getLastDrawCallCount() == 1,
              "the simulated particles reach the frame in one instanced draw call");

        if (benchmark_)
        {
            const auto timeOf = [](auto&& work) {
                work();
                const auto start = std::chrono::steady_clock::now();
                constexpr int kRuns = 10;
                for (int i = 0; i < kRuns; ++i) work();
                return std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - start).count() / kRuns;
            };

            const double gpuStep = timeOf([&] { integrator.dispatch((kParticles + 63) / 64); });
            std::vector<Particle> cpuCopy = initial;
            const double cpuStep = timeOf([&] { StepOnCpu(cpuCopy, kStep); });
            const double readBack = timeOf([&] { (void)buffer.getData(); });

            std::printf("--- MOD-1550: %d particles, Mesa llvmpipe ---\n", kParticles);
            std::printf("    GPU simulation step : %8.3f ms\n", gpuStep);
            std::printf("    CPU simulation step : %8.3f ms\n", cpuStep);
            std::printf("    read-back to the CPU: %8.3f ms  <- the cost CNA cannot yet avoid\n",
                        readBack);
        }

        std::printf("%d/%d checks passed\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    explicit ComputeParticlesExample(bool benchmark) : benchmark_(benchmark)
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kFrame);
        gdm_->setPreferredBackBufferHeightProperty(kFrame);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int result() const { return result_; }
};

int main(int argc, char** argv)
{
    try
    {
        bool benchmark = false;
        for (int i = 1; i < argc; ++i)
            if (std::strcmp(argv[i], "--benchmark") == 0) benchmark = true;

        ComputeParticlesExample example(benchmark);
        example.Run();
        return example.result();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        // The registered ctest points at CNA_TEST_DISPLAY, which is not always a display that
        // exists; SKIP is the honest answer there, not a crash.
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
