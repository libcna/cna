// SPDX-License-Identifier: MS-PL
//
// Game::PollEvents' F9/F10 context-loss hotkeys are a debugging aid: a released game must leave
// both keys to the game (galaxy-eggbert UB-013: F9 destroyed the GL context, and on the web left
// the canvas black until F10). CNA_DEBUG_CONTEXT_HOTKEYS decides; cmake/UnitTests.cmake hands this
// file the configured mode and configuration so the rule is restated here, not read back.

#include <gtest/gtest.h>

#include "CNA/Internal/Runtime/DebugContextHotkeys.hpp"
#include "CNA/Platform/PlatformEvent.hpp"

#include <string>

namespace
{
    using CNA::Internal::Runtime::ClassifyDebugContextHotkey;
    using CNA::Internal::Runtime::DebugContextHotkey;
    using CNA::Internal::Runtime::DebugContextHotkeysEnabled;
    using CNA::Platform::KeyCode;

    CNA::Platform::KeyEvent Key(const KeyCode key, const bool pressed, const bool repeat)
    {
        CNA::Platform::KeyEvent event;
        event.keycode = key;
        event.pressed = pressed;
        event.repeat = repeat;
        return event;
    }
}

TEST(DebugContextHotkeysTest, BuildSettingFollowsTheConfiguredMode)
{
#if defined(CNA_TEST_DEBUG_CONTEXT_HOTKEYS_MODE) && defined(CNA_TEST_CONFIG_IS_DEBUG)
    const std::string mode = CNA_TEST_DEBUG_CONTEXT_HOTKEYS_MODE;
    const bool debugConfiguration = CNA_TEST_CONFIG_IS_DEBUG != 0;
    const bool expected = mode == "ON" || (mode == "AUTO" && debugConfiguration);
    EXPECT_EQ(DebugContextHotkeysEnabled(), expected)
        << "CNA_DEBUG_CONTEXT_HOTKEYS=" << mode << ", Debug configuration: " << debugConfiguration;
#else
    FAIL() << "cmake/UnitTests.cmake did not pass CNA_TEST_DEBUG_CONTEXT_HOTKEYS_MODE and "
              "CNA_TEST_CONFIG_IS_DEBUG to this file";
#endif
}

TEST(DebugContextHotkeysTest, DefaultArgumentIsTheBuildSetting)
{
    const auto f9 = Key(KeyCode::F9, true, false);
    EXPECT_EQ(ClassifyDebugContextHotkey(f9),
              ClassifyDebugContextHotkey(f9, DebugContextHotkeysEnabled()));
}

TEST(DebugContextHotkeysTest, EnabledMapsAFreshF9PressToLossAndF10ToRestore)
{
    EXPECT_EQ(ClassifyDebugContextHotkey(Key(KeyCode::F9, true, false), true),
              DebugContextHotkey::SimulateLoss);
    EXPECT_EQ(ClassifyDebugContextHotkey(Key(KeyCode::F10, true, false), true),
              DebugContextHotkey::Restore);
}

TEST(DebugContextHotkeysTest, EnabledIgnoresReleasesRepeatsAndOtherKeys)
{
    EXPECT_EQ(ClassifyDebugContextHotkey(Key(KeyCode::F9, false, false), true),
              DebugContextHotkey::None);
    EXPECT_EQ(ClassifyDebugContextHotkey(Key(KeyCode::F10, false, false), true),
              DebugContextHotkey::None);
    EXPECT_EQ(ClassifyDebugContextHotkey(Key(KeyCode::F9, true, true), true),
              DebugContextHotkey::None);
    EXPECT_EQ(ClassifyDebugContextHotkey(Key(KeyCode::F10, true, true), true),
              DebugContextHotkey::None);
    EXPECT_EQ(ClassifyDebugContextHotkey(Key(KeyCode::F11, true, false), true),
              DebugContextHotkey::None);
    EXPECT_EQ(ClassifyDebugContextHotkey(Key(KeyCode::A, true, false), true),
              DebugContextHotkey::None);
}

TEST(DebugContextHotkeysTest, DisabledLeavesF9AndF10ToTheGame)
{
    EXPECT_EQ(ClassifyDebugContextHotkey(Key(KeyCode::F9, true, false), false),
              DebugContextHotkey::None);
    EXPECT_EQ(ClassifyDebugContextHotkey(Key(KeyCode::F10, true, false), false),
              DebugContextHotkey::None);
}
