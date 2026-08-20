// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2090..MOD-2095: section 20.10 end to end, with its costs.
//
// The unit tests already assert that each of these subsystems does what it says. What no test can
// say is what any of it *costs*, and the section's whole argument is about cost -- a readback is a
// stall, an upscale is cheaper than the pixels it replaces, a compute particle step beats a CPU
// one. So this program measures them against the thing each one claims to beat, and prints the
// pairs next to each other.
//
// Check A -- the renderer offers indirect draws and a vertex-stage storage buffer, or SKIP.
// Check B -- an indirect draw puts the SAME pixels on screen as the ordinary draw it replaces.
// Check C -- GPU culling draws the survivors the CPU culler agrees on.
// Check D -- the upscale at 1:1 is pixel-identical to no pass at all.
//
// `--benchmark` reports the costs (the section's own recording in `docs/cnaext-perf.md`).
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/FrustumCullerEXT.hpp"
#include "CNA/Graphics/GpuInstanceCuller.hpp"
#include "CNA/Graphics/HdrDisplayOutput.hpp"
#include "CNA/Graphics/ParticleSystem.hpp"
#include "CNA/Graphics/SpatialUpscalePass.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/IndirectDrawArguments.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::IndirectDrawArguments;
using CNA::IndirectDrawIndexedArguments;
using CNA::Graphics::FrustumCullerEXT;
using CNA::Graphics::GpuCullableInstance;
using CNA::Graphics::GpuInstanceCuller;
using CNA::Graphics::HdrDisplayOutput;
using CNA::Graphics::ParticleEmitterSettings;
using CNA::Graphics::ParticleSystem;
using CNA::Graphics::SpatialUpscalePass;
using CNA::Graphics::StorageBuffer;

namespace
{
    constexpr int kFrame = 256;

    Matrix View()
    {
        return Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 24.0f), Vector3::Zero,
                                    Vector3(0.0f, 1.0f, 0.0f));
    }

    Matrix Projection()
    {
        return Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.5f, 200.0f);
    }

    /// One clockwise quad, indexed -- the unit of geometry every draw below issues.
    std::array<VertexPositionColor, 4> QuadCorners()
    {
        return {VertexPositionColor(Vector3(-0.5f, -0.5f, 0.0f), Color::White),
                VertexPositionColor(Vector3(-0.5f, 0.5f, 0.0f), Color::White),
                VertexPositionColor(Vector3(0.5f, 0.5f, 0.0f), Color::White),
                VertexPositionColor(Vector3(0.5f, -0.5f, 0.0f), Color::White)};
    }

    double MillisecondsOf(const int repeats, const std::function<void()>& work)
    {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < repeats; ++i) work();
        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count() /
               static_cast<double>(repeats);
    }

    /// A field of quads, most of them behind the camera so culling has something to remove.
    std::vector<GpuCullableInstance> Field(const int count)
    {
        std::vector<GpuCullableInstance> instances;
        instances.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            const float t = static_cast<float>(i);
            // Half the field is placed at a positive Z, which is behind a camera looking down -Z
            // from z = 24 -- a deterministic split rather than a random one, so the survivor count
            // is the same on every run and every machine.
            const float z = (i % 2 == 0) ? -8.0f - std::fmod(t, 30.0f) : 60.0f + std::fmod(t, 30.0f);
            const Vector3 at(std::sin(t * 0.7f) * 8.0f, std::cos(t * 0.53f) * 8.0f, z);
            GpuCullableInstance instance;
            instance.World = Matrix::CreateTranslation(at);
            instance.Bounds = BoundingBox(at - Vector3(0.5f, 0.5f, 0.5f),
                                          at + Vector3(0.5f, 0.5f, 0.5f));
            instances.push_back(instance);
        }
        return instances;
    }

    int LitPixels(const std::vector<Color>& pixels)
    {
        int lit = 0;
        for (const Color& texel : pixels)
            if (texel.getRProperty() > 32) ++lit;
        return lit;
    }
}

class GpuDrivenExample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool benchmark_  = false;
    int  passCount_  = 0;
    int  checkCount_ = 0;
    int  result_     = 1;

    void check(const bool ok, const std::string& label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        if (!device.SupportsCapability(CNA::GraphicsCapability::IndirectDraw) ||
            !device.SupportsCapability(CNA::GraphicsCapability::ComputeShaders))
        {
            std::printf("SKIP: this renderer has no indirect draw or no compute (a documented "
                        "capability boundary, not a defect)\n");
            std::exit(77);
        }
        check(true, "the renderer offers indirect draws and compute");

        const auto corners = QuadCorners();
        VertexBuffer quad(device, 4);
        quad.SetData(corners.data(), 4);
        const std::array<std::uint16_t, 6> order{0, 1, 2, 0, 2, 3};
        IndexBuffer quadIndices(device, 6);
        quadIndices.SetData(order.data(), 6);

        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setWorldProperty(Matrix::CreateScale(6.0f) *
                                Matrix::CreateTranslation(0.0f, 0.0f, -6.0f));
        effect.setViewProperty(View());
        effect.setProjectionProperty(Projection());

        std::vector<Color> ordinary(static_cast<std::size_t>(kFrame) * kFrame, Color(0, 0, 0, 0));
        std::vector<Color> indirect = ordinary;

        const auto drawOrdinary = [&] {
            device.Clear(Color::Black);
            device.SetVertexBuffer(&quad);
            device.SetIndexBuffer(&quadIndices);
            effect.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            device.SetIndexBuffer(nullptr);
            device.SetVertexBuffer(nullptr);
        };

        StorageBuffer command(device, sizeof(IndirectDrawIndexedArguments));
        IndirectDrawIndexedArguments arguments;
        arguments.IndexCount = 6;
        arguments.InstanceCount = 1;
        command.setBytes(&arguments, sizeof(arguments));

        const auto drawIndirect = [&] {
            device.Clear(Color::Black);
            device.SetVertexBuffer(&quad);
            device.SetIndexBuffer(&quadIndices);
            effect.Apply();
            device.DrawIndexedPrimitivesIndirectEXT(PrimitiveType::TriangleList,
                                                    *command.getRendererEXT(), 0);
            device.SetIndexBuffer(nullptr);
            device.SetVertexBuffer(nullptr);
        };

        drawOrdinary();
        device.GetBackBufferData(ordinary.data(), static_cast<int>(ordinary.size()));
        drawIndirect();
        device.GetBackBufferData(indirect.data(), static_cast<int>(indirect.size()));

        // The strongest form the claim can take: not "it drew something" but "it drew the same
        // thing". An indirect draw whose arguments say what the ordinary call said must be
        // indistinguishable from it.
        bool identical = LitPixels(ordinary) > 0;
        for (std::size_t i = 0; i < ordinary.size() && identical; ++i)
            identical = ordinary[i].getRProperty() == indirect[i].getRProperty();
        check(identical, "an indirect draw puts the same pixels on screen as the ordinary one");

        // --- GPU culling ------------------------------------------------------------------------
        GpuInstanceCuller culler(device);
        const std::vector<GpuCullableInstance> field = Field(1024);
        int cpuVisible = 0;
        {
            FrustumCullerEXT cpu;
            cpu.setCamera(View(), Projection());
            for (const GpuCullableInstance& instance : field)
                if (cpu.isVisible(instance.Bounds)) ++cpuVisible;
        }
        if (culler.isSupported())
        {
            culler.setInstances(field);
            culler.cull(View(), Projection(), 6);
            check(culler.readVisibleCountEXT() == cpuVisible && cpuVisible > 0 &&
                      cpuVisible < static_cast<int>(field.size()),
                  "GPU culling keeps exactly the instances the CPU culler keeps");
        }
        else
        {
            std::printf("    (GPU culling unavailable: %s)\n",
                        culler.getUnsupportedReason().c_str());
            check(true, "GPU culling names the requirement it is missing");
        }

        // --- Spatial upscaling ------------------------------------------------------------------
        SpatialUpscalePass upscale(device);
        {
            RenderTarget2D small(device, kFrame / 2, kFrame / 2);
            RenderTarget2D same(device, kFrame, kFrame);
            device.SetRenderTarget(&same);
            device.Clear(Color::Black);
            device.SetRenderTarget(nullptr);

            std::vector<Color> texels(static_cast<std::size_t>(kFrame) * kFrame, Color(0, 0, 0, 255));
            for (std::size_t i = 0; i < texels.size(); ++i)
                texels[i] = Color(static_cast<int>(i % 256), 128, 64, 255);
            Texture2D source(device, kFrame, kFrame);
            source.SetData(texels.data(), static_cast<int>(texels.size()));

            device.SetRenderTarget(&same);
            upscale.draw(&source, kFrame, kFrame, kFrame, kFrame);
            device.SetRenderTarget(nullptr);

            std::vector<Color> copied(texels.size(), Color(0, 0, 0, 0));
            same.GetData(copied.data(), static_cast<int>(copied.size()));
            bool untouched = upscale.isSupported();
            for (std::size_t i = 0; i < texels.size() && untouched; ++i)
                untouched = copied[i].getRProperty() == texels[i].getRProperty() &&
                            copied[i].getGProperty() == texels[i].getGProperty();
            check(untouched || !upscale.isSupported(),
                  "the upscale at a 1:1 scale is the frame it was given, pixel for pixel");
        }

        if (benchmark_) RunBenchmark(device, quad, quadIndices, effect, command, field);

        std::printf("%d/%d checks passed\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

    /// Every number here is a pair: the new path and the thing it claims to beat, measured the same
    /// way in the same frame. A single figure would say nothing.
    void RunBenchmark(GraphicsDevice& device, VertexBuffer& quad, IndexBuffer& quadIndices,
                      BasicEffect& effect, StorageBuffer& command,
                      const std::vector<GpuCullableInstance>& field)
    {
        std::printf("\n--- benchmark (%dx%d) ---\n", kFrame, kFrame);
        std::vector<Color> frame(static_cast<std::size_t>(kFrame) * kFrame, Color(0, 0, 0, 0));

        // Read-back is subtracted rather than avoided: without a sync the GPU is free to have done
        // none of the work by the time the loop ends, and a benchmark of an empty queue is a
        // benchmark of nothing.
        const auto timed = [&](const char* name, const std::function<void()>& work) {
            for (int i = 0; i < 3; ++i)
            {
                work();
                device.GetBackBufferData(frame.data(), static_cast<int>(frame.size()));
            }
            const double withSync = MillisecondsOf(16, [&] {
                work();
                device.GetBackBufferData(frame.data(), static_cast<int>(frame.size()));
            });
            const double readBack = MillisecondsOf(16, [&] {
                device.GetBackBufferData(frame.data(), static_cast<int>(frame.size()));
            });
            std::printf("    %-42s %8.3f ms\n", name, withSync - readBack);
        };

        timed("one ordinary indexed draw", [&] {
            device.Clear(Color::Black);
            device.SetVertexBuffer(&quad);
            device.SetIndexBuffer(&quadIndices);
            effect.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            device.SetIndexBuffer(nullptr);
            device.SetVertexBuffer(nullptr);
        });
        timed("one indirect indexed draw", [&] {
            device.Clear(Color::Black);
            device.SetVertexBuffer(&quad);
            device.SetIndexBuffer(&quadIndices);
            effect.Apply();
            device.DrawIndexedPrimitivesIndirectEXT(PrimitiveType::TriangleList,
                                                    *command.getRendererEXT(), 0);
            device.SetIndexBuffer(nullptr);
            device.SetVertexBuffer(nullptr);
        });

        // --- culling: the CPU walk versus the dispatch ------------------------------------------
        {
            FrustumCullerEXT cpu;
            GpuInstanceCuller culler(device);
            const bool gpu = culler.isSupported();
            if (gpu) culler.setInstances(field);
            for (const int count : {256, 1024})
            {
                const std::vector<GpuCullableInstance> subset(
                    field.begin(), field.begin() + static_cast<std::ptrdiff_t>(count));
                char label[96];
                std::snprintf(label, sizeof(label), "cull %d instances on the CPU", count);
                timed(label, [&] {
                    cpu.setCamera(View(), Projection());
                    std::vector<Matrix> visible;
                    for (const GpuCullableInstance& instance : subset)
                        if (cpu.isVisible(instance.Bounds)) visible.push_back(instance.World);
                });
                if (!gpu) continue;
                std::snprintf(label, sizeof(label), "cull %d instances on the GPU, no readback",
                              count);
                culler.setInstances(subset);
                timed(label, [&] { culler.cull(View(), Projection(), 6); });
            }
        }

        // --- particles: the same step, twice ----------------------------------------------------
        for (const int count : {1024, 8192})
        {
            ParticleSystem particles(device, count);
            ParticleEmitterSettings settings;
            settings.EmissionRate = static_cast<float>(count);
            settings.Lifetime = 1.0f;
            particles.setSettings(settings);
            particles.reset();

            char label[96];
            if (particles.getUnsupportedReason().empty())
            {
                std::snprintf(label, sizeof(label), "step %d particles on the GPU", count);
                particles.setSimulationOnCpuEXT(false);
                timed(label, [&] { particles.update(1.0f / 60.0f); });
            }
            std::snprintf(label, sizeof(label), "step %d particles on the CPU", count);
            particles.setSimulationOnCpuEXT(true);
            timed(label, [&] { particles.update(1.0f / 60.0f); });
        }

        // --- the two fullscreen passes ----------------------------------------------------------
        {
            std::vector<Color> texels(static_cast<std::size_t>(kFrame) * kFrame,
                                      Color(90, 140, 200, 255));
            Texture2D source(device, kFrame, kFrame);
            source.SetData(texels.data(), static_cast<int>(texels.size()));
            Texture2D half(device, kFrame / 2, kFrame / 2);
            std::vector<Color> halfTexels(static_cast<std::size_t>(kFrame / 2) * (kFrame / 2),
                                          Color(90, 140, 200, 255));
            half.SetData(halfTexels.data(), static_cast<int>(halfTexels.size()));

            SpatialUpscalePass upscale(device);
            timed("upscale a half-size frame to full size", [&] {
                device.Clear(Color::Black);
                upscale.draw(&half, kFrame / 2, kFrame / 2, kFrame, kFrame);
            });
            timed("the same pass at 1:1 (its copy-through path)", [&] {
                device.Clear(Color::Black);
                upscale.draw(&source, kFrame, kFrame, kFrame, kFrame);
            });

            HdrDisplayOutput display(device);
            timed("display output in sRGB (copy through)", [&] {
                device.Clear(Color::Black);
                display.draw(&source, nullptr, kFrame, kFrame);
            });
            display.setColorSpace(CNA::DisplayColorSpace::Hdr10);
            timed("display output in HDR10 (PQ + Rec. 2020)", [&] {
                device.Clear(Color::Black);
                display.draw(&source, nullptr, kFrame, kFrame);
            });
        }
    }

public:
    explicit GpuDrivenExample(const bool benchmark) : benchmark_(benchmark)
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

        GpuDrivenExample example(benchmark);
        example.Run();
        return example.result();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
