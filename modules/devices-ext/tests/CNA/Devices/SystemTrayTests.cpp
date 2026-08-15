// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include "CNA/Devices/SystemTray.hpp"
#include "CNA/Platform/CannedTray.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <memory>

namespace {

    using CNA::Devices::SystemTray;
    using CNA::Platform::PlatformCapability;
    using CNA::Platform::PlatformFactory;
    using CNA::Platform::PlatformNotSupportedException;
    using CNA::Platform::Testing::CannedTrayPlatform;
    using CNA::Platform::Testing::CannedTrayState;
    using CNA::Platform::Testing::ScopedCurrentPlatform;

} // namespace

TEST(SystemTrayTests, IsSupportedReflectsTheSelectedPlatformCapability)
{
    {
        CannedTrayPlatform platform;
        ScopedCurrentPlatform current(platform);
        EXPECT_TRUE(SystemTray::getIsSupportedProperty());
    }
    {
        std::unique_ptr<CNA::Platform::IPlatform> platform = PlatformFactory::Create("Headless");
        ScopedCurrentPlatform current(*platform);
        EXPECT_FALSE(SystemTray::getIsSupportedProperty());
    }
}

TEST(SystemTrayTests, UnsupportedPlatformRefusesBeforeCreatingAnIcon)
{
    std::unique_ptr<CNA::Platform::IPlatform> platform = PlatformFactory::Create("Headless");
    ScopedCurrentPlatform current(*platform);

    try
    {
        SystemTray tray("unreachable");
        FAIL() << "an unsupported tray must refuse instead of becoming an inert object";
    }
    catch (const PlatformNotSupportedException& refusal)
    {
        EXPECT_EQ(refusal.GetCapability(), PlatformCapability::Tray);
    }
}

TEST(SystemTrayTests, ConstructorCreatesOneIconWithTheRequestedTooltip)
{
    CannedTrayPlatform platform;
    ScopedCurrentPlatform current(platform);

    SystemTray tray("my tooltip");

    ASSERT_EQ(platform.Canned().icons.size(), 1u);
    EXPECT_EQ(platform.Canned().icons[0]->tooltip, "my tooltip");
    EXPECT_FALSE(platform.Canned().icons[0]->destroyed);
}

TEST(SystemTrayTests, DestructorRemovesTheOwnedIcon)
{
    CannedTrayPlatform platform;
    ScopedCurrentPlatform current(platform);
    std::shared_ptr<CannedTrayState> state;

    {
        SystemTray tray("my tooltip");
        ASSERT_EQ(platform.Canned().icons.size(), 1u);
        state = platform.Canned().icons[0];
        EXPECT_FALSE(state->destroyed);
    }

    EXPECT_TRUE(state->destroyed);
}

TEST(SystemTrayTests, TooltipAndLabelChangesReachThePlatformIcon)
{
    CannedTrayPlatform platform;
    ScopedCurrentPlatform current(platform);
    SystemTray tray("initial");
    const std::shared_ptr<CannedTrayState>& state = platform.Canned().icons[0];

    tray.setTooltipProperty("updated");
    const std::size_t index = tray.AddEntry("Show", false, false, true, {});
    tray.SetEntryLabel(index, "Open");

    EXPECT_EQ(state->tooltip, "updated");
    ASSERT_EQ(state->entries.size(), 1u);
    EXPECT_EQ(state->entries[0].label, "Open");
}

TEST(SystemTrayTests, AddEntryPreservesParametersAndReturnsStableIndices)
{
    CannedTrayPlatform platform;
    ScopedCurrentPlatform current(platform);
    SystemTray tray("tooltip");
    const std::shared_ptr<CannedTrayState>& state = platform.Canned().icons[0];

    const std::size_t first = tray.AddEntry("Show", false, false, true, {});
    const std::size_t second = tray.AddEntry("Enabled", true, true, false, {});

    EXPECT_EQ(first, 0u);
    EXPECT_EQ(second, 1u);
    ASSERT_EQ(state->entries.size(), 2u);
    EXPECT_EQ(state->entries[0].label, "Show");
    EXPECT_FALSE(state->entries[0].checkable);
    EXPECT_TRUE(state->entries[0].enabled);
    EXPECT_TRUE(state->entries[1].checkable);
    EXPECT_TRUE(state->entries[1].checked);
    EXPECT_FALSE(state->entries[1].enabled);
}

TEST(SystemTrayTests, EntryClickCallbackCanFireRepeatedlyThroughThePlatformIcon)
{
    CannedTrayPlatform platform;
    ScopedCurrentPlatform current(platform);
    SystemTray tray("tooltip");
    const std::shared_ptr<CannedTrayState>& state = platform.Canned().icons[0];

    int clicks = 0;
    tray.AddEntry("Quit", false, false, true, [&clicks] { ++clicks; });

    ASSERT_EQ(state->entries.size(), 1u);
    ASSERT_TRUE(static_cast<bool>(state->entries[0].onClick));
    state->entries[0].onClick();
    state->entries[0].onClick();
    EXPECT_EQ(clicks, 2);
}

TEST(SystemTrayTests, CheckedAndEnabledStatesRoundTrip)
{
    CannedTrayPlatform platform;
    ScopedCurrentPlatform current(platform);
    SystemTray tray("tooltip");

    const std::size_t index = tray.AddEntry("Toggle", true, false, true, {});
    EXPECT_FALSE(tray.GetEntryChecked(index));
    EXPECT_TRUE(tray.GetEntryEnabled(index));

    tray.SetEntryChecked(index, true);
    tray.SetEntryEnabled(index, false);
    EXPECT_TRUE(tray.GetEntryChecked(index));
    EXPECT_FALSE(tray.GetEntryEnabled(index));
}

TEST(SystemTrayTests, UnknownEntryIndicesAreSafeAndReadFalse)
{
    CannedTrayPlatform platform;
    ScopedCurrentPlatform current(platform);
    SystemTray tray("tooltip");

    EXPECT_NO_THROW(tray.SetEntryLabel(42, "missing"));
    EXPECT_NO_THROW(tray.SetEntryChecked(42, true));
    EXPECT_NO_THROW(tray.SetEntryEnabled(42, true));
    EXPECT_FALSE(tray.GetEntryChecked(42));
    EXPECT_FALSE(tray.GetEntryEnabled(42));
}

#endif // CNA_DEVICES
