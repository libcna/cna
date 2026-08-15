// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/PlatformCapabilities.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <type_traits>

namespace {

using namespace CNA::Platform;

TEST(PlatformCapabilitiesTests, DefaultConstructedReportsEverythingUnsupported)
{
    // An implementation states what it CAN do. A capability it forgets to mention must read as
    // unsupported, so the failure mode is a refused call rather than a crash in an unwritten path.
    const PlatformCapabilities capabilities;
    for (const PlatformCapability capability : AllCapabilities())
    {
        EXPECT_FALSE(Supports(capabilities, capability)) << "capability: " << ToString(capability);
    }
}

TEST(PlatformCapabilitiesTests, IsTriviallyCopyable)
{
    // Documented as read once and cached by value; a capability query must never be a virtual
    // call in a per-frame path.
    EXPECT_TRUE(std::is_trivially_copyable_v<PlatformCapabilities>);
}

TEST(PlatformCapabilitiesTests, AllCapabilitiesHasNoDuplicates)
{
    const std::set<PlatformCapability> unique(AllCapabilities().begin(), AllCapabilities().end());
    EXPECT_EQ(unique.size(), AllCapabilities().size());
}

TEST(PlatformCapabilitiesTests, EveryCapabilityMapsToADistinctStructField)
{
    // The load-bearing test for the enum/struct split. Setting exactly one capability must make
    // exactly one enum value read true -- if two enum values shared a field, or one were wired to
    // the wrong field, this catches it. A hand-written switch over 26 cases is precisely where a
    // copy-paste slip hides.
    for (const PlatformCapability capability : AllCapabilities())
    {
        PlatformCapabilities capabilities;

        // Find this capability's field by flipping the whole struct on and comparing: set every
        // field true, then confirm the capability reads true; the per-field isolation below is
        // what actually pins the mapping.
        int trueCount = 0;
        for (const PlatformCapability other : AllCapabilities())
        {
            if (Supports(capabilities, other))
            {
                ++trueCount;
            }
        }
        ASSERT_EQ(trueCount, 0) << "baseline must be all-false";

        // Flip one field at a time via the struct's own members, using Supports() as the oracle.
        // Done by brute force over the struct's bytes: the struct is an aggregate of bools, so
        // toggling byte i toggles exactly field i.
        auto* bytes = reinterpret_cast<unsigned char*>(&capabilities);
        const std::size_t fieldCount = sizeof(PlatformCapabilities) / sizeof(bool);
        ASSERT_EQ(fieldCount, AllCapabilities().size())
            << "struct field count must match the enum; adding one without the other is the bug "
               "this whole enum/struct split exists to catch";

        bool foundExactlyOne = false;
        for (std::size_t i = 0; i < fieldCount; ++i)
        {
            std::fill(bytes, bytes + fieldCount, static_cast<unsigned char>(0));
            bytes[i] = 1;

            int matches = 0;
            for (const PlatformCapability other : AllCapabilities())
            {
                if (Supports(capabilities, other))
                {
                    ++matches;
                }
            }
            EXPECT_EQ(matches, 1) << "field " << i << " must map to exactly one capability";

            if (Supports(capabilities, capability))
            {
                EXPECT_FALSE(foundExactlyOne)
                    << ToString(capability) << " is reachable from more than one field";
                foundExactlyOne = true;
            }
        }
        EXPECT_TRUE(foundExactlyOne) << ToString(capability) << " maps to no struct field";
    }
}

TEST(PlatformCapabilitiesTests, SupportsReadsTheMatchingFieldForRepresentativeCapabilities)
{
    // Explicit spot checks, so a wholesale regeneration of the switch cannot pass the structural
    // test above while silently renaming what a capability means.
    PlatformCapabilities capabilities;
    capabilities.highDpi = true;
    capabilities.vulkanSurface = true;
    capabilities.exactKeyboardState = true;

    EXPECT_TRUE(Supports(capabilities, PlatformCapability::HighDpi));
    EXPECT_TRUE(Supports(capabilities, PlatformCapability::VulkanSurface));
    EXPECT_TRUE(Supports(capabilities, PlatformCapability::ExactKeyboardState));
    EXPECT_FALSE(Supports(capabilities, PlatformCapability::Clipboard));
    EXPECT_FALSE(Supports(capabilities, PlatformCapability::Gamepad));
}

TEST(PlatformCapabilitiesTests, ToStringIsDistinctAndNonEmptyForEveryCapability)
{
    // Names appear in the refusal message a capability-gated call produces, so a caller learns
    // which capability was missing. Duplicates would defeat that.
    std::set<std::string> names;
    for (const PlatformCapability capability : AllCapabilities())
    {
        const std::string& name = ToString(capability);
        EXPECT_FALSE(name.empty());
        names.insert(name);
    }
    EXPECT_EQ(names.size(), AllCapabilities().size());
}

TEST(PlatformCapabilitiesTests, ToStringReturnsStableStorage)
{
    for (const PlatformCapability capability : AllCapabilities())
    {
        EXPECT_EQ(&ToString(capability), &ToString(capability));
    }
}

TEST(PlatformCapabilitiesTests, CapabilitySetCoversTheDocumentedContract)
{
    // cnaplatform.md sketched ten capabilities; PLAT-2's classification, PLAT-3's renderer audit
    // and the terminal analysis each added more. Pinning the count makes growing the set a
    // deliberate act that updates this expectation too.
    EXPECT_EQ(AllCapabilities().size(), 29u);
}

} // namespace
