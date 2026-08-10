// SPDX-License-Identifier: MS-PL
// Task 345: audit GraphicsAdapter API against FNA.
//
// GraphicsAdapter::DefaultAdapter is initialized at static-init time (before main()) via
// getDefaultAdapterProperty(), which populates the adapter list lazily. That first population
// runs before any test gets a chance to call SDL_InitSubSystem(SDL_INIT_VIDEO), so if SDL wasn't
// already initialized, the adapter list falls back to a single synthetic "Default Display"
// adapter (see GraphicsAdapter::AdaptersChanged()'s no-displays branch). Tests that don't care
// whether the adapter is real or synthetic run unconditionally; tests that need to verify
// real-display-derived state (DeviceName format, display mode dedup) explicitly initialize SDL
// video and force a refresh via AdaptersChanged(), skipping under GTEST_SKIP() if unavailable.

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DisplayMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DisplayModeCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DisplayMode;
using Microsoft::Xna::Framework::Graphics::DisplayModeCollection;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

// --- Adapters / DefaultAdapter ---

TEST(GraphicsAdapterTest, AdaptersIsNonEmpty)
{
    const auto& adapters = GraphicsAdapter::getAdaptersProperty();
    EXPECT_FALSE(adapters.empty());
}

TEST(GraphicsAdapterTest, DefaultAdapterIsAdaptersFirstEntry)
{
    const auto& adapters = GraphicsAdapter::getAdaptersProperty();
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_EQ(&def, adapters[0].get());
}

TEST(GraphicsAdapterTest, DefaultAdapterIsDefaultAdapterPropertyTrue)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_TRUE(def.getIsDefaultAdapterProperty());
}

TEST(GraphicsAdapterTest, DefaultAdapterRemainsValidAcrossAdaptersChanged)
{
    // Regression test: GraphicsAdapter::AdaptersChanged() destroys and recreates every
    // GraphicsAdapter instance. getDefaultAdapterProperty() must be re-evaluated fresh on every
    // call (matching FNA's DefaultAdapter property) rather than returning a stale/dangling
    // reference to an adapter instance from before the refresh.
    GraphicsAdapter::AdaptersChanged();
    GraphicsAdapter& first = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_TRUE(first.getIsDefaultAdapterProperty());

    GraphicsAdapter::AdaptersChanged();
    GraphicsAdapter& second = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_TRUE(second.getIsDefaultAdapterProperty());
    EXPECT_FALSE(second.getDescriptionProperty().empty());
}

// --- Description / DeviceName ---

TEST(GraphicsAdapterTest, DescriptionAndDeviceNameAreNonEmpty)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_FALSE(def.getDescriptionProperty().empty());
    EXPECT_FALSE(def.getDeviceNameProperty().empty());
}

TEST(GraphicsAdapterTest, RealDisplay_DeviceNameFollowsWindowsStylePathConvention)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    GraphicsAdapter::AdaptersChanged();
    const auto& adapters = GraphicsAdapter::getAdaptersProperty();
    ASSERT_FALSE(adapters.empty());

    // Matches FNA's SDL3_FNAPlatform.GetGraphicsAdapters(): DeviceName is a synthetic
    // "\\.\DISPLAYn" path, distinct from Description (the real display name).
    const std::string& deviceName = adapters[0]->getDeviceNameProperty();
    EXPECT_EQ(deviceName.rfind("\\\\.\\DISPLAY", 0), 0u);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

// --- SupportedDisplayModes / CurrentDisplayMode ---

TEST(GraphicsAdapterTest, SupportedDisplayModesIsNonEmpty)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    const DisplayModeCollection& modes = def.getSupportedDisplayModesProperty();
    EXPECT_GT(modes.getCountProperty(), 0);
}

TEST(GraphicsAdapterTest, RealDisplay_SupportedDisplayModesHasNoWidthHeightDuplicates)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    GraphicsAdapter::AdaptersChanged();
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    const DisplayModeCollection& modes = def.getSupportedDisplayModesProperty();

    // Matches FNA's SDL3_FNAPlatform.GetGraphicsAdapters(): modes that only differ by refresh
    // rate must be deduplicated by (width, height) — a display supporting several refresh rates
    // at the same resolution must not report that resolution more than once.
    for (SharpRuntime::intcs i = 0; i < modes.getCountProperty(); ++i)
    {
        for (SharpRuntime::intcs j = i + 1; j < modes.getCountProperty(); ++j)
        {
            const bool sameSize = modes[i].getWidthProperty() == modes[j].getWidthProperty() &&
                                   modes[i].getHeightProperty() == modes[j].getHeightProperty();
            EXPECT_FALSE(sameSize) << "duplicate mode at indices " << i << " and " << j;
        }
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

TEST(GraphicsAdapterTest, CurrentDisplayModeHasPositiveDimensions)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    DisplayMode mode = def.getCurrentDisplayModeProperty();
    EXPECT_GT(mode.getWidthProperty(), 0);
    EXPECT_GT(mode.getHeightProperty(), 0);
}

// --- IsWideScreen / MonitorHandle ---

TEST(GraphicsAdapterTest, IsWideScreenDoesNotThrow)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_NO_THROW({ (void)def.getIsWideScreenProperty(); });
}

TEST(GraphicsAdapterTest, MonitorHandleDoesNotThrow)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_NO_THROW({ (void)def.getMonitorHandleProperty(); });
}

// --- DeviceId / Revision / SubSystemId / VendorId ---
//
// FNA always throws NotImplementedException for all 4. CNA deliberately deviates: DeviceId and
// VendorId query the real PCI ID via sysfs on Linux (returning 0 elsewhere/on failure); Revision
// and SubSystemId are not queryable via SDL at all and always return 0. None of the 4 throw.

TEST(GraphicsAdapterTest, DeviceIdAndVendorIdDoNotThrow)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_NO_THROW({ (void)def.getDeviceIdProperty(); });
    EXPECT_NO_THROW({ (void)def.getVendorIdProperty(); });
}

TEST(GraphicsAdapterTest, RevisionIsAlwaysZero)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_EQ(def.getRevisionProperty(), 0);
}

TEST(GraphicsAdapterTest, SubSystemIdIsAlwaysZero)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_EQ(def.getSubSystemIdProperty(), 0);
}

// --- UseNullDevice / UseReferenceDevice ---

TEST(GraphicsAdapterTest, UseNullDeviceRoundTrip)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    const bool original = def.getUseNullDeviceProperty();

    def.setUseNullDeviceProperty(true);
    EXPECT_TRUE(def.getUseNullDeviceProperty());
    def.setUseNullDeviceProperty(false);
    EXPECT_FALSE(def.getUseNullDeviceProperty());

    def.setUseNullDeviceProperty(original);
}

TEST(GraphicsAdapterTest, UseReferenceDeviceRoundTrip)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    const bool original = def.getUseReferenceDeviceProperty();

    def.setUseReferenceDeviceProperty(true);
    EXPECT_TRUE(def.getUseReferenceDeviceProperty());
    def.setUseReferenceDeviceProperty(false);
    EXPECT_FALSE(def.getUseReferenceDeviceProperty());

    def.setUseReferenceDeviceProperty(original);
}

// --- IsProfileSupported ---

TEST(GraphicsAdapterTest, IsProfileSupportedReachIsTrue)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_TRUE(def.IsProfileSupported(GraphicsProfile::Reach));
}

TEST(GraphicsAdapterTest, IsProfileSupportedHiDefIsTrue)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_TRUE(def.IsProfileSupported(GraphicsProfile::HiDef));
}

// --- QueryRenderTargetFormat ---

TEST(GraphicsAdapterTest, QueryRenderTargetFormatAcceptsSupportedFormat)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    SurfaceFormat selectedFormat;
    DepthFormat selectedDepthFormat;
    SharpRuntime::intcs selectedMultiSampleCount;

    const bool accepted = def.QueryRenderTargetFormat(
        GraphicsProfile::HiDef, SurfaceFormat::Color, DepthFormat::None, 0,
        selectedFormat, selectedDepthFormat, selectedMultiSampleCount);

    EXPECT_TRUE(accepted);
    EXPECT_EQ(selectedFormat, SurfaceFormat::Color);
    EXPECT_EQ(selectedDepthFormat, DepthFormat::None);
    EXPECT_EQ(selectedMultiSampleCount, 0);
}

TEST(GraphicsAdapterTest, QueryRenderTargetFormatSubstitutesUnsupportedFormat)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    SurfaceFormat selectedFormat;
    DepthFormat selectedDepthFormat;
    SharpRuntime::intcs selectedMultiSampleCount;

    const bool accepted = def.QueryRenderTargetFormat(
        GraphicsProfile::HiDef, SurfaceFormat::Bgr565, DepthFormat::None, 0,
        selectedFormat, selectedDepthFormat, selectedMultiSampleCount);

    EXPECT_FALSE(accepted);
    EXPECT_EQ(selectedFormat, SurfaceFormat::Color);
}

// --- QueryBackBufferFormat ---

TEST(GraphicsAdapterTest, QueryBackBufferFormatAcceptsColor)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    SurfaceFormat selectedFormat;
    DepthFormat selectedDepthFormat;
    SharpRuntime::intcs selectedMultiSampleCount;

    const bool accepted = def.QueryBackBufferFormat(
        GraphicsProfile::HiDef, SurfaceFormat::Color, DepthFormat::None, 0,
        selectedFormat, selectedDepthFormat, selectedMultiSampleCount);

    EXPECT_TRUE(accepted);
    EXPECT_EQ(selectedFormat, SurfaceFormat::Color);
}

TEST(GraphicsAdapterTest, QueryBackBufferFormatSubstitutesNonColorFormat)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    SurfaceFormat selectedFormat;
    DepthFormat selectedDepthFormat;
    SharpRuntime::intcs selectedMultiSampleCount;

    const bool accepted = def.QueryBackBufferFormat(
        GraphicsProfile::HiDef, SurfaceFormat::Bgr565, DepthFormat::None, 0,
        selectedFormat, selectedDepthFormat, selectedMultiSampleCount);

    EXPECT_FALSE(accepted);
    EXPECT_EQ(selectedFormat, SurfaceFormat::Color);
}

// --- Headless fallback (Task 346) ---
//
// GraphicsAdapter::AdaptersChanged() falls back to a single synthetic 800x480 "Default Display"
// adapter when SDL_GetDisplays() fails. This is a genuinely reachable, not fabricated, scenario:
// confirmed via a standalone probe that SDL_GetDisplays() returns nullptr/count=0 with error
// "Video subsystem has not been initialized" whenever SDL_INIT_VIDEO hasn't been initialized yet
// - exactly the case a headless CI runner with no display server hits. Every other test in this
// file that needs SDL video explicitly balances SDL_InitSubSystem/SDL_QuitSubSystem within itself
// (see the RealDisplay_* tests above), so by gtest's sequential execution the video subsystem's
// refcount should reliably be 0 by the time this test runs, regardless of test order - if it
// isn't (something else left it initialized), this test skips rather than producing a misleading
// pass/fail. GraphicsAdapter::adapters_ is a process-wide cache, so this test restores a real
// enumeration afterward (deliberately without a matching SDL_QuitSubSystem) so later tests in
// this binary aren't left with the single synthetic adapter.

TEST(GraphicsAdapterTest, HeadlessFallback_NoVideoSubsystemProducesSingleSyntheticAdapter)
{
    if (SDL_WasInit(SDL_INIT_VIDEO) != 0)
    {
        GTEST_SKIP() << "SDL video subsystem is already initialized by something else in this "
                         "process - can't reliably exercise the enumeration-unavailable path.";
    }

    GraphicsAdapter::AdaptersChanged();

    const auto& adapters = GraphicsAdapter::getAdaptersProperty();
    ASSERT_EQ(adapters.size(), 1u);
    GraphicsAdapter& fallback = *adapters[0];

    EXPECT_EQ(fallback.getDeviceNameProperty(), "\\\\.\\DISPLAY1");
    EXPECT_EQ(fallback.getDescriptionProperty(), "Default Display");
    EXPECT_TRUE(fallback.getIsDefaultAdapterProperty());

    const DisplayModeCollection& modes = fallback.getSupportedDisplayModesProperty();
    ASSERT_EQ(modes.getCountProperty(), 1);
    EXPECT_EQ(modes[0].getWidthProperty(), 800);
    EXPECT_EQ(modes[0].getHeightProperty(), 480);
    EXPECT_EQ(modes[0].getFormatProperty(), SurfaceFormat::Color);

    const DisplayMode current = fallback.getCurrentDisplayModeProperty();
    EXPECT_EQ(current.getWidthProperty(), 800);
    EXPECT_EQ(current.getHeightProperty(), 480);

    EXPECT_NO_THROW({ (void)fallback.getMonitorHandleProperty(); });
    EXPECT_NO_THROW({ (void)fallback.getIsWideScreenProperty(); });

    if (SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GraphicsAdapter::AdaptersChanged();
    }
}

// --- GetTypeName ---

TEST(GraphicsAdapterTest, GetTypeNameReturnsExpectedString)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_EQ(def.GetTypeName(), "Microsoft.Xna.Framework.Graphics.GraphicsAdapter");
}
