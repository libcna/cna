// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2108: what transparency costs, and what each path gets wrong.
//
// The unit tests assert that each path does what it says. What they cannot say is which one to
// reach for, and that is a cost question with a correctness question attached: sorting is exact for
// surfaces that do not interpenetrate and has no answer for surfaces that do, while weighted
// blending is approximate everywhere and never wrong about order. So this measures both against
// each other at several surface counts, and measures the one thing the approximation is worst at.
//
// Check A -- the renderer runs the order-independent route, or SKIP.
// Check B -- submitting the same surfaces in either order gives the same frame.
// Check C -- the sorted list really orders back to front.
//
// `--benchmark` reports the costs (recorded in `docs/cnaext-perf.md`).
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/TransparentDrawList.hpp"
#include "CNA/Graphics/WeightedBlendedTransparency.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::TransparentDrawList;
using CNA::Graphics::WeightedBlendedTransparency;

namespace
{
    constexpr int   kFrame = 256;
    constexpr float kFar   = 100.0f;

    constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
uniform vec2 uOffset;
uniform float uScale;
void main() { gl_Position = vec4(aPos.xy * uScale + uOffset, aPos.z, 1.0); }
)";

    constexpr const char* kBlendFragment = R"(#version 300 es
precision highp float;
out vec4 FragColor;
uniform vec4 uColour;
void main() { FragColor = uColour; }
)";

    std::array<VertexPositionColor, 6> Quad()
    {
        const auto at = [](const float x, const float y) {
            return VertexPositionColor(Vector3(x, y, 0.0f), Color::White);
        };
        return {at(-1.0f, -1.0f), at(-1.0f, 1.0f), at(1.0f, 1.0f),
                at(-1.0f, -1.0f), at(1.0f, 1.0f),  at(1.0f, -1.0f)};
    }

    double MillisecondsOf(const int repeats, const std::function<void()>& work)
    {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < repeats; ++i) work();
        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count() /
               static_cast<double>(repeats);
    }

    /// A deterministic spread of overlapping surfaces, each at its own depth.
    struct Surface { float X, Y, Depth, R, G, B; };

    std::vector<Surface> Surfaces(const int count)
    {
        std::vector<Surface> surfaces;
        surfaces.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            const float t = static_cast<float>(i);
            surfaces.push_back(Surface{std::sin(t * 0.7f) * 0.4f, std::cos(t * 0.53f) * 0.4f,
                                       2.0f + std::fmod(t * 3.7f, 60.0f),
                                       std::fmod(t * 0.31f, 1.0f), std::fmod(t * 0.17f, 1.0f),
                                       std::fmod(t * 0.23f, 1.0f)});
        }
        return surfaces;
    }
}

class TransparencyExample : public Game
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

        WeightedBlendedTransparency oit(device, kFrame, kFrame);
        if (!oit.isSupported())
        {
            std::printf("SKIP: %s\n", oit.getUnsupportedReason().c_str());
            std::exit(77);
        }
        check(true, "the renderer runs the order-independent route");

        std::string accumulate = "#version 300 es\nprecision highp float;\n";
        accumulate += WeightedBlendedTransparency::getAccumulationGlsl();
        accumulate += R"(
uniform vec4 uColour;
uniform float uDepth;
void main() { cnaOitEmit(uColour.rgb, uColour.a, uDepth); }
)";
        ShaderEffect emitter(device, kVertexSource, accumulate);
        ShaderEffect blender(device, kVertexSource, kBlendFragment);
        if (!emitter.IsEffectValid() || !blender.IsEffectValid())
        {
            std::printf("SKIP: the transparency shaders did not compile\n");
            std::exit(77);
        }

        const auto quad = Quad();
        const auto drawOne = [&](ShaderEffect& effect, const Surface& surface, const bool oitPath) {
            effect.Apply();
            effect.SetUniformVec2("uOffset", surface.X, surface.Y);
            effect.SetUniformFloat("uScale", 0.5f);
            effect.SetUniformVec4("uColour", surface.R, surface.G, surface.B, 0.4f);
            if (oitPath)
            {
                effect.SetUniformFloat("uDepth", surface.Depth);
                effect.SetUniformFloat("uCnaOitFarPlane", kFar);
            }
            device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
        };

        const auto accumulateAll = [&](const std::vector<Surface>& surfaces, const bool reversed) {
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.SetVertexBuffer(nullptr);
            oit.begin(kFar);
            if (reversed)
                for (auto it = surfaces.rbegin(); it != surfaces.rend(); ++it)
                    drawOne(emitter, *it, true);
            else
                for (const Surface& surface : surfaces) drawOne(emitter, surface, true);
            oit.end();
            device.Clear(Color::Black);
            oit.resolve(kFrame, kFrame);
        };

        std::vector<Color> forward(static_cast<std::size_t>(kFrame) * kFrame, Color(0, 0, 0, 0));
        std::vector<Color> backward = forward;
        const std::vector<Surface> eight = Surfaces(8);

        accumulateAll(eight, false);
        device.GetBackBufferData(forward.data(), static_cast<int>(forward.size()));
        accumulateAll(eight, true);
        device.GetBackBufferData(backward.data(), static_cast<int>(backward.size()));

        int worst = 0;
        int lit = 0;
        for (std::size_t i = 0; i < forward.size(); ++i)
        {
            worst = std::max(worst, std::abs(forward[i].getRProperty() - backward[i].getRProperty()));
            if (forward[i].getRProperty() > 8) ++lit;
        }
        std::printf("    eight surfaces, either order: worst channel difference %d, %d lit pixels\n",
                    worst, lit);
        check(worst <= 1 && lit > kFrame * kFrame / 16,
              "submitting the same surfaces in either order gives the same frame");

        // Check C: the sorted list is what orders the other path, so it is checked here too.
        TransparentDrawList list;
        std::vector<int> order;
        for (int i = 0; i < 4; ++i)
        {
            const Vector3 at(0.0f, 0.0f, -static_cast<float>(i) * 4.0f);
            list.submit(BoundingBox(at - Vector3(1.0f, 1.0f, 1.0f), at + Vector3(1.0f, 1.0f, 1.0f)),
                        [i, &order] { order.push_back(i); });
        }
        list.drawSorted(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 10.0f), Vector3::Zero, Vector3::Up));
        check(order == std::vector<int>({3, 2, 1, 0}),
              "the sorted list draws the furthest surface first");

        // How approximate is the approximation? The sorted path is exact for surfaces that do not
        // interpenetrate, and these do not, so it is a reference the other can be measured against.
        {
            std::vector<Color> sorted(static_cast<std::size_t>(kFrame) * kFrame, Color(0, 0, 0, 0));
            device.Clear(Color::Black);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::NonPremultiplied);
            device.SetVertexBuffer(nullptr);
            TransparentDrawList list;
            for (const Surface& surface : eight)
            {
                const Vector3 at(surface.X, surface.Y, -surface.Depth);
                list.submit(BoundingBox(at - Vector3(0.5f, 0.5f, 0.1f),
                                        at + Vector3(0.5f, 0.5f, 0.1f)),
                            [&] { drawOne(blender, surface, false); });
            }
            list.drawSorted(Matrix::CreateLookAt(Vector3::Zero, Vector3(0.0f, 0.0f, -1.0f),
                                                 Vector3::Up));
            device.setBlendStateProperty(BlendState::Opaque);
            device.GetBackBufferData(sorted.data(), static_cast<int>(sorted.size()));

            long total = 0;
            int worstAgainstSorted = 0;
            int counted = 0;
            for (std::size_t i = 0; i < sorted.size(); ++i)
            {
                if (sorted[i].getRProperty() == 0 && forward[i].getRProperty() == 0) continue;
                const int difference =
                    std::abs(sorted[i].getRProperty() - forward[i].getRProperty());
                worstAgainstSorted = std::max(worstAgainstSorted, difference);
                total += difference;
                ++counted;
            }
            std::printf("    approximation against the exact sorted frame: mean %.1f, worst %d "
                        "(of 255, over %d covered pixels)\n",
                        counted > 0 ? static_cast<double>(total) / counted : 0.0,
                        worstAgainstSorted, counted);
        }

        if (benchmark_) RunBenchmark(device, emitter, blender, oit, drawOne);

        std::printf("%d/%d checks passed\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

    void RunBenchmark(GraphicsDevice& device, ShaderEffect& emitter, ShaderEffect& blender,
                      WeightedBlendedTransparency& oit,
                      const std::function<void(ShaderEffect&, const Surface&, bool)>& drawOne)
    {
        std::printf("\n--- benchmark (%dx%d, each surface covers a quarter of the frame) ---\n",
                    kFrame, kFrame);
        std::vector<Color> frame(static_cast<std::size_t>(kFrame) * kFrame, Color(0, 0, 0, 0));

        const auto timed = [&](const char* name, const std::function<void()>& work) {
            for (int i = 0; i < 3; ++i)
            {
                work();
                device.GetBackBufferData(frame.data(), static_cast<int>(frame.size()));
            }
            const double withSync = MillisecondsOf(8, [&] {
                work();
                device.GetBackBufferData(frame.data(), static_cast<int>(frame.size()));
            });
            const double readBack = MillisecondsOf(8, [&] {
                device.GetBackBufferData(frame.data(), static_cast<int>(frame.size()));
            });
            std::printf("    %-46s %8.3f ms\n", name, withSync - readBack);
        };

        for (const int count : {8, 64})
        {
            const std::vector<Surface> surfaces = Surfaces(count);
            char label[96];

            // The sorted path pays for the sort and then draws once per surface. The sort is on the
            // CPU and the draw is on the GPU, and at these counts the sort is not the cost.
            std::snprintf(label, sizeof(label), "%d surfaces, sorted alpha blending", count);
            timed(label, [&] {
                device.Clear(Color::Black);
                device.setRasterizerStateProperty(RasterizerState::CullNone);
                device.setDepthStencilStateProperty(DepthStencilState::None);
                device.setBlendStateProperty(BlendState::NonPremultiplied);
                device.SetVertexBuffer(nullptr);
                TransparentDrawList list;
                for (const Surface& surface : surfaces)
                {
                    const Vector3 at(surface.X, surface.Y, -surface.Depth);
                    list.submit(BoundingBox(at - Vector3(0.5f, 0.5f, 0.1f),
                                            at + Vector3(0.5f, 0.5f, 0.1f)),
                                [&] { drawOne(blender, surface, false); });
                }
                list.drawSorted(Matrix::CreateLookAt(Vector3::Zero, Vector3(0.0f, 0.0f, -1.0f),
                                                     Vector3::Up));
                device.setBlendStateProperty(BlendState::Opaque);
            });

            std::snprintf(label, sizeof(label), "%d surfaces, order-independent", count);
            timed(label, [&] {
                device.setRasterizerStateProperty(RasterizerState::CullNone);
                device.SetVertexBuffer(nullptr);
                oit.begin(kFar);
                for (const Surface& surface : surfaces) drawOne(emitter, surface, true);
                oit.end();
                device.Clear(Color::Black);
                oit.resolve(kFrame, kFrame);
            });
        }

        // The fixed half of the order-independent cost: two full-screen clears and one full-screen
        // resolve, paid whether one surface was drawn or a thousand.
        timed("the order-independent overhead alone (no surfaces)", [&] {
            oit.begin(kFar);
            oit.end();
            device.Clear(Color::Black);
            oit.resolve(kFrame, kFrame);
        });
    }

public:
    explicit TransparencyExample(const bool benchmark) : benchmark_(benchmark)
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

        TransparencyExample example(benchmark);
        example.Run();
        return example.result();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
