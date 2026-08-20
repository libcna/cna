// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2048: clustered forward shading, end to end, with its cost per light count.
//
// The section's claim is that a scene can hold hundreds of lights because a fragment only pays for
// the ones its own cluster holds. That claim is only worth as much as the numbers behind it, so
// this program measures the three stages separately -- sorting the lights into clusters, uploading
// the result, and shading with it -- at four light counts, and prints them next to each other.
//
// Check A -- the renderer runs the clustered effect, or the program SKIPs.
// Check B -- the GPU assignment matches the CPU one exactly, where compute exists.
// Check C -- 256 lights reach the frame.
//
// `--benchmark` reports the per-stage costs (MOD-2048's own recording).
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/ClusteredForwardEffect.hpp"
#include "CNA/Graphics/ClusteredLightAssignment.hpp"
#include "CNA/Graphics/ClusteredLightBuffer.hpp"
#include "CNA/Graphics/ClusteredLightCompute.hpp"
#include "CNA/Graphics/ClusteredLightGrid.hpp"
#include "CNA/Graphics/ClusteredLightSetEXT.hpp"
#include "CNA/Graphics/PbrMaterialExtensions.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <utility>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::ClusteredForwardEffect;
using CNA::Graphics::ClusteredLightAssignment;
using CNA::Graphics::ClusteredLightBuffer;
using CNA::Graphics::ClusteredLightCompute;
using CNA::Graphics::ClusteredLightEXT;
using CNA::Graphics::ClusteredLightGrid;
using CNA::Graphics::ClusteredLightSetEXT;
using CNA::Graphics::ClusteredLightType;
using CNA::Graphics::PbrMaterialExtensions;

namespace
{
    constexpr int   kFrame = 256;
    constexpr float kNear  = 0.5f;
    constexpr float kFar   = 120.0f;
    constexpr float kWallZ = -14.0f;
    constexpr float kHalf  = 12.0f;

    Matrix View()
    {
        return Matrix::CreateLookAt(Vector3::Zero, Vector3(0.0f, 0.0f, -1.0f), Vector3::Up);
    }

    Matrix Projection()
    {
        return Matrix::CreatePerspectiveFieldOfView(1.0471975512f, 1.0f, kNear, kFar);
    }

    std::array<VertexPositionNormalTexture, 6> Wall()
    {
        const Vector3 facing(0.0f, 0.0f, 1.0f);
        const auto vertex = [&](const float x, const float y) {
            return VertexPositionNormalTexture(Vector3(x, y, kWallZ), facing, Vector2(0.0f, 0.0f));
        };
        return {vertex(-kHalf, -kHalf), vertex(kHalf, -kHalf), vertex(kHalf, kHalf),
                vertex(-kHalf, -kHalf), vertex(kHalf, kHalf),  vertex(-kHalf, kHalf)};
    }

    /// A deterministic spread of lights just in front of the wall, so every one of them matters.
    ClusteredLightSetEXT MakeLights(const int count)
    {
        ClusteredLightSetEXT lights;
        for (int i = 0; i < count; ++i)
        {
            const float t = static_cast<float>(i);
            ClusteredLightEXT light;
            light.Type = ClusteredLightType::Point;
            light.Position = Vector3(std::sin(t * 0.9f) * 9.0f, std::cos(t * 0.7f) * 9.0f,
                                     kWallZ + 1.5f + std::sin(t * 0.31f));
            light.Range = 4.0f;
            light.Intensity = 6.0f;
            light.Color = Vector3(1.0f, 0.9f, 0.8f);
            lights.add(light);
        }
        return lights;
    }

    double MillisecondsOf(const int repeats, const std::function<void()>& work)
    {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < repeats; ++i) work();
        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count() /
               static_cast<double>(repeats);
    }
}

class ClusteredLightsExample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool benchmark_ = false;
    int  passCount_ = 0;
    int  checkCount_ = 0;
    int  result_ = 1;

    void check(const bool ok, const std::string& label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    static void DrawWall(GraphicsDevice& device, ClusteredForwardEffect& effect,
                         const ClusteredLightBuffer& buffer)
    {
        device.Clear(Color::Black);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setBlendStateProperty(BlendState::Opaque);
        device.SetVertexBuffer(nullptr);

        effect.begin(Matrix::getIdentityProperty(), View(), Projection(), Vector3::Zero, buffer);
        effect.getEffect()->Apply();
        const auto wall = Wall();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, wall.data(), 0, 2);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        ClusteredForwardEffect effect(device);
        if (!effect.isSupported())
        {
            std::printf("SKIP: this renderer does not execute shader-effect source (a documented "
                        "capability boundary, not a defect)\n");
            std::exit(77);
        }
        check(true, "the renderer runs the clustered forward effect");

        ClusteredLightGrid grid;
        grid.setProjection(Projection(), kNear, kFar);

        // A stride wide enough for this scene: the lights are piled close to the wall on
        // purpose, and a cluster there holds far more than the default capacity.
        ClusteredLightCompute compute(device, 256);
        const ClusteredLightSetEXT lights = MakeLights(256);
        const std::vector<BoundingSphere> bounds = lights.collectBounds();

        ClusteredLightAssignment onCpu;
        onCpu.assign(grid, View(), bounds);

        if (compute.isSupported())
        {
            ClusteredLightAssignment onGpu;
            compute.assign(grid, View(), bounds, onGpu);
            bool identical = onGpu.getTotalReferenceCount() == onCpu.getTotalReferenceCount() &&
                             !compute.hasOverflowed();
            for (int cluster = 0; identical && cluster < onCpu.getClusterCount(); ++cluster)
            {
                const auto a = onCpu.lightsInCluster(cluster);
                const auto b = onGpu.lightsInCluster(cluster);
                identical = a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
            }
            check(identical, "the GPU assignment matches the CPU one exactly");
        }
        else
        {
            std::printf("[NOTE] no compute here (%s); the CPU path is the only one measured\n",
                        compute.getUnsupportedReason().c_str());
        }

        ClusteredLightBuffer buffer(device);
        buffer.upload(lights, grid, onCpu);

        effect.setBaseColor(Vector3(0.8f, 0.8f, 0.8f));
        effect.setRoughness(0.45f);
        DrawWall(device, effect, buffer);

        std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::Black);
        device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));
        int lit = 0;
        for (const Color& p : pixels)
            if (p.getRProperty() > 8) ++lit;
        check(lit > kFrame * kFrame / 20, "256 lights reach the frame");

        std::printf("    %d lights, %d clusters, %d light references, at most %d per cluster\n",
                    lights.getCount(), grid.getClusterCount(), onCpu.getTotalReferenceCount(),
                    onCpu.getMaxLightsPerCluster());

        if (benchmark_)
        {
            std::printf("--- MOD-2048: clustered forward, %dx%d, Mesa llvmpipe ---\n", kFrame,
                        kFrame);
            std::printf("    lights |  sort CPU |  sort GPU |    upload |     shade"
                        "   (shade excludes the read-back that forces it to complete)\n");
            for (const int count : {1, 16, 64, 256})
            {
                const ClusteredLightSetEXT set = MakeLights(count);
                const std::vector<BoundingSphere> spheres = set.collectBounds();

                ClusteredLightAssignment cpuResult;
                const double cpuSort = MillisecondsOf(
                    8, [&] { cpuResult.assign(grid, View(), spheres); });

                double gpuSort = -1.0;
                if (compute.isSupported())
                {
                    ClusteredLightAssignment gpuResult;
                    gpuSort = MillisecondsOf(
                        8, [&] { compute.assign(grid, View(), spheres, gpuResult); });
                }

                ClusteredLightBuffer target(device);
                const double upload =
                    MillisecondsOf(8, [&] { target.upload(set, grid, cpuResult); });

                // A draw submitted is not a draw done: GL is asynchronous, so timing the call
                // alone measures submission and reports 0.06 ms for work that has not happened.
                // The frame is therefore read back inside the timed block to force completion, and
                // the read-back's own cost is measured separately and subtracted.
                std::vector<Color> frame(static_cast<std::size_t>(kFrame) * kFrame, Color::Black);
                // Warm-up, discarded. The first draw with a given light buffer pays for texture
                // uploads and pipeline state the rest do not, and without this the one-light row
                // came out an order of magnitude *slower* than the sixteen-light one.
                for (int warm = 0; warm < 3; ++warm)
                {
                    DrawWall(device, effect, target);
                    device.GetBackBufferData(frame.data(), static_cast<int>(frame.size()));
                }
                const double drawAndSync = MillisecondsOf(8, [&] {
                    DrawWall(device, effect, target);
                    device.GetBackBufferData(frame.data(), static_cast<int>(frame.size()));
                });
                const double readBack = MillisecondsOf(8, [&] {
                    device.GetBackBufferData(frame.data(), static_cast<int>(frame.size()));
                });
                const double shade = drawAndSync - readBack;

                if (gpuSort >= 0.0)
                    std::printf("    %6d | %8.3f  | %8.3f  | %8.3f  | %8.3f  ms\n", count, cpuSort,
                                gpuSort, upload, shade);
                else
                    std::printf("    %6d | %8.3f  |        -- | %8.3f  | %8.3f  ms\n", count,
                                cpuSort, upload, shade);
            }
        }

        if (benchmark_)
        {
            // plans/plan_modern.md MOD-2077: what each material lobe costs, measured rather than
            // asserted. 64 lights, so the light loop is the thing the lobes are being added to.
            std::printf("--- MOD-2077: material lobes at 64 lights, %dx%d, Mesa llvmpipe ---\n",
                        kFrame, kFrame);
            const ClusteredLightSetEXT lobeLights = MakeLights(64);
            ClusteredLightAssignment lobeAssignment;
            lobeAssignment.assign(grid, View(), lobeLights.collectBounds());
            ClusteredLightBuffer lobeBuffer(device);
            lobeBuffer.upload(lobeLights, grid, lobeAssignment);

            Texture2D opaque(device, 4, 4);
            const std::vector<Color> grey(16, Color(128, 128, 128, 255));
            opaque.SetData(grey.data(), 16);
            effect.setOpaqueFrame(&opaque);

            PbrMaterialExtensions clearcoat;
            clearcoat.setClearcoatFactor(1.0f);
            clearcoat.setClearcoatRoughness(0.3f);
            PbrMaterialExtensions sheen;
            sheen.setSheenColorFactor(Vector3(1.0f, 1.0f, 1.0f));
            sheen.setSheenRoughness(0.4f);
            PbrMaterialExtensions iridescence;
            iridescence.setIridescenceFactor(1.0f);
            PbrMaterialExtensions subsurface;
            subsurface.setSubsurfaceColor(Vector3(0.6f, 0.3f, 0.2f));
            PbrMaterialExtensions transmission;
            transmission.setTransmissionFactor(1.0f);
            transmission.setThicknessFactor(1.0f);
            transmission.setAttenuationDistance(2.0f);
            PbrMaterialExtensions everything = clearcoat;
            everything.setSheenColorFactor(Vector3(1.0f, 1.0f, 1.0f));
            everything.setSheenRoughness(0.4f);
            everything.setIridescenceFactor(1.0f);
            everything.setSubsurfaceColor(Vector3(0.6f, 0.3f, 0.2f));

            const std::pair<const char*, const PbrMaterialExtensions*> lobes[] = {
                {"none", nullptr},
                {"clearcoat", &clearcoat},
                {"sheen", &sheen},
                {"iridescence", &iridescence},
                {"subsurface", &subsurface},
                {"transmission", &transmission},
                {"all four", &everything},
            };

            std::vector<Color> frame(static_cast<std::size_t>(kFrame) * kFrame, Color::Black);
            for (const auto& [name, extensions] : lobes)
            {
                if (extensions != nullptr) effect.setMaterialExtensions(*extensions);
                else effect.setMaterialExtensions(PbrMaterialExtensions());

                for (int warm = 0; warm < 3; ++warm)
                {
                    DrawWall(device, effect, lobeBuffer);
                    device.GetBackBufferData(frame.data(), static_cast<int>(frame.size()));
                }
                const double withSync = MillisecondsOf(8, [&] {
                    DrawWall(device, effect, lobeBuffer);
                    device.GetBackBufferData(frame.data(), static_cast<int>(frame.size()));
                });
                const double readBack = MillisecondsOf(8, [&] {
                    device.GetBackBufferData(frame.data(), static_cast<int>(frame.size()));
                });
                std::printf("    %-13s %8.3f ms\n", name, withSync - readBack);
            }
            effect.setMaterialExtensions(PbrMaterialExtensions());
            effect.setOpaqueFrame(nullptr);
        }

        std::printf("%d/%d checks passed\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    explicit ClusteredLightsExample(const bool benchmark) : benchmark_(benchmark)
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

        ClusteredLightsExample example(benchmark);
        example.Run();
        return example.result();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
