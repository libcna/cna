// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-10: the single choke point for "the renderer cannot do this".
//
// Two things need pinning. The pass case must be *silent* -- a helper on a hot-ish path that
// allocates a message before deciding it does not need one is a helper nobody keeps calling. And
// the failure case must name the renderer, resolved from the device rather than passed in, since
// a call site that had to supply the name would get it wrong on the day the renderer is chosen at
// runtime.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/EngineException.hpp"
#include "CNA/Graphics/RequireCapability.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace {

using CNA::GraphicsCapability;
using CNA::Graphics::EngineException;
using CNA::Graphics::detail::nameOfCapability;
using CNA::Graphics::detail::requireCapability;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

/// Every value of the enum, in declaration order. Written out rather than derived, so a capability
/// added to `GraphicsCapability` without being added here shows up as a gap in coverage instead of
/// silently shrinking what these tests check.
constexpr GraphicsCapability kEveryCapability[] = {
    GraphicsCapability::ThreeD,
    GraphicsCapability::DepthStencilBuffer,
    GraphicsCapability::MultiSampleAntiAliasing,
    GraphicsCapability::MultipleRenderTargets,
    GraphicsCapability::AnisotropicFiltering,
    GraphicsCapability::WireFrame,
    GraphicsCapability::OcclusionQuery,
    GraphicsCapability::CustomEffects,
    GraphicsCapability::Texture3D,
    GraphicsCapability::MultiStreamVertexInput,
    GraphicsCapability::Instancing,
    GraphicsCapability::StencilBuffer,
    GraphicsCapability::AdditiveBlending,
    GraphicsCapability::CompiledEffects,
    GraphicsCapability::FloatRenderTargets,
    GraphicsCapability::HalfFloatRenderTargets,
    GraphicsCapability::HalfFloatTextureLinearFiltering,
    GraphicsCapability::ComputeShaders,
};

TEST(RequireCapabilityTest, ASupportedCapabilityReturnsWithoutThrowing)
{
    GraphicsDevice gd;
    // Whatever this renderer is, it supports something; check the quiet path on everything it has.
    // Asserting on a fixed capability would make this test about the renderer, not the helper.
    int checked = 0;
    for (const GraphicsCapability capability : kEveryCapability)
    {
        if (!gd.SupportsCapability(capability))
            continue;
        ++checked;
        EXPECT_NO_THROW(requireCapability(gd, capability, "RequireCapabilityTest"));
    }
    // plan_modern.md MOD-1693. This used to be an EXPECT_GT, on the assumption that every renderer
    // supports at least one thing. OpenVG is the counterexample: it answers no to every capability
    // in the enum. That makes the quiet path genuinely unreachable there rather than untested, and
    // this test is about the helper, not about the renderer -- so it skips, and says why.
    if (checked == 0)
        GTEST_SKIP() << "this renderer supports no capability at all, so requireCapability's quiet "
                        "path has nothing to be quiet about here";
}

TEST(RequireCapabilityTest, AnUnsupportedCapabilityThrowsWithTheRenderersOwnName)
{
    GraphicsDevice gd;
    // Find something this renderer does not have, so the test works on every renderer rather than
    // on the one it was written against.
    bool foundOne = false;
    for (const GraphicsCapability capability : kEveryCapability)
    {
        if (gd.SupportsCapability(capability))
            continue;
        foundOne = true;
        try
        {
            requireCapability(gd, capability, "SsaoPass");
            ADD_FAILURE() << "an unsupported capability did not throw";
        }
        catch (const EngineException& engine)
        {
            EXPECT_EQ(engine.getSubsystemProperty(), "SsaoPass");
            EXPECT_EQ(engine.getRequirementProperty(), nameOfCapability(capability));
            EXPECT_EQ(engine.getRendererNameProperty(),
                      std::string(gd.GetGraphicsRendererName()))
                << "the renderer name must come from the device, not the call site";
            EXPECT_EQ(engine.getMessageProperty(),
                      "SsaoPass: " + nameOfCapability(capability) + " is not supported by the "
                          + std::string(gd.GetGraphicsRendererName()) + " renderer");
        }
        break;
    }
    if (!foundOne)
        GTEST_SKIP() << "this renderer supports every capability there is, so there is no refusal "
                        "to observe";
}

TEST(RequireCapabilityTest, EveryCapabilityHasItsOwnName)
{
    // A name table with a copy-paste duplicate produces a message that blames the wrong feature,
    // which is worse than no name at all. Checking uniqueness catches that; checking against the
    // fallback catches a value that was added to the enum and forgotten here.
    std::vector<std::string> names;
    for (const GraphicsCapability capability : kEveryCapability)
    {
        const std::string name = nameOfCapability(capability);
        EXPECT_FALSE(name.empty());
        EXPECT_NE(name, "an unrecognised capability")
            << "capability " << static_cast<int>(capability) << " has no name of its own";
        names.push_back(name);
    }

    std::sort(names.begin(), names.end());
    EXPECT_EQ(std::adjacent_find(names.begin(), names.end()), names.end())
        << "two capabilities share a name, so one of them would be misreported";
}

TEST(RequireCapabilityTest, AValueOutsideTheEnumGetsADefinedAnswer)
{
    // Not a legal input, but an integer cast reaches it, and a name lookup that walked off the end
    // of a table would be a crash inside error handling -- the worst place for one.
    EXPECT_EQ(nameOfCapability(static_cast<GraphicsCapability>(9999)),
              "an unrecognised capability");
}

} // namespace

#endif // CNA_CNAEXT
