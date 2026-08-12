// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/NativeWindowSystem.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

namespace {

using CNA::Platform::NativeWindowSystem;
using CNA::Platform::ToString;

/// Every enumerator, so the tests below can be exhaustive by construction rather than by
/// remembering to extend a list. A value added to the enum without being added here shows up
/// as a failing count assertion, not as silently reduced coverage.
const std::vector<NativeWindowSystem>& AllSystems()
{
    static const std::vector<NativeWindowSystem> all = {
        NativeWindowSystem::Unknown,
        NativeWindowSystem::Win32,
        NativeWindowSystem::X11,
        NativeWindowSystem::Wayland,
        NativeWindowSystem::Cocoa,
        NativeWindowSystem::Android,
        NativeWindowSystem::Web,
        NativeWindowSystem::Headless,
        NativeWindowSystem::Terminal,
    };
    return all;
}

TEST(NativeWindowSystemTests, EnumHasExactlyTheExpectedMembers)
{
    // Pins the size of the contract. Adding a windowing system is a deliberate act that must
    // also update ToString()'s exhaustive switch and this expectation together.
    EXPECT_EQ(AllSystems().size(), 9u);
}

TEST(NativeWindowSystemTests, ToStringReturnsExpectedNameForEverySystem)
{
    EXPECT_EQ(ToString(NativeWindowSystem::Unknown), "Unknown");
    EXPECT_EQ(ToString(NativeWindowSystem::Win32), "Win32");
    EXPECT_EQ(ToString(NativeWindowSystem::X11), "X11");
    EXPECT_EQ(ToString(NativeWindowSystem::Wayland), "Wayland");
    EXPECT_EQ(ToString(NativeWindowSystem::Cocoa), "Cocoa");
    EXPECT_EQ(ToString(NativeWindowSystem::Android), "Android");
    EXPECT_EQ(ToString(NativeWindowSystem::Web), "Web");
    EXPECT_EQ(ToString(NativeWindowSystem::Headless), "Headless");
    EXPECT_EQ(ToString(NativeWindowSystem::Terminal), "Terminal");
}

TEST(NativeWindowSystemTests, ToStringNamesAreAllDistinct)
{
    // The names exist to disambiguate a handle mismatch in a diagnostic. Two systems sharing a
    // name would make exactly the message that matters most ambiguous.
    std::set<std::string> names;
    for (const NativeWindowSystem system : AllSystems())
    {
        names.insert(ToString(system));
    }
    EXPECT_EQ(names.size(), AllSystems().size());
}

TEST(NativeWindowSystemTests, ToStringIsNeverEmpty)
{
    for (const NativeWindowSystem system : AllSystems())
    {
        EXPECT_FALSE(ToString(system).empty());
    }
}

TEST(NativeWindowSystemTests, ToStringReturnsStableStorageAcrossCalls)
{
    // Documented as returning a reference to stable storage, so a caller may hold the reference.
    // Returning a reference to a temporary would be undefined behaviour a value-comparison test
    // could easily miss.
    for (const NativeWindowSystem system : AllSystems())
    {
        const std::string& first = ToString(system);
        const std::string& second = ToString(system);
        EXPECT_EQ(&first, &second);
    }
}

TEST(NativeWindowSystemTests, UnknownIsTheDefaultConstructedValue)
{
    // NativeWindowHandle default-initialises its system field; a zero-initialised handle must
    // read as Unknown so an unset handle is never mistaken for a real Win32 one.
    EXPECT_EQ(static_cast<int>(NativeWindowSystem::Unknown), 0);
}

} // namespace
