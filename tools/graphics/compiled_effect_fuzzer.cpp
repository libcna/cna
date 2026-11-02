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
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectAnnotation.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
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
        std::uint64_t stateResets = 0, textureBindings = 0, targetBindings = 0;
        std::uint64_t techniqueSelections = 0;
    } stats;

    std::vector<std::uint32_t> candidatePixels;

    struct DeviceUnavailable : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

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
        static auto ownedDevice = [] {
            try
            {
                return std::make_unique<GraphicsDevice>(GraphicsAdapter::getDefaultAdapterProperty(),
                    GraphicsProfile::HiDef, PresentationParameters::CreateWindowedEXT(16, 16));
            }
            catch (const std::bad_alloc&) { throw; }
            catch (const std::exception& error) { throw DeviceUnavailable(error.what()); }
        }();
        auto& device = *ownedDevice;
        if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        {
            throw DeviceUnavailable("CompiledEffects unavailable on renderer " +
                                     std::string(device.GetGraphicsRendererName()));
        }
        return device;
    }

    struct Resources
    {
        Texture2D first, second, resetTexture;
        RenderTarget2D firstTarget, secondTarget;
        BasicEffect baseline;

        explicit Resources(GraphicsDevice& device)
            : first(device, 2, 2), second(device, 2, 2), resetTexture(device, 2, 2),
              firstTarget(device, 16, 16), secondTarget(device, 16, 16), baseline(device)
        {
            const std::array<Color, 4> red{Color::Red, Color::Red, Color::Red, Color::Red};
            const std::array<Color, 4> green{Color::Lime, Color::Lime, Color::Lime, Color::Lime};
            first.SetData(red.data(), 4);
            second.SetData(green.data(), 4);
            const std::array<Color, 4> edge{Color::Red, Color::White, Color::Red, Color::White};
            resetTexture.SetData(edge.data(), 4);
            baseline.setVertexColorEnabledProperty(true);
            baseline.setTextureEnabledProperty(true);
            baseline.setTextureProperty(&resetTexture);
        }
    };

    Resources& SharedResources()
    {
        static Resources resources(SharedDevice());
        return resources;
    }

    void ResetState(GraphicsDevice& device)
    {
        auto& resources = SharedResources();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        // A state-only or pixel-only pass must inherit a known shader, never a prior candidate's.
        resources.baseline.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply();
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setBlendFactorProperty(Color::White);
        device.setMultiSampleMaskProperty(-1);
        device.setReferenceStencilProperty(0);
        for (const bool vertex : {false, true})
        {
            auto& samplers = vertex ? device.getVertexSamplerStatesProperty() : device.getSamplerStatesProperty();
            auto& textures = vertex ? device.getVertexTexturesProperty() : device.getTexturesProperty();
            for (int slot = 0; slot < samplers.ActiveSamplerCount(); ++slot)
            {
                samplers(slot, SamplerState::PointClamp);
                textures(slot, nullptr);
            }
        }
        device.getTexturesProperty()(0, &resources.resetTexture);
        device.setViewportProperty(Viewport(0, 0, 16, 16));
        device.setScissorRectangleProperty(Rectangle(0, 0, 16, 16));
        // This is a harness invariant, not a rejectable operation on untrusted bytes.
        device.Clear(Color::Black);
        const std::array<VertexPositionColorTexture, 3> vertices{{
            {Vector3(-1.f, -1.f, 0.f), Color::White, Vector2(1.75f, 0.5f)},
            {Vector3(-1.f, 3.f, 0.f), Color::White, Vector2(1.75f, 0.5f)},
            {Vector3(3.f, -1.f, 0.f), Color::White, Vector2(1.75f, 0.5f)}}};
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 1);
        const Rectangle pixel(8, 8, 1, 1);
        Color color;
        device.GetBackBufferData(&pixel, &color, 0, 1);
        if (color != Color::White) { throw std::runtime_error("candidate state reset pixel mismatch"); }
        ++stats.stateResets;
    }

    struct Budget
    {
        int nodes = 256, annotations = 256, techniques = 32, passes = 64;
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
        FX_PROBE(value.SetValue(Vector4(1.f, 1.f, 0.f, 1.f)));
        FX_PROBE(value.SetValue(std::vector<Vector4>{Vector4(1.f, 1.f, 0.f, 1.f)}));
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

    void DrawAndRead(GraphicsDevice& device, RenderTarget2D* target)
    {
        const std::array<VertexPositionColorTexture, 3> vertices{{
            {Vector3(-0.5f, -0.5f, 0.f), Color::White, Vector2(0.f, 0.f)},
            {Vector3(0.f, 0.5f, 0.f), Color::White, Vector2(0.5f, 1.f)},
            {Vector3(0.5f, -0.5f, 0.f), Color::White, Vector2(1.f, 0.f)}}};
        if (Probe([&] { device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 1); }))
        {
            ++stats.draws;
            const Rectangle pixel(8, 8, 1, 1);
            Color color;
            // Resolve/unbind before reading a target, including when drawing was refused.
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            if (Probe([&] {
                if (target) { target->GetData(0, &pixel, &color, 0, 1); }
                else { device.GetBackBufferData(&pixel, &color, 0, 1); }
            }))
            {
                ++stats.readbacks;
                candidatePixels.push_back(color.getPackedValueProperty());
            }
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    void BindTextures(EffectParameter& parameter, Texture2D& texture, int depth, int& remaining)
    {
        if (depth > 8 || remaining <= 0) { return; }
        --remaining;
        const auto type = parameter.getParameterTypeProperty();
        if (type == EffectParameterType::Texture || type == EffectParameterType::Texture2D)
        {
            if (Probe([&] { parameter.SetValue(&texture); })) { ++stats.textureBindings; }
        }
        for (auto* collection : {&parameter.getElementsProperty(), &parameter.getStructureMembersProperty()})
        {
            for (int index = 0; index < collection->getCountProperty() && remaining > 0; ++index)
            {
                if (auto* member = (*collection)[index]) { BindTextures(*member, texture, depth + 1, remaining); }
            }
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
        // Revisit the first technique after the forward walk to cover switching back as well.
        for (int index = 0; index <= techniques.getCountProperty() && budget.techniques > 0; ++index)
        {
            --budget.techniques;
            if (techniques.getCountProperty() == 0) { break; }
            auto* technique = techniques[index == techniques.getCountProperty() ? 0 : index];
            if (technique == nullptr) { continue; }
            FX_PROBE(technique->getNameProperty());
            WalkAnnotations(technique->getAnnotationsProperty(), budget);
            if (!FX_PROBE(effect.setCurrentTechniqueProperty(technique))) { continue; }
            ++stats.techniqueSelections;
            auto& passes = technique->getPassesProperty();
            for (int pass = 0; pass < passes.getCountProperty() && budget.passes > 0; ++pass)
            {
                --budget.passes;
                if (auto* value = passes[pass])
                {
                    FX_PROBE(value->getNameProperty());
                    WalkAnnotations(value->getAnnotationsProperty(), budget);
                    // Backbuffer -> target A -> target B, with non-null A/B/A texture rebinding.
                    // Each binding precedes Apply so the runtime must commit the new resource.
                    auto& resources = SharedResources();
                    for (int binding = 0; binding < 3; ++binding)
                    {
                        auto& texture = binding == 1 ? resources.second : resources.first;
                        int remaining = 256;
                        for (int parameter = 0; parameter < parameters.getCountProperty() && remaining > 0; ++parameter)
                        {
                            if (auto* item = parameters[parameter])
                            {
                                Probe([&] { BindTextures(*item, texture, 0, remaining); });
                            }
                        }
                        auto* target = binding == 0 ? nullptr :
                            (binding == 1 ? &resources.firstTarget : &resources.secondTarget);
                        device.SetRenderTarget(target);
                        ++stats.targetBindings;
                        device.Clear(Color::Black);
                        if (FX_PROBE(value->Apply()))
                        {
                            ++stats.applies;
                            DrawAndRead(device, target);
                        }
                        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
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
    candidatePixels.clear();
    auto& device = SharedDevice();
    if (size == 0 || size > MaximumInputBytes) { ++stats.skipped; return 0; }
    ++stats.candidates;
    ResetState(device);
    const std::vector<std::uint8_t> bytes(data, data + size);
    std::unique_ptr<Effect> effect;
    if (!Probe([&] { effect = std::make_unique<Effect>(device, bytes); })) { return 0; }
    ++stats.constructed;
    ExerciseEffect(*effect, device);
    std::unique_ptr<Effect> clone;
    if (Probe([&] { clone.reset(effect->Clone()); }) && clone)
    {
        ++stats.clones;
        ExerciseEffect(*clone, device);
        effect.reset();
        ExerciseEffect(*clone, device);
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

    void PrintStatistics(const char* prefix, const Statistics& before = {},
                         const std::string& digest = {})
    {
        const bool executed = stats.constructed > before.constructed && stats.clones > before.clones &&
            stats.applies > before.applies && stats.draws > before.draws && stats.readbacks > before.readbacks;
        std::cout << prefix << " {\"renderer\":" << std::quoted(std::string(SharedDevice().GetGraphicsRendererName()))
            << ",\"qualification\":\"" << (executed ? "execution_reached" : "no_execution") << '"'
            << ",\"candidates\":" << stats.candidates - before.candidates
            << ",\"skipped\":" << stats.skipped - before.skipped
            << ",\"constructed\":" << stats.constructed - before.constructed
            << ",\"clones\":" << stats.clones - before.clones
            << ",\"operations\":" << stats.operations - before.operations
            << ",\"rejections\":" << stats.rejections - before.rejections
            << ",\"parameters\":" << stats.parameters - before.parameters
            << ",\"annotations\":" << stats.annotations - before.annotations
            << ",\"applies\":" << stats.applies - before.applies
            << ",\"draws\":" << stats.draws - before.draws
            << ",\"readbacks\":" << stats.readbacks - before.readbacks
            << ",\"state_resets\":" << stats.stateResets - before.stateResets
            << ",\"texture_bindings\":" << stats.textureBindings - before.textureBindings
            << ",\"target_bindings\":" << stats.targetBindings - before.targetBindings
            << ",\"technique_selections\":" << stats.techniqueSelections - before.techniqueSelections;
        if (!digest.empty())
        {
            std::cout << ",\"sha256\":" << std::quoted(digest) << ",\"pixels\":[";
            for (std::size_t i = 0; i < candidatePixels.size(); ++i)
            {
                if (i != 0) { std::cout << ','; }
                std::cout << candidatePixels[i];
            }
            std::cout << ']';
        }
        std::cout << "}\n";
        std::cout.flush();
        if (!std::cout) { throw std::runtime_error("cannot write replay results"); }
    }

    void Replay(const std::vector<std::uint8_t>& bytes)
    {
        const auto before = stats;
        const auto digest = Digest(bytes);
        std::cout << "fx_fuzz_begin " << stats.candidates << " sha256 " << digest << std::endl;
        if (!std::cout) { throw std::runtime_error("cannot write candidate identity"); }
        LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
        PrintStatistics("fx_fuzz_candidate", before, digest);
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
                Replay(bytes);
            }
        }
        else
        {
            std::vector<std::filesystem::path> paths(argv + 1, argv + argc);
            std::size_t remaining = MaximumCorpusBytes;
            for (const auto& path : EnumerateInputs(paths))
            {
                const auto bytes = ReadFile(path, remaining);
                remaining -= bytes.size();
                PrintSeed(path, bytes);
                Replay(bytes);
            }
        }
        PrintStatistics("fx_fuzz_summary");
        return 0;
    }
    catch (const DeviceUnavailable& error)
    {
        std::cerr << "compiled_effect_fuzzer: not qualified: " << error.what() << '\n';
        return 1;
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

