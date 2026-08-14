// SPDX-License-Identifier: MS-PL
//
// plan_fx.md FX-051: one fuzz entry point for the whole compiled-effect surface -- construction,
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

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectAnnotation.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using Microsoft::Xna::Framework::Graphics::Effect;
    using Microsoft::Xna::Framework::Graphics::EffectParameter;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

    /// One compiled effect binary is at most 64 MiB by contract; anything larger is rejected by
    /// the constructor anyway, so a campaign should not waste time generating it.
    constexpr std::size_t kMaximumInputBytes = 64u * 1024u * 1024u;

    GraphicsDevice& SharedDevice()
    {
        static GraphicsDevice device;
        return device;
    }

    void WalkParameter(const EffectParameter& parameter, int depth)
    {
        if (depth > 8) return;
        (void) parameter.getNameProperty();
        (void) parameter.getSemanticProperty();
        (void) parameter.getParameterClassProperty();
        (void) parameter.getParameterTypeProperty();
        (void) parameter.getRowCountProperty();
        (void) parameter.getColumnCountProperty();

        const auto& annotations = parameter.getAnnotationsProperty();
        for (int i = 0; i < annotations.getCountProperty(); ++i)
            (void) annotations[i].getNameProperty();

        const auto& elements = parameter.getElementsProperty();
        for (int i = 0; i < elements.getCountProperty(); ++i)
            WalkParameter(elements[i], depth + 1);

        const auto& members = parameter.getStructureMembersProperty();
        for (int i = 0; i < members.getCountProperty(); ++i)
            WalkParameter(members[i], depth + 1);
    }

    void ExerciseEffect(Effect& effect)
    {
        auto& parameters = effect.getParametersProperty();
        for (int i = 0; i < parameters.getCountProperty(); ++i)
            WalkParameter(parameters[i], 0);

        auto& techniques = effect.getTechniquesProperty();
        for (int i = 0; i < techniques.getCountProperty(); ++i)
        {
            auto& technique = techniques[i];
            (void) technique.getNameProperty();
            effect.setCurrentTechniqueProperty(&technique);
            auto& passes = technique.getPassesProperty();
            for (int pass = 0; pass < passes.getCountProperty(); ++pass)
            {
                (void) passes[pass].getNameProperty();
                passes[pass].Apply();
            }
        }
    }
}

/**
 * @brief Runs one candidate compiled-effect binary through the full public surface.
 *
 * Every std::exception is an accepted outcome: rejecting malformed content is the contract. Only
 * a crash, a sanitizer report, a hang, or an unbounded allocation is a finding.
 */
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size == 0 || size > kMaximumInputBytes) return 0;

    GraphicsDevice& device = SharedDevice();
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects)) return 0;

    const std::vector<SharpRuntime::bytecs> bytes(data, data + size);
    try
    {
        auto effect = std::make_unique<Effect>(device, bytes);
        ExerciseEffect(*effect);

        std::unique_ptr<Effect> clone(effect->Clone());
        if (clone)
        {
            ExerciseEffect(*clone);
            // Destroying the source first is the ordering a use-after-free would surface in.
            effect.reset();
            ExerciseEffect(*clone);
        }
    }
    catch (const std::exception&)
    {
        // Expected for malformed, truncated, foreign-format and over-limit input.
    }
    return 0;
}

#ifndef CNA_FX_FUZZER_ENTRY_POINT
namespace
{
    std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    int ReplayOne(const std::filesystem::path& path)
    {
        const std::vector<std::uint8_t> bytes = ReadFile(path);
        if (bytes.empty())
        {
            std::cerr << "compiled_effect_fuzzer: cannot read " << path << "\n";
            return 1;
        }
        LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
        std::cout << "ok " << path.filename().string() << " (" << bytes.size() << " bytes)\n";
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: compiled_effect_fuzzer <corpus-file-or-directory>...\n"
                  << "Replays each input through the libFuzzer entry point. Build with\n"
                  << "-DCNA_FX_FUZZER_ENTRY_POINT=ON under clang for a real campaign.\n";
        return 2;
    }

    int failures = 0;
    for (int i = 1; i < argc; ++i)
    {
        const std::filesystem::path path(argv[i]);
        std::error_code error;
        if (std::filesystem::is_directory(path, error))
        {
            for (const auto& entry : std::filesystem::directory_iterator(path, error))
                if (entry.is_regular_file(error)) failures += ReplayOne(entry.path());
        }
        else
        {
            failures += ReplayOne(path);
        }
    }
    if (failures != 0) std::cerr << failures << " input(s) could not be read\n";
    return failures == 0 ? 0 : 1;
}
#endif
