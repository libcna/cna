// SPDX-License-Identifier: MS-PL
//
// plans/plan_fx.md FX-053: measures the compiled-effect operations a real game repeats every frame, so
// the decision to add (or not add) an immutable native-artifact cache rests on numbers rather than
// on intuition. Construction and clone are the candidates such a cache would target; dirty upload,
// clean apply and draw are what a cache must not make slower.
//
// Standalone, manually invoked, and deliberately not a ctest test: timings on a shared machine are
// not a pass/fail signal. Run it, record the numbers, and compare like for like.
//
//   cmake --build cmake-build-debug --target cna_compiled_effect_benchmark -j3
//   SDL_VIDEODRIVER=offscreen ./cna_compiled_effect_benchmark [effects-directory]

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using Clock = std::chrono::steady_clock;

    std::vector<SharpRuntime::bytecs> ReadFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    struct Measurement
    {
        double medianMicroseconds = 0.0;
        double meanMicroseconds = 0.0;
        int iterations = 0;
    };

    Measurement Measure(int iterations, const std::function<void()>& body)
    {
        std::vector<double> samples;
        samples.reserve(static_cast<std::size_t>(iterations));
        double total = 0.0;
        for (int i = 0; i < iterations; ++i)
        {
            const auto start = Clock::now();
            body();
            const auto elapsed = std::chrono::duration<double, std::micro>(
                Clock::now() - start).count();
            samples.push_back(elapsed);
            total += elapsed;
        }
        std::sort(samples.begin(), samples.end());
        Measurement result;
        result.iterations = iterations;
        result.meanMicroseconds = iterations > 0 ? total / iterations : 0.0;
        result.medianMicroseconds = samples.empty() ? 0.0
            : samples[samples.size() / 2];
        return result;
    }

    void Report(const std::string& name, const Measurement& measurement)
    {
        std::cout << std::left << std::setw(46) << name << std::right
                  << std::setw(8) << measurement.iterations << "  "
                  << std::fixed << std::setprecision(1) << std::setw(11)
                  << measurement.medianMicroseconds << "  "
                  << std::setw(11) << measurement.meanMicroseconds << "\n";
    }

    const VertexPositionColor* FullScreenQuad()
    {
        static const VertexPositionColor vertices[6] = {
            { Vector3(-1.0f, 1.0f, 0.5f), Color::White },
            { Vector3(-1.0f, -1.0f, 0.5f), Color::White },
            { Vector3(1.0f, -1.0f, 0.5f), Color::White },
            { Vector3(-1.0f, 1.0f, 0.5f), Color::White },
            { Vector3(1.0f, -1.0f, 0.5f), Color::White },
            { Vector3(1.0f, 1.0f, 0.5f), Color::White },
        };
        return vertices;
    }
}

int main(int argc, char** argv)
{
    const std::filesystem::path effects = argc > 1
        ? std::filesystem::path(argv[1])
        : std::filesystem::path("modules/renderers/fna3d/effects");

    const auto basicBytes = ReadFile(effects / "BasicEffect.fxb");
    const auto spriteBytes = ReadFile(effects / "SpriteEffect.fxb");
    const auto skinnedBytes = ReadFile(effects / "SkinnedEffect.fxb");
    if (basicBytes.empty() || spriteBytes.empty() || skinnedBytes.empty())
    {
        std::cerr << "compiled_effect_benchmark: cannot read the stock fixtures under "
                  << effects << "\n";
        return 2;
    }

    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
    {
        std::cerr << "compiled_effect_benchmark: the selected renderer does not execute "
                     "compiled effects\n";
        return 3;
    }
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);

    std::cout << "compiled effect benchmark (FX-053)\n"
              << std::left << std::setw(46) << "operation" << std::right
              << std::setw(8) << "iters" << "  " << std::setw(11) << "median us"
              << "  " << std::setw(11) << "mean us" << "\n"
              << std::string(80, '-') << "\n";

    Report("construct SpriteEffect.fxb (1 KiB)", Measure(200, [&] {
        Effect effect(device, spriteBytes);
        (void) effect.getParametersProperty().getCountProperty();
    }));
    Report("construct BasicEffect.fxb (28 KiB)", Measure(100, [&] {
        Effect effect(device, basicBytes);
        (void) effect.getParametersProperty().getCountProperty();
    }));
    Report("construct SkinnedEffect.fxb (54 KiB)", Measure(50, [&] {
        Effect effect(device, skinnedBytes);
        (void) effect.getParametersProperty().getCountProperty();
    }));

    Effect basic(device, basicBytes);
    basic.getParametersProperty()["WorldViewProj"]->SetValue(Matrix::getIdentityProperty());
    basic.getParametersProperty()["DiffuseColor"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
    basic.getParametersProperty()["ShaderIndex"]->SetValue(3);

    Report("clone BasicEffect", Measure(200, [&] {
        std::unique_ptr<Effect> clone(basic.Clone());
        (void) clone->getParametersProperty().getCountProperty();
    }));

    Report("apply pass, nothing dirty", Measure(2000, [&] {
        basic.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();
    }));

    float phase = 0.0f;
    Report("set one float4 + apply pass", Measure(2000, [&] {
        phase += 0.001f;
        basic.getParametersProperty()["DiffuseColor"]->SetValue(
            Vector4(phase, 1.0f, 1.0f, 1.0f));
        basic.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();
    }));

    Report("set matrix + float4 + int + apply pass", Measure(2000, [&] {
        phase += 0.001f;
        basic.getParametersProperty()["WorldViewProj"]->SetValue(
            Matrix::CreateRotationZ(phase));
        basic.getParametersProperty()["DiffuseColor"]->SetValue(
            Vector4(phase, 1.0f, 1.0f, 1.0f));
        basic.getParametersProperty()["ShaderIndex"]->SetValue(3);
        basic.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();
    }));

    device.Clear(Color::Black);
    Report("compiled effect: apply + draw 2 triangles", Measure(500, [&] {
        basic.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, FullScreenQuad(), 0, 2);
    }));

    BasicEffect stock(device);
    stock.VertexColorEnabled = true;
    stock.setWorldProperty(Matrix::getIdentityProperty());
    stock.setViewProperty(Matrix::getIdentityProperty());
    stock.setProjectionProperty(Matrix::getIdentityProperty());
    Report("stock BasicEffect: apply + draw 2 triangles", Measure(500, [&] {
        stock.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, FullScreenQuad(), 0, 2);
    }));

    std::cout << std::string(80, '-') << "\n"
              << "Timings are wall clock on a shared machine; compare runs, not absolutes.\n";
    return 0;
}
