// SPDX-License-Identifier: MS-PL
//
// FX-051: one fuzz entry point for the whole compiled-effect surface -- construction,
// reflection walking, parameter reads, clone, technique/pass selection, apply and disposal. A
// compiled Effect Framework binary is untrusted input handed to a native parser, so every one of
// those stages has to survive arbitrary bytes, not just the constructor.
//
// The same translation unit builds in two shapes:
//
//   * libFuzzer/AFL++ (`-DCNA_FX_FUZZER_ENTRY_POINT=ON`, clang): exports
//     LLVMFuzzerTestOneInput and lets the driver own main(). Build the whole project with
//     `-fsanitize=fuzzer-no-link,address,undefined` and link this target with `-fsanitize=fuzzer`.
//     AFL++ picks the same entry point up through afl-clang-lto's libFuzzer compatibility mode.
//
//   * standalone replay (default, any compiler): a main() that replays every file named on the
//     command line, or every file in a directory, through the identical entry point. This is what
//     makes the harness verifiable in an ordinary build and is how a crashing input found by a
//     campaign is reproduced.
//
// A GraphicsDevice is created once and reused, because device creation dwarfs the cost of the
// code under test and a per-input device would make a campaign useless.

#include "compiled_effect_fuzzer_support.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectAnnotation.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "System/Security/Cryptography/SHA256.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using namespace CNA::Tools::FxFuzz;

    struct Statistics
    {
        std::uint64_t candidates = 0, skipped = 0, constructed = 0, clones = 0;
        std::uint64_t operations = 0, rejections = 0, parameters = 0, annotations = 0;
        std::uint64_t applies = 0, draws = 0, readbacks = 0;
    } stats;

    template<class Action>
    bool Probe(Action&& action)
    {
        ++stats.operations;
        const bool accepted = Attempt(std::forward<Action>(action));
        if (!accepted) { ++stats.rejections; }
        return accepted;
    }

    GraphicsDevice& SharedDevice()
    {
        static GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(),
            GraphicsProfile::HiDef, PresentationParameters::CreateWindowedEXT(16, 16));
        if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        {
            throw std::runtime_error("CompiledEffects unavailable on renderer " +
                                     std::string(device.GetGraphicsRendererName()));
        }
        return device;
    }

    struct Budget
    {
        int nodes = 256, annotations = 256, techniques = 32, passes = 64, draws = 1;
    };

#define FX_PROBE(expression) Probe([&] { (void)(expression); })

    void WalkAnnotations(const EffectAnnotationCollection& annotations, Budget& budget)
    {
        for (int index = 0; index < annotations.getCountProperty() && budget.annotations > 0; ++index)
        {
            const auto* value = annotations[index];
            --budget.annotations;
            if (value == nullptr) { continue; }
            ++stats.annotations;
            FX_PROBE(value->getNameProperty());
            FX_PROBE(value->getSemanticProperty());
            FX_PROBE(value->getParameterClassProperty());
            FX_PROBE(value->getParameterTypeProperty());
            FX_PROBE(value->getRowCountProperty());
            FX_PROBE(value->getColumnCountProperty());
            FX_PROBE(value->GetValueBoolean());
            FX_PROBE(value->GetValueInt32());
            FX_PROBE(value->GetValueSingle());
            FX_PROBE(value->GetValueString());
            FX_PROBE(value->GetValueVector2());
            FX_PROBE(value->GetValueVector3());
            FX_PROBE(value->GetValueVector4());
            FX_PROBE(value->GetValueMatrix());
        }
    }

    void WalkParameter(EffectParameter& value, int depth, Budget& budget)
    {
        if (depth > 8 || budget.nodes <= 0) { return; }
        --budget.nodes;
        ++stats.parameters;
        FX_PROBE(value.getNameProperty());
        FX_PROBE(value.getSemanticProperty());
        FX_PROBE(value.getParameterClassProperty());
        FX_PROBE(value.getParameterTypeProperty());
        FX_PROBE(value.getRowCountProperty());
        FX_PROBE(value.getColumnCountProperty());
        FX_PROBE(value.GetValueBoolean());
        FX_PROBE(value.GetValueInt32());
        FX_PROBE(value.GetValueSingle());
        FX_PROBE(value.GetValueString());
        FX_PROBE(value.GetValueMatrix());
        FX_PROBE(value.GetValueMatrixTranspose());
        FX_PROBE(value.GetValueQuaternion());
        FX_PROBE(value.GetValueVector2());
        FX_PROBE(value.GetValueVector3());
        FX_PROBE(value.GetValueVector4());
        FX_PROBE(value.GetValueTexture2D());
        FX_PROBE(value.GetValueTexture3D());
        FX_PROBE(value.GetValueTextureCube());
        for (const int count : {0, 1, 4})
        {
            FX_PROBE(value.GetValueBooleanArray(count));
            FX_PROBE(value.GetValueInt32Array(count));
            FX_PROBE(value.GetValueSingleArray(count));
            FX_PROBE(value.GetValueMatrixArray(count));
            FX_PROBE(value.GetValueMatrixTransposeArray(count));
            FX_PROBE(value.GetValueQuaternionArray(count));
            FX_PROBE(value.GetValueVector2Array(count));
            FX_PROBE(value.GetValueVector3Array(count));
            FX_PROBE(value.GetValueVector4Array(count));
        }
        FX_PROBE(value.SetValue(false));
        FX_PROBE(value.SetValue(std::vector<bool>{false, true}));
        FX_PROBE(value.SetValue(0));
        FX_PROBE(value.SetValue(std::vector<int>{0, 1}));
        FX_PROBE(value.SetValue(0.25f));
        FX_PROBE(value.SetValue(std::vector<float>{0.f, 1.f}));
        FX_PROBE(value.SetValue(std::string("fuzz")));
        const Matrix identity = Matrix::getIdentityProperty();
        FX_PROBE(value.SetValue(identity));
        FX_PROBE(value.SetValue(std::vector<Matrix>{identity}));
        FX_PROBE(value.SetValueTranspose(identity));
        FX_PROBE(value.SetValueTranspose(std::vector<Matrix>{identity}));
        FX_PROBE(value.SetValue(Quaternion::Identity));
        FX_PROBE(value.SetValue(std::vector<Quaternion>{Quaternion::Identity}));
        FX_PROBE(value.SetValue(Vector2(0.f, 1.f)));
        FX_PROBE(value.SetValue(std::vector<Vector2>{Vector2(0.f, 1.f)}));
        FX_PROBE(value.SetValue(Vector3(0.f, 1.f, 0.f)));
        FX_PROBE(value.SetValue(std::vector<Vector3>{Vector3(0.f, 1.f, 0.f)}));
        FX_PROBE(value.SetValue(Vector4(0.f, 1.f, 0.f, 1.f)));
        FX_PROBE(value.SetValue(std::vector<Vector4>{Vector4(0.f, 1.f, 0.f, 1.f)}));
        FX_PROBE(value.SetValue(static_cast<Texture*>(nullptr)));
        FX_PROBE(value.SetValue(static_cast<Texture2D*>(nullptr)));
        FX_PROBE(value.SetValue(static_cast<Texture3D*>(nullptr)));
        FX_PROBE(value.SetValue(static_cast<TextureCube*>(nullptr)));
        WalkAnnotations(value.getAnnotationsProperty(), budget);
        for (auto* collection : {&value.getElementsProperty(), &value.getStructureMembersProperty()})
        {
            for (int index = 0; index < collection->getCountProperty() && budget.nodes > 0; ++index)
            {
                if (auto* member = (*collection)[index]) { WalkParameter(*member, depth + 1, budget); }
            }
        }
    }

    void DrawAndRead(GraphicsDevice& device)
    {
        const std::array<VertexPositionColorTexture, 3> vertices{{
            {Vector3(-0.5f, -0.5f, 0.f), Color::White, Vector2(0.f, 0.f)},
            {Vector3(0.f, 0.5f, 0.f), Color::White, Vector2(0.5f, 1.f)},
            {Vector3(0.5f, -0.5f, 0.f), Color::White, Vector2(1.f, 0.f)}}};
        if (Probe([&] { device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 1); }))
        {
            ++stats.draws;
            const Rectangle pixel(0, 0, 1, 1);
            Color color;
            if (Probe([&] { device.GetBackBufferData(&pixel, &color, 0, 1); })) { ++stats.readbacks; }
        }
    }

    void ExerciseEffect(Effect& effect, GraphicsDevice& device)
    {
        Budget budget;
        auto& parameters = effect.getParametersProperty();
        for (int index = 0; index < parameters.getCountProperty() && budget.nodes > 0; ++index)
        {
            if (auto* parameter = parameters[index])
            {
                Probe([&] { WalkParameter(*parameter, 0, budget); });
            }
        }
        auto& techniques = effect.getTechniquesProperty();
        for (int index = 0; index < techniques.getCountProperty() && budget.techniques > 0; ++index)
        {
            --budget.techniques;
            auto* technique = techniques[index];
            if (technique == nullptr) { continue; }
            FX_PROBE(technique->getNameProperty());
            WalkAnnotations(technique->getAnnotationsProperty(), budget);
            if (!FX_PROBE(effect.setCurrentTechniqueProperty(technique))) { continue; }
            auto& passes = technique->getPassesProperty();
            for (int pass = 0; pass < passes.getCountProperty() && budget.passes > 0; ++pass)
            {
                --budget.passes;
                if (auto* value = passes[pass])
                {
                    FX_PROBE(value->getNameProperty());
                    WalkAnnotations(value->getAnnotationsProperty(), budget);
                    if (FX_PROBE(value->Apply()))
                    {
                        ++stats.applies;
                        if (budget.draws > 0) { --budget.draws; DrawAndRead(device); }
                    }
                }
            }
        }
    }
#undef FX_PROBE
}

/** @brief Exercises bounded public operations; allocation failure escapes as a finding. */
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    auto& device = SharedDevice();
    if (size == 0 || size > MaximumInputBytes) { ++stats.skipped; return 0; }
    ++stats.candidates;
    const std::vector<std::uint8_t> bytes(data, data + size);
    std::unique_ptr<Effect> effect;
    if (!Probe([&] { effect = std::make_unique<Effect>(device, bytes); })) { return 0; }
    ++stats.constructed;
    Probe([&] { ExerciseEffect(*effect, device); });
    std::unique_ptr<Effect> clone;
    if (Probe([&] { clone.reset(effect->Clone()); }) && clone)
    {
        ++stats.clones;
        Probe([&] { ExerciseEffect(*clone, device); });
        effect.reset();
        Probe([&] { ExerciseEffect(*clone, device); });
    }
    return 0;
}

#ifndef CNA_FX_FUZZER_ENTRY_POINT
namespace
{
    std::string Digest(const std::vector<std::uint8_t>& bytes)
    {
        System::Security::Cryptography::SHA256 hash;
        std::ostringstream text;
        for (const auto byte : hash.ComputeHash(bytes))
        {
            text << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned>(byte);
        }
        return text.str();
    }

    void PrintSummary()
    {
        std::cout << "fx_fuzz_summary {\"renderer\":" << std::quoted(std::string(SharedDevice().GetGraphicsRendererName()))
            << ",\"candidates\":" << stats.candidates
            << ",\"skipped\":" << stats.skipped << ",\"constructed\":" << stats.constructed
            << ",\"clones\":" << stats.clones << ",\"operations\":" << stats.operations
            << ",\"rejections\":" << stats.rejections << ",\"parameters\":" << stats.parameters
            << ",\"annotations\":" << stats.annotations << ",\"applies\":" << stats.applies
            << ",\"draws\":" << stats.draws << ",\"readbacks\":" << stats.readbacks << "}\n";
        std::cout.flush();
        if (!std::cout) { throw std::runtime_error("cannot write replay results"); }
    }

    void PrintSeed(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::cout << "seed " << Digest(bytes) << ' ' << bytes.size() << ' ' << path << std::endl;
        if (!std::cout) { throw std::runtime_error("cannot write seed inventory"); }
    }
}

int main(int argc, char** argv)
{
    try
    {
        const std::string mode = argc > 1 ? argv[1] : "";
        if (argc < 2 || mode == "--help")
        {
            std::cout << "usage: compiled_effect_fuzzer <file-or-directory>...\n"
                         "       compiled_effect_fuzzer --campaign <corpus> <positive-iterations> [seed]\n";
            return argc < 2 ? 2 : 0;
        }
        if (mode == "--campaign")
        {
            if (argc != 4 && argc != 5) { throw std::invalid_argument("--campaign requires corpus, iterations and optional seed"); }
            const auto iterations = ParseUnsigned(argv[3]);
            if (iterations == 0) { throw std::invalid_argument("iterations must be positive"); }
            Rng random{argc == 5 ? ParseUnsigned(argv[4], true) : 0x4658465556555AULL};
            const auto files = EnumerateInputs({argv[2]});
            std::vector<std::vector<std::uint8_t>> corpus;
            std::size_t remaining = MaximumCorpusBytes;
            for (const auto& path : files)
            {
                auto bytes = ReadFile(path, remaining);
                remaining -= bytes.size();
                PrintSeed(path, bytes);
                corpus.push_back(std::move(bytes));
            }
            std::cout << "campaign-v2 " << files.size() << " seeds, " << iterations
                      << " iterations, initial seed 0x" << std::hex << random.state << std::dec << std::endl;
            for (std::uint64_t iteration = 0; iteration < iterations; ++iteration)
            {
                auto bytes = corpus[random.Below(static_cast<std::uint32_t>(corpus.size()))];
                Mutate(random, bytes);
                if (iteration % 100 == 0)
                {
                    std::cout << "iteration " << iteration << " rng 0x" << std::hex << random.state
                              << std::dec << " sha256 " << Digest(bytes) << std::endl;
                    if (!std::cout) { throw std::runtime_error("cannot write campaign progress"); }
                }
                LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
            }
        }
        else
        {
            std::vector<std::filesystem::path> paths(argv + 1, argv + argc);
            for (const auto& path : EnumerateInputs(paths))
            {
                const auto bytes = ReadFile(path);
                PrintSeed(path, bytes);
                LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
            }
        }
        PrintSummary();
        return 0;
    }
    catch (const std::invalid_argument& error)
    {
        std::cerr << "compiled_effect_fuzzer: " << error.what() << '\n';
        return 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "compiled_effect_fuzzer: fatal: " << error.what() << '\n';
        return 1;
    }
}
#endif

