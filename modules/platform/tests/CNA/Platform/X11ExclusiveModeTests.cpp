// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0153: the parts of exclusive fullscreen that are arithmetic -- which mode
// a size gets, how the screen is resized around a CRTC change, the bytes the mode guardian sends.
// No X server: X11ExclusiveFullscreenTests.cpp exercises them against one.

#include <gtest/gtest.h>

#include "../../../src/X11/X11ModeGuardian.hpp"
#include "../../../src/X11/X11ModeSwitch.hpp"

#include <algorithm>
#include <cstring>
#include <optional>
#include <vector>

namespace {

using namespace CNA::Platform::X11;

X11ModeCandidate Mode(const std::uint64_t id, const int width, const int height,
                      const float refreshRate = 60.0f)
{
    X11ModeCandidate mode;
    mode.id = id;
    mode.width = width;
    mode.height = height;
    mode.refreshRate = refreshRate;
    return mode;
}

std::vector<X11ModeCandidate> DesktopModes()
{
    return {Mode(1, 1920, 1080), Mode(2, 1280, 1024), Mode(3, 1280, 720), Mode(4, 1024, 768),
            Mode(5, 800, 600), Mode(6, 640, 480)};
}

TEST(X11ExclusiveModeChoice, AnExactModeIsChosen)
{
    const std::optional<X11ModeCandidate> chosen =
        ChooseExclusiveMode(DesktopModes(), 1024, 768, 60.0f);
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(chosen->id, 4u);
}

TEST(X11ExclusiveModeChoice, ASizeBetweenModesGetsTheSmallestModeOfItsShapeThatHoldsIt)
{
    const std::optional<X11ModeCandidate> chosen =
        ChooseExclusiveMode(DesktopModes(), 1000, 750, 60.0f);
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(chosen->width, 1024);
    EXPECT_EQ(chosen->height, 768);
}

TEST(X11ExclusiveModeChoice, XnasDefaultBackBufferGetsTheWideModeRatherThanTheSmallSquareOne)
{
    // 800x480 fits in 800x600, but it is a 5:3 picture: SDL's rule takes the 16:9 1280x720,
    // whose shape is closer, over the smaller 4:3 mode. Pinned because it is the case an XNA game
    // meets first, and because "fixing" it would make CNA's X11 backend choose differently from
    // its SDL3 one.
    const std::optional<X11ModeCandidate> chosen =
        ChooseExclusiveMode(DesktopModes(), 800, 480, 60.0f);
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(chosen->width, 1280);
    EXPECT_EQ(chosen->height, 720);
}

TEST(X11ExclusiveModeChoice, TheAspectRatioDecidesBetweenModesThatBothHoldTheSize)
{
    // 1280x720 and 1024x768 both hold 960x540; 16:9 matches the request, so it wins even though
    // it is the larger of the two.
    const std::optional<X11ModeCandidate> chosen =
        ChooseExclusiveMode(DesktopModes(), 960, 540, 60.0f);
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(chosen->width, 1280);
    EXPECT_EQ(chosen->height, 720);
}

TEST(X11ExclusiveModeChoice, AModeTooShortIsPassedOverForATallerOne)
{
    // 1280x720 is wide enough for 1100x800 but not tall enough.
    const std::optional<X11ModeCandidate> chosen =
        ChooseExclusiveMode(DesktopModes(), 1100, 800, 60.0f);
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(chosen->width, 1280);
    EXPECT_EQ(chosen->height, 1024);
}

TEST(X11ExclusiveModeChoice, ASizeLargerThanEveryModeGetsNone)
{
    EXPECT_FALSE(ChooseExclusiveMode(DesktopModes(), 2560, 1440, 60.0f).has_value());
    EXPECT_FALSE(ChooseExclusiveMode(DesktopModes(), 1920, 1200, 60.0f).has_value());
    EXPECT_FALSE(ChooseExclusiveMode({}, 640, 480, 60.0f).has_value());
}

TEST(X11ExclusiveModeChoice, AmongModesOfOneSizeTheDesktopsRefreshRateWins)
{
    const std::vector<X11ModeCandidate> modes = {Mode(1, 800, 600, 75.0f), Mode(2, 800, 600, 60.0f),
                                                 Mode(3, 800, 600, 85.0f)};
    EXPECT_EQ(ChooseExclusiveMode(modes, 800, 600, 60.0f)->id, 2u);
    EXPECT_EQ(ChooseExclusiveMode(modes, 800, 600, 72.0f)->id, 1u);
    // Equally far from the desktop's rate: the higher one.
    EXPECT_EQ(ChooseExclusiveMode({Mode(1, 800, 600, 50.0f), Mode(2, 800, 600, 70.0f)}, 800, 600,
                                  60.0f)
                  ->id,
              2u);
}

TEST(X11ExclusiveModeChoice, TheOrderModesArriveInDoesNotMatter)
{
    std::vector<X11ModeCandidate> reversed = DesktopModes();
    std::reverse(reversed.begin(), reversed.end());
    EXPECT_EQ(ChooseExclusiveMode(reversed, 900, 700, 60.0f)->id,
              ChooseExclusiveMode(DesktopModes(), 900, 700, 60.0f)->id);
}

TEST(X11ExclusiveModeChoice, ADegenerateModeIsNeverChosen)
{
    const std::optional<X11ModeCandidate> chosen =
        ChooseExclusiveMode({Mode(1, 0, 0), Mode(2, 640, 480)}, 320, 240, 60.0f);
    ASSERT_TRUE(chosen.has_value());
    EXPECT_EQ(chosen->id, 2u);
}

// --- the screen around a CRTC change ------------------------------------------------------------

TEST(X11ScreenPlan, OneMonitorShrinkingChangesTheCrtcFirstAndTheScreenAfter)
{
    // 1280x1024 -> 800x600: the CRTC fits the old screen, so no growing; the screen then follows
    // the mode, so the pointer cannot wander into the part nothing shows.
    const X11ScreenPlan plan =
        PlanScreenSizes(1280, 1024, nullptr, 0, X11Rect{0, 0, 800, 600}, 0, 0, 8, 8, 32767, 32767);
    EXPECT_EQ(plan.growWidth, 1280);
    EXPECT_EQ(plan.growHeight, 1024);
    EXPECT_EQ(plan.finalWidth, 800);
    EXPECT_EQ(plan.finalHeight, 600);
}

TEST(X11ScreenPlan, OneMonitorGrowingGrowsTheScreenFirstAndNothingAfter)
{
    const X11ScreenPlan plan = PlanScreenSizes(800, 600, nullptr, 0, X11Rect{0, 0, 1280, 1024},
                                               1280, 1024, 8, 8, 32767, 32767);
    EXPECT_EQ(plan.growWidth, 1280);
    EXPECT_EQ(plan.growHeight, 1024);
    EXPECT_EQ(plan.finalWidth, 1280);
    EXPECT_EQ(plan.finalHeight, 1024);
}

TEST(X11ScreenPlan, AnotherMonitorKeepsItsPlaceWhenOneShrinks)
{
    // Two 1920x1080 monitors side by side; the left one goes to 800x600. The screen may not cut
    // through the right one -- SDL shrinks the screen to the mode and gets BadMatch here.
    const X11Rect right{1920, 0, 1920, 1080};
    const X11ScreenPlan plan = PlanScreenSizes(3840, 1080, &right, 1, X11Rect{0, 0, 800, 600}, 0,
                                               0, 8, 8, 32767, 32767);
    EXPECT_EQ(plan.growWidth, 3840);
    EXPECT_EQ(plan.growHeight, 1080);
    EXPECT_EQ(plan.finalWidth, 3840);
    EXPECT_EQ(plan.finalHeight, 1080);
}

TEST(X11ScreenPlan, AMonitorBelowAnotherShrinksTheScreenOnlyWhereNothingIs)
{
    // A 1280x1024 on top of a 1920x1080; the top one goes to 640x480. Nothing reaches past the
    // lower monitor's right edge, and the lower one is at y=1024 still.
    const X11Rect lower{0, 1024, 1920, 1080};
    const X11ScreenPlan plan = PlanScreenSizes(1920, 2104, &lower, 1, X11Rect{0, 0, 640, 480}, 0,
                                               0, 8, 8, 32767, 32767);
    EXPECT_EQ(plan.finalWidth, 1920);
    EXPECT_EQ(plan.finalHeight, 2104);
}

TEST(X11ScreenPlan, ARestoreGetsBackAScreenLargerThanItsMonitors)
{
    // A desktop with a virtual screen larger than its only monitor (`xrandr --fb`). Switching
    // shrank the screen to the mode; the restore brings back the size the user had, not merely
    // the size the monitor needs.
    const X11ScreenPlan plan = PlanScreenSizes(800, 600, nullptr, 0, X11Rect{0, 0, 1280, 1024},
                                               2560, 1600, 8, 8, 32767, 32767);
    EXPECT_EQ(plan.growWidth, 2560);
    EXPECT_EQ(plan.growHeight, 1600);
    EXPECT_EQ(plan.finalWidth, 2560);
    EXPECT_EQ(plan.finalHeight, 1600);
}

TEST(X11ScreenPlan, TheServersSizeRangeBoundsTheScreen)
{
    const X11ScreenPlan small =
        PlanScreenSizes(1280, 1024, nullptr, 0, X11Rect{0, 0, 320, 200}, 0, 0, 640, 480, 4096, 4096);
    EXPECT_EQ(small.finalWidth, 640);
    EXPECT_EQ(small.finalHeight, 480);

    const X11ScreenPlan large = PlanScreenSizes(1280, 1024, nullptr, 0, X11Rect{0, 0, 8000, 8000},
                                                0, 0, 8, 8, 4096, 4096);
    EXPECT_EQ(large.finalWidth, 4096);
    EXPECT_EQ(large.finalHeight, 4096);
}

TEST(X11ScreenPlan, AChangeThatNeedsNothingPlansNothing)
{
    const X11ScreenPlan plan = PlanScreenSizes(1280, 1024, nullptr, 0, X11Rect{0, 0, 1280, 1024},
                                               0, 0, 8, 8, 32767, 32767);
    EXPECT_EQ(plan.growWidth, 1280);
    EXPECT_EQ(plan.growHeight, 1024);
    EXPECT_EQ(plan.finalWidth, 1280);
    EXPECT_EQ(plan.finalHeight, 1024);
}

TEST(X11ScreenPlan, TheScreensDensityIsKept)
{
    // 1280 px across 338 mm; 800 px of the same screen is 211 mm.
    EXPECT_EQ(MillimetresFor(800, 1280, 338), 211u);
    EXPECT_EQ(MillimetresFor(1280, 1280, 338), 338u);
    // No density to keep: the X server's own 96 DPI.
    EXPECT_EQ(MillimetresFor(960, 0, 0), 254u);
    // Never zero, which RandR refuses.
    EXPECT_EQ(MillimetresFor(1, 1280, 338), 1u);
}

// --- the bytes the guardian sends -----------------------------------------------------------------

TEST(X11ModeGuardianWire, TheConnectionSetupIsLittleEndianProtocolElevenWithItsCookiePadded)
{
    const char name[] = "MIT-MAGIC-COOKIE-1";
    const unsigned char cookie[16] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                      0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
    unsigned char setup[64] = {};
    const std::size_t length =
        EncodeConnectionSetup(setup, sizeof(setup), name, sizeof(name) - 1, cookie, sizeof(cookie));
    // 12 bytes of header, the 18-byte name padded to 20, the 16-byte cookie.
    ASSERT_EQ(length, 12u + 20u + 16u);
    EXPECT_EQ(setup[0], 'l');
    EXPECT_EQ(setup[2], 11);
    EXPECT_EQ(setup[3], 0);
    EXPECT_EQ(setup[4], 0);
    EXPECT_EQ(setup[5], 0);
    EXPECT_EQ(setup[6], 18);
    EXPECT_EQ(setup[7], 0);
    EXPECT_EQ(setup[8], 16);
    EXPECT_EQ(setup[9], 0);
    EXPECT_EQ(std::memcmp(setup + 12, name, 18), 0);
    EXPECT_EQ(setup[30], 0);
    EXPECT_EQ(setup[31], 0);
    EXPECT_EQ(std::memcmp(setup + 32, cookie, 16), 0);
}

TEST(X11ModeGuardianWire, ASetupWithoutAuthorisationIsTheBareHeader)
{
    unsigned char setup[16] = {};
    EXPECT_EQ(EncodeConnectionSetup(setup, sizeof(setup), nullptr, 0, nullptr, 0), 12u);
    EXPECT_EQ(setup[6], 0);
    EXPECT_EQ(setup[8], 0);
}

TEST(X11ModeGuardianWire, ASetupThatDoesNotFitIsRefusedRatherThanTruncated)
{
    const char name[] = "MIT-MAGIC-COOKIE-1";
    const unsigned char cookie[16] = {};
    unsigned char setup[20] = {};
    EXPECT_EQ(EncodeConnectionSetup(setup, sizeof(setup), name, sizeof(name) - 1, cookie,
                                    sizeof(cookie)),
              0u);
}

TEST(X11ModeGuardianWire, ARestoreWithNowhereToConnectFailsWithoutTouchingAnything)
{
    X11ModeRestorePlan plan;
    unsigned char scratch[4096] = {};
    EXPECT_EQ(RestoreDisplayModeRaw(plan, scratch, sizeof(scratch)), X11ModeRestoreResult::Failed);
    EXPECT_EQ(RestoreDisplayModeRaw(plan, nullptr, 0), X11ModeRestoreResult::Failed);
}

} // namespace
