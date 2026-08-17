// SPDX-License-Identifier: MS-PL
//
// plan_runtimerenderer.md RTR-P2-3 / RTR-P6-9: the generated registry agrees with itself and with
// the descriptors it lists.
//
// The registry is the join between four things written in four different places: the identity in
// GraphicsRendererType.hpp, the identity -> namespace entry in cmake/RendererRegistry.cmake, the
// descriptor accessor in a family's own *RendererDescriptor.cpp, and the factory that descriptor
// takes the address of. scripts/check_runtime_renderer_discipline.py checks that chain is COMPLETE
// for every identity, textually and for every configuration at once. What it cannot check is that
// the chain is CORRECT for the identities this particular build actually linked -- a descriptor
// naming an identity other than the one CMake asked for, or a name that does not match the enum's
// name table, is a link-time-valid, run-time-wrong registry, and only a build can catch it.
//
// PIXIJS is the reason both halves exist: it reached a release with a family directory, a working
// renderer and no registry entry at all, so configuring it could only ever have died inside
// cna_renderer_identity_to_namespace(). Nothing here or in the discipline gate would have let that
// through.

#include <gtest/gtest.h>

#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <string_view>

namespace Registry = CNA::Internal::Renderers::GraphicsRendererRegistry;

using CNA::GraphicsRendererType;

namespace
{
    [[nodiscard]] std::string ToLower(std::string_view text)
    {
        std::string lowered(text);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lowered;
    }
}

TEST(GraphicsRendererRegistryTest, IsNeverEmptyAndCountAgreesWithAll)
{
    // A build with no renderer cannot configure, so an empty table would mean the generated unit
    // was not the one this binary linked.
    ASSERT_GT(Registry::Count(), 0u);
    EXPECT_EQ(Registry::Count(), Registry::All().size());
}

TEST(GraphicsRendererRegistryTest, DefaultIsTheFirstEntry)
{
    // cmake/RendererRegistry.cmake puts CNA_GRAPHICS_RENDERER first, and GraphicsRendererSelection
    // attempts the default before any fallback candidate. Those two statements are the same fact.
    EXPECT_EQ(&Registry::Default(), Registry::All().data());
}

TEST(GraphicsRendererRegistryTest, EveryEntryIsServedByExactlyOneDescriptor)
{
    std::set<int> seenTypes;
    std::set<std::string> seenNames;
    for (const auto& descriptor : Registry::All())
    {
        EXPECT_TRUE(seenTypes.insert(static_cast<int>(descriptor.type)).second)
            << "identity " << CNA::getGraphicsRendererName(descriptor.type)
            << " appears twice in the generated registry";
        EXPECT_TRUE(seenNames.insert(ToLower(descriptor.name)).second)
            << "renderer name " << descriptor.name << " appears twice";
    }
}

TEST(GraphicsRendererRegistryTest, EveryDescriptorNamesItsOwnIdentity)
{
    // The name is a copy of the enum's name table, not an independent spelling: an environment
    // variable or a JS-side selection is matched against THIS string, so a family that wrote its
    // own would answer to a name nothing else in the project uses.
    for (const auto& descriptor : Registry::All())
    {
        EXPECT_EQ(CNA::getGraphicsRendererName(descriptor.type), descriptor.name);
    }
}

TEST(GraphicsRendererRegistryTest, EveryDescriptorCarriesTheHooksTheContractRequires)
{
    // GraphicsRendererDescriptor documents both of these as required-non-null precisely so that no
    // call site needs a null check; a family that left one out would fault at construction rather
    // than fail with a diagnostic.
    for (const auto& descriptor : Registry::All())
    {
        EXPECT_NE(nullptr, descriptor.isAvailable)
            << CNA::getGraphicsRendererName(descriptor.type) << " has no availability probe";
        EXPECT_NE(nullptr, descriptor.create)
            << CNA::getGraphicsRendererName(descriptor.type) << " has no factory";
    }
}

TEST(GraphicsRendererRegistryTest, ADescriptorNeedingAWindowAlsoNeedsTheVideoSubsystem)
{
    // GraphicsDevice acquires the video subsystem for the candidate and then creates its window;
    // a descriptor claiming it needs a window without the subsystem that backs one would ask the
    // platform to create a window on a subsystem nobody started.
    for (const auto& descriptor : Registry::All())
    {
        if (descriptor.needsWindow)
        {
            EXPECT_TRUE(descriptor.needsVideoSubsystem)
                << CNA::getGraphicsRendererName(descriptor.type)
                << " needs a window but claims not to need the video subsystem";
        }
        // The reverse is deliberately NOT asserted: a family may legitimately need the subsystem
        // for something other than a window of its own.
    }
}

TEST(GraphicsRendererRegistryTest, LookupByIdentityReturnsThatEntry)
{
    for (const auto& descriptor : Registry::All())
    {
        EXPECT_EQ(&descriptor, Registry::Find(descriptor.type));
    }
}

TEST(GraphicsRendererRegistryTest, LookupByNameIsCaseInsensitiveAndFindsTheSameEntry)
{
    for (const auto& descriptor : Registry::All())
    {
        const std::string canonical(descriptor.name);
        EXPECT_EQ(&descriptor, Registry::Find(canonical));
        EXPECT_EQ(&descriptor, Registry::Find(ToLower(canonical)));
    }
}

TEST(GraphicsRendererRegistryTest, LookupOfSomethingThisBuildDoesNotContainReturnsNull)
{
    EXPECT_EQ(nullptr, Registry::Find(std::string_view("NOT_A_RENDERER")));
    EXPECT_EQ(nullptr, Registry::Find(std::string_view("")));

    for (int ordinal = 0; ordinal <= static_cast<int>(GraphicsRendererType::PixiJs); ++ordinal)
    {
        const auto candidate = static_cast<GraphicsRendererType>(ordinal);
        if (Registry::Find(candidate) == nullptr)
        {
            SUCCEED() << CNA::getGraphicsRendererName(candidate)
                      << " is not compiled in and is correctly reported as absent";
            return;
        }
    }
    SUCCEED() << "this build contains every public identity, so there is nothing to miss";
}

TEST(GraphicsRendererRegistryTest, TheSelectionLayerSeesExactlyWhatTheRegistryHolds)
{
    // The generated unit publishes the compiled-in set into GraphicsRendererSelection before
    // main() runs. If that ever stopped happening, SetPreferred() would accept a renderer this
    // build does not contain -- which is what it did before RTR-P4 made the publish eager.
    const auto available = CNA::GraphicsRendererSelection::GetAvailable();
    ASSERT_EQ(Registry::Count(), available.size());
    for (std::size_t index = 0; index < available.size(); ++index)
    {
        EXPECT_EQ(Registry::All()[index].type, available[index]);
    }
    EXPECT_TRUE(CNA::GraphicsRendererSelection::IsAvailable(Registry::Default().type));
}
