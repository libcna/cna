// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Platform/PlatformFactory.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "System/AppContext.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/String.hpp"

namespace {
constexpr const char* Switch = "SharpRuntime.UseNetFrameworkArgumentExceptionMessages";
TEST(GameExceptionCompatibility, XnaHostDefaultsToFrameworkParameterDiagnostics) {
    // Each ctest-discovered case runs in its own process, with no earlier Game.
    Microsoft::Xna::Framework::Game game;
    EXPECT_EQ(System::ArgumentException("bad", "value").getMessageProperty(), "bad\nParameter name: value");
    try { System::String::Substring("echo", 5); FAIL(); }
    catch (const System::ArgumentOutOfRangeException& e) {
        EXPECT_EQ(e.getMessageProperty(), "startIndex cannot be larger than length of string.\nParameter name: startIndex");
    }
}
TEST(GameExceptionCompatibility, AnExplicitModernProfileIsRespected) {
    bool previous = false;
    System::AppContext::TryGetSwitch(Switch, previous);
    System::AppContext::SetSwitch(Switch, false);
    {
        Microsoft::Xna::Framework::Game game;
        EXPECT_EQ(System::ArgumentException("bad", "value").getMessageProperty(), "bad (Parameter 'value')");
    }
    System::AppContext::SetSwitch(Switch, previous);
}
}
