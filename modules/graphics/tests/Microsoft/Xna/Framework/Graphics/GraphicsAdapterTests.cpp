// SPDX-License-Identifier: MS-PL
// Task 345: audit GraphicsAdapter API against FNA.
//
// PLAT-63: adapter enumeration is tested through a deterministic IPlatformDisplays. The test
// fixture installs that service as the current platform, refreshes the process-wide adapter cache,
// and restores both the platform and cache after every test.

#include <gtest/gtest.h>

#include "CNA/Platform/PlatformTestDecorator.hpp"

#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DisplayMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DisplayModeCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using CNA::Platform::DisplayInfo;
using CNA::Platform::IPlatformDisplays;
using CNA::Platform::IPlatformWindow;
using CNA::Platform::ResetCurrentPlatform;
using CNA::Platform::WindowBounds;
using CNA::Platform::Testing::PlatformTestDecorator;
using CNA::Platform::Testing::ScopedCurrentPlatform;

using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DisplayMode;
using Microsoft::Xna::Framework::Graphics::DisplayModeCollection;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

namespace {

class CannedDisplays final : public IPlatformDisplays
{
public:
    std::vector<DisplayInfo> displays;
    std::vector<CNA::Platform::DisplayMode> primaryModes;
    CNA::Platform::DisplayMode primaryCurrent;
    CNA::Platform::DisplayMode secondaryCurrent;
    bool currentModesAvailable = true;

    [[nodiscard]] std::vector<DisplayInfo> GetDisplays() const override { return displays; }

    [[nodiscard]] bool TryGetDisplayForWindow(
        const IPlatformWindow&, DisplayInfo&) const override
    {
        return false;
    }

    [[nodiscard]] bool TryGetSafeAreaForWindow(
        const IPlatformWindow&, WindowBounds&) const override
    {
        return false;
    }

    [[nodiscard]] std::vector<CNA::Platform::DisplayMode> GetDisplayModes(
        const std::uint32_t displayId) const override
    {
        return displayId == 0xA1u ? primaryModes : std::vector<CNA::Platform::DisplayMode>{};
    }

    [[nodiscard]] bool TryGetCurrentDisplayMode(
        const std::uint32_t displayId, CNA::Platform::DisplayMode& mode) const override
    {
        if (!currentModesAvailable)
        {
            return false;
        }
        if (displayId == 0xA1u)
        {
            mode = primaryCurrent;
            return true;
        }
        if (displayId == 0xB2u)
        {
            mode = secondaryCurrent;
            return true;
        }
        return false;
    }
};

class CannedDisplayPlatform final : public PlatformTestDecorator
{
public:
    explicit CannedDisplayPlatform(CannedDisplays& displays) : displays_(displays) {}

    bool exposeDisplays = true;

    [[nodiscard]] IPlatformDisplays* GetDisplays() override
    {
        return exposeDisplays ? &displays_ : nullptr;
    }

private:
    CannedDisplays& displays_;
};

class GraphicsAdapterTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ResetCurrentPlatform();
        GraphicsAdapter::AdaptersChanged();
    }

    void TearDown() override
    {
        ResetCurrentPlatform();
    }
};

class GraphicsAdapterPlatformTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ResetCurrentPlatform();

        DisplayInfo primary;
        primary.id = 0xA1u;
        primary.name = "Primary Panel";
        primary.width = 1920;
        primary.height = 1080;
        primary.desktopMode = {1920, 1080, 60.0f};

        DisplayInfo secondary;
        secondary.id = 0xB2u;
        secondary.width = 1024;
        secondary.height = 768;
        secondary.desktopMode = {1024, 768, 60.0f};

        displays_.displays = {primary, secondary};
        displays_.primaryModes = {
            {1920, 1080, 60.0f},
            {1280, 720, 60.0f},
            {1920, 1080, 144.0f},
            {800, 600, 60.0f}};
        displays_.primaryCurrent = {1600, 900, 75.0f};
        displays_.secondaryCurrent = {1024, 768, 60.0f};

        installed_ = std::make_unique<ScopedCurrentPlatform>(platform_);
        GraphicsAdapter::AdaptersChanged();
    }

    void TearDown() override
    {
        installed_.reset();
        GraphicsAdapter::AdaptersChanged();
        ResetCurrentPlatform();
    }

    CannedDisplays displays_;
    CannedDisplayPlatform platform_{displays_};
    std::unique_ptr<ScopedCurrentPlatform> installed_;
};

} // namespace

// --- Adapters / DefaultAdapter ---

TEST_F(GraphicsAdapterTest, AdaptersIsNonEmpty)
{
    const auto& adapters = GraphicsAdapter::getAdaptersProperty();
    EXPECT_FALSE(adapters.empty());
}

TEST_F(GraphicsAdapterTest, DefaultAdapterIsAdaptersFirstEntry)
{
    const auto& adapters = GraphicsAdapter::getAdaptersProperty();
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_EQ(&def, adapters[0].get());
}

TEST_F(GraphicsAdapterTest, DefaultAdapterIsDefaultAdapterPropertyTrue)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_TRUE(def.getIsDefaultAdapterProperty());
}

TEST_F(GraphicsAdapterTest, DefaultAdapterRemainsValidAcrossAdaptersChanged)
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

TEST_F(GraphicsAdapterTest, DescriptionAndDeviceNameAreNonEmpty)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_FALSE(def.getDescriptionProperty().empty());
    EXPECT_FALSE(def.getDeviceNameProperty().empty());
}

TEST_F(GraphicsAdapterPlatformTest, PlatformDisplaysDriveNamesIdsAndCurrentModes)
{
    const auto& adapters = GraphicsAdapter::getAdaptersProperty();
    ASSERT_EQ(adapters.size(), 2u);

    EXPECT_EQ(adapters[0]->getDeviceNameProperty(), "\\\\.\\DISPLAY1");
    EXPECT_EQ(adapters[0]->getDescriptionProperty(), "Primary Panel");
    EXPECT_EQ(adapters[0]->getMonitorHandleProperty(), 0xA1u);
    EXPECT_TRUE(adapters[0]->getIsDefaultAdapterProperty());

    const DisplayMode current = adapters[0]->getCurrentDisplayModeProperty();
    EXPECT_EQ(current.getWidthProperty(), 1600);
    EXPECT_EQ(current.getHeightProperty(), 900);

    EXPECT_EQ(adapters[1]->getDeviceNameProperty(), "\\\\.\\DISPLAY2");
    EXPECT_EQ(adapters[1]->getDescriptionProperty(), "Display 1");
    EXPECT_EQ(adapters[1]->getMonitorHandleProperty(), 0xB2u);
}

// --- SupportedDisplayModes / CurrentDisplayMode ---

TEST_F(GraphicsAdapterTest, SupportedDisplayModesIsNonEmpty)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    const DisplayModeCollection& modes = def.getSupportedDisplayModesProperty();
    EXPECT_GT(modes.getCountProperty(), 0);
}

TEST_F(GraphicsAdapterPlatformTest, ModesKeepReverseOrderAndDeduplicateRefreshRates)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    const DisplayModeCollection& modes = def.getSupportedDisplayModesProperty();

    ASSERT_EQ(modes.getCountProperty(), 3);
    EXPECT_EQ(modes[0].getWidthProperty(), 800);
    EXPECT_EQ(modes[0].getHeightProperty(), 600);
    EXPECT_EQ(modes[1].getWidthProperty(), 1920);
    EXPECT_EQ(modes[1].getHeightProperty(), 1080);
    EXPECT_EQ(modes[2].getWidthProperty(), 1280);
    EXPECT_EQ(modes[2].getHeightProperty(), 720);

    for (SharpRuntime::intcs i = 0; i < modes.getCountProperty(); ++i)
    {
        for (SharpRuntime::intcs j = i + 1; j < modes.getCountProperty(); ++j)
        {
            const bool sameSize = modes[i].getWidthProperty() == modes[j].getWidthProperty() &&
                                   modes[i].getHeightProperty() == modes[j].getHeightProperty();
            EXPECT_FALSE(sameSize) << "duplicate mode at indices " << i << " and " << j;
        }
    }
}

TEST_F(GraphicsAdapterPlatformTest, ADisplayWithNoModeListUsesItsCurrentMode)
{
    const auto& adapters = GraphicsAdapter::getAdaptersProperty();
    ASSERT_EQ(adapters.size(), 2u);

    const DisplayModeCollection& modes = adapters[1]->getSupportedDisplayModesProperty();
    ASSERT_EQ(modes.getCountProperty(), 1);
    EXPECT_EQ(modes[0].getWidthProperty(), 1024);
    EXPECT_EQ(modes[0].getHeightProperty(), 768);
}

TEST_F(GraphicsAdapterPlatformTest, AnUnavailableCurrentModeUsesTheHistoricalFallback)
{
    displays_.currentModesAvailable = false;

    const DisplayMode mode = GraphicsAdapter::getDefaultAdapterProperty().getCurrentDisplayModeProperty();
    EXPECT_EQ(mode.getWidthProperty(), 800);
    EXPECT_EQ(mode.getHeightProperty(), 480);
}

TEST_F(GraphicsAdapterTest, CurrentDisplayModeHasPositiveDimensions)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    DisplayMode mode = def.getCurrentDisplayModeProperty();
    EXPECT_GT(mode.getWidthProperty(), 0);
    EXPECT_GT(mode.getHeightProperty(), 0);
}

// --- IsWideScreen / MonitorHandle ---

TEST_F(GraphicsAdapterTest, IsWideScreenDoesNotThrow)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_NO_THROW({ (void)def.getIsWideScreenProperty(); });
}

TEST_F(GraphicsAdapterTest, MonitorHandleDoesNotThrow)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_NO_THROW({ (void)def.getMonitorHandleProperty(); });
}

// --- DeviceId / Revision / SubSystemId / VendorId ---
//
// FNA always throws NotImplementedException for all 4. CNA deliberately deviates: DeviceId and
// VendorId query the real PCI ID via sysfs on Linux (returning 0 elsewhere/on failure); Revision
// and SubSystemId are not exposed by the platform at all and always return 0. None throw.

TEST_F(GraphicsAdapterTest, DeviceIdAndVendorIdDoNotThrow)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_NO_THROW({ (void)def.getDeviceIdProperty(); });
    EXPECT_NO_THROW({ (void)def.getVendorIdProperty(); });
}

TEST_F(GraphicsAdapterTest, RevisionIsAlwaysZero)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_EQ(def.getRevisionProperty(), 0);
}

TEST_F(GraphicsAdapterTest, SubSystemIdIsAlwaysZero)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_EQ(def.getSubSystemIdProperty(), 0);
}

// --- UseNullDevice / UseReferenceDevice ---

TEST_F(GraphicsAdapterTest, UseNullDeviceRoundTrip)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    const bool original = def.getUseNullDeviceProperty();

    def.setUseNullDeviceProperty(true);
    EXPECT_TRUE(def.getUseNullDeviceProperty());
    def.setUseNullDeviceProperty(false);
    EXPECT_FALSE(def.getUseNullDeviceProperty());

    def.setUseNullDeviceProperty(original);
}

TEST_F(GraphicsAdapterTest, UseReferenceDeviceRoundTrip)
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

TEST_F(GraphicsAdapterTest, IsProfileSupportedReachIsTrue)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_TRUE(def.IsProfileSupported(GraphicsProfile::Reach));
}

TEST_F(GraphicsAdapterTest, IsProfileSupportedHiDefIsTrue)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_TRUE(def.IsProfileSupported(GraphicsProfile::HiDef));
}

// --- QueryRenderTargetFormat ---

TEST_F(GraphicsAdapterTest, QueryRenderTargetFormatAcceptsSupportedFormat)
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

TEST_F(GraphicsAdapterTest, QueryRenderTargetFormatSubstitutesUnsupportedFormat)
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

TEST_F(GraphicsAdapterTest, QueryBackBufferFormatAcceptsColor)
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

TEST_F(GraphicsAdapterTest, QueryBackBufferFormatSubstitutesNonColorFormat)
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

// --- Display-less fallback (Task 346 / PLAT-63) ---

TEST_F(GraphicsAdapterPlatformTest, EmptyEnumerationProducesSingleSyntheticAdapter)
{
    displays_.displays.clear();
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

}

TEST_F(GraphicsAdapterPlatformTest, MissingDisplayServiceProducesSingleSyntheticAdapter)
{
    platform_.exposeDisplays = false;
    GraphicsAdapter::AdaptersChanged();

    const auto& adapters = GraphicsAdapter::getAdaptersProperty();
    ASSERT_EQ(adapters.size(), 1u);
    EXPECT_EQ(adapters[0]->getDeviceNameProperty(), "\\\\.\\DISPLAY1");
    EXPECT_EQ(adapters[0]->getDescriptionProperty(), "Default Display");
    EXPECT_EQ(adapters[0]->getMonitorHandleProperty(), 0u);
}

// --- GetTypeName ---

TEST_F(GraphicsAdapterTest, GetTypeNameReturnsExpectedString)
{
    GraphicsAdapter& def = GraphicsAdapter::getDefaultAdapterProperty();
    EXPECT_EQ(def.GetTypeName(), "Microsoft.Xna.Framework.Graphics.GraphicsAdapter");
}
