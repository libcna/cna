// SPDX-License-Identifier: MS-PL
//
// plans/plan_fx.md FX-051: one fuzz entry point for the whole compiled-effect surface -- construction,
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

    std::vector<std::vector<std::uint8_t>> ReadCorpus(const std::filesystem::path& path,
                                                      int& unreadable)
    {
        std::vector<std::vector<std::uint8_t>> corpus;
        std::error_code error;
        const auto take = [&](const std::filesystem::path& file) {
            std::vector<std::uint8_t> bytes = ReadFile(file);
            if (bytes.empty())
            {
                std::cerr << "compiled_effect_fuzzer: cannot read " << file << "\n";
                ++unreadable;
                return;
            }
            corpus.push_back(std::move(bytes));
        };
        if (std::filesystem::is_directory(path, error))
        {
            for (const auto& entry : std::filesystem::directory_iterator(path, error))
                if (entry.is_regular_file(error)) take(entry.path());
        }
        else
        {
            take(path);
        }
        return corpus;
    }

    /// Same generator the in-build deterministic corpus uses, so a campaign finding reproduces
    /// from its printed seed alone.
    struct Rng
    {
        std::uint64_t state;

        std::uint32_t Next()
        {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            return static_cast<std::uint32_t>(state >> 33);
        }

        std::uint32_t Below(std::uint32_t bound) { return bound == 0 ? 0 : Next() % bound; }
    };

    void Mutate(Rng& random, std::vector<std::uint8_t>& bytes)
    {
        if (bytes.empty()) return;
        const std::uint32_t mutations = 1 + random.Below(6);
        for (std::uint32_t i = 0; i < mutations; ++i)
        {
            switch (random.Below(4))
            {
                case 0:
                    bytes[random.Below(static_cast<std::uint32_t>(bytes.size()))] ^=
                        static_cast<std::uint8_t>(1u << random.Below(8));
                    break;
                case 1:
                    bytes[random.Below(static_cast<std::uint32_t>(bytes.size()))] =
                        static_cast<std::uint8_t>(random.Below(256));
                    break;
                case 2:
                {
                    // Overwrite a whole little-endian word: offsets and counts live in these, so
                    // byte flips alone rarely reach the bounds checks that matter.
                    const std::uint32_t word =
                        random.Below(static_cast<std::uint32_t>(bytes.size() / 4));
                    const std::uint32_t value = random.Below(4) == 0
                        ? 0xFFFFFFFFu : random.Next();
                    for (std::size_t b = 0; b < 4; ++b)
                        bytes[word * 4 + b] = static_cast<std::uint8_t>(value >> (b * 8));
                    break;
                }
                default:
                    bytes.resize(1 + random.Below(static_cast<std::uint32_t>(bytes.size())));
                    break;
            }
        }
    }
}

int main(int argc, char** argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    if (argc < 2 || mode == "--help")
    {
        std::cerr
            << "usage:\n"
            << "  compiled_effect_fuzzer <corpus-file-or-directory>...\n"
            << "      Replays each input once through the libFuzzer entry point.\n"
            << "  compiled_effect_fuzzer --campaign <corpus-dir> <iterations> [seed]\n"
            << "      Runs a deterministic mutation campaign over the corpus. Intended to be\n"
            << "      run from a sanitizer build; a finding reproduces from the printed seed.\n"
            << "Build with -DCNA_FX_FUZZER_ENTRY_POINT=ON under clang for a coverage-guided\n"
            << "libFuzzer campaign instead.\n";
        return 2;
    }

    int unreadable = 0;
    if (mode == "--campaign")
    {
        if (argc < 4)
        {
            std::cerr << "compiled_effect_fuzzer: --campaign needs a corpus and an iteration "
                         "count\n";
            return 2;
        }
        const auto corpus = ReadCorpus(std::filesystem::path(argv[2]), unreadable);
        if (corpus.empty())
        {
            std::cerr << "compiled_effect_fuzzer: the corpus is empty\n";
            return 1;
        }
        const long iterations = std::strtol(argv[3], nullptr, 10);
        Rng random{argc > 4 ? std::strtoull(argv[4], nullptr, 0) : 0x4658465556555AULL};
        std::cout << "campaign: " << corpus.size() << " seeds, " << iterations
                  << " iterations, seed 0x" << std::hex << random.state << std::dec << "\n";
        for (long i = 0; i < iterations; ++i)
        {
            std::vector<std::uint8_t> candidate = corpus[random.Below(
                static_cast<std::uint32_t>(corpus.size()))];
            Mutate(random, candidate);
            LLVMFuzzerTestOneInput(candidate.data(), candidate.size());
            // Flushed progress: a campaign that dies inside a native parser leaves no other
            // record of how far it got, and the iteration index plus the seed is what makes the
            // failing input reproducible without writing every candidate to disk.
            if ((i % 100) == 0)
                std::cout << "  iteration " << i << ", rng 0x" << std::hex << random.state
                          << std::dec << std::endl;
        }
        std::cout << "campaign complete: " << iterations << " iterations, no crash\n";
        return unreadable == 0 ? 0 : 1;
    }

    for (int i = 1; i < argc; ++i)
    {
        const auto corpus = ReadCorpus(std::filesystem::path(argv[i]), unreadable);
        for (const auto& bytes : corpus)
        {
            LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
            std::cout << "ok (" << bytes.size() << " bytes)\n";
        }
    }
    if (unreadable != 0) std::cerr << unreadable << " input(s) could not be read\n";
    return unreadable == 0 ? 0 : 1;
}
#endif
