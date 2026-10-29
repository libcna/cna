// SPDX-License-Identifier: MS-PL
// FX-111: a compiled effect truncated at every length is refused, or parsed whole,
// by the selected renderer's own compiled-effect runtime -- and never wedges the test run. Moved
// from the retired VULKAN suite (VulkanCompiledEffectTest.MalformedBytecodeIsRejectedWithoutCrashing).
//
// Some lengths of the real conformance effect reach an assertion inside pinned MojoShader's effect
// parser (`mojoshader_effects.c` `readvalue`). MojoShader's `assert` is SDL's, whose default
// handler blocks for an interactive answer, so before tests/HarnessAssertionPolicy.cpp made the
// suite's policy non-interactive this case hung the whole run. It is the one case that proves that
// policy is in effect: if it regresses, this test hangs.
//
// The sweep calls IGraphicsRenderer::CreateCompiledEffect rather than the public Effect
// constructor on purpose. The public constructor runs a structural Effect Framework preflight that
// refuses every truncated length before any renderer parses it -- measured on the SDL GPU
// renderer, the public sweep never reaches MojoShader even with the assertion policy set to
// abort -- so it cannot exercise the policy.
// That public sweep is CompiledEffectConformance's RunCompiledEffectTruncationContract, which every
// compiled-effect family already runs.

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/ICompiledEffectRuntime.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/TestSupport/TestPaths.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

namespace
{
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using CNA::Internal::Renderers::ICompiledEffectRuntime;
}

TEST(CompiledEffectTruncationSweepTest, EveryTruncationOfARealEffectIsRefusedOrParsedWhole)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "the selected renderer does not execute compiled effects";

    const std::filesystem::path fixture =
        CNA::TestSupport::CompiledEffectDirectory() / "CnaConformanceEffect.fxb";
    std::ifstream input(fixture, std::ios::binary);
    const std::vector<std::uint8_t> whole{std::istreambuf_iterator<char>(input),
                                          std::istreambuf_iterator<char>()};
    ASSERT_GT(whole.size(), 64u) << "the committed fixture is missing or too small: " << fixture;

    // The whole effect must parse, or the sweep below proves nothing about partial inputs.
    ASSERT_NE(device.GetRenderer().CreateCompiledEffect(whole.data(), whole.size()), nullptr);

    for (std::size_t bytes = 4; bytes < whole.size(); bytes += 4)
    {
        std::unique_ptr<ICompiledEffectRuntime> parsed;
        try
        {
            parsed = device.GetRenderer().CreateCompiledEffect(whole.data(), bytes);
        }
        catch (const std::exception&)
        {
            continue;  // refused: the usual outcome
        }
        // The last few lengths drop only trailing bytes MojoShader does not need, so a parse is
        // legitimate -- but then the runtime must be whole, not a half-built object.
        ASSERT_NE(parsed, nullptr) << "truncated to " << bytes << " bytes";
        EXPECT_FALSE(parsed->GetDescription().techniques.empty())
            << "truncated to " << bytes << " bytes: accepted, but reflects nothing";
    }
}
