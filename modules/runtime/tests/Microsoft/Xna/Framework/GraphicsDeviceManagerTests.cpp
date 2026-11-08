// SPDX-License-Identifier: MS-PL
// PLAT-56: GraphicsDeviceManager requires the selected platform/renderer combination to construct
// its ordinary hidden window. Probe that capability through IPlatform so this suite is unchanged
// across platform selections and never names a backend API.

#include <gtest/gtest.h>
#include <algorithm>

#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "System/OverflowException.hpp"
#include "System/InvalidOperationException.hpp"

#include "Microsoft/Xna/Framework/Graphics/NoSuitableGraphicsDeviceException.hpp"

#include "CNA/ProjectGraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/EventArgs.hpp"
#include "RuntimePlatformTestSupport.hpp"

using namespace Microsoft::Xna::Framework;

TEST(GraphicsDeviceManagerTest, ProjectProfileSuppliesDefaultWithoutOverridingExplicitChoice)
{
    const auto previous = CNA::GetProjectGraphicsProfileEXT();
    struct RestoreProfile
    {
        Graphics::GraphicsProfile value;
        ~RestoreProfile() { CNA::SetProjectGraphicsProfileEXT(value); }
    } restore{previous};

    CNA::SetProjectGraphicsProfileEXT(Graphics::GraphicsProfile::Reach);
    EXPECT_EQ(GraphicsDeviceManager().getGraphicsProfileProperty(),
              Graphics::GraphicsProfile::Reach);

    const CNA::ProjectGraphicsProfileEXT projectProfile{Graphics::GraphicsProfile::HiDef};
    EXPECT_EQ(CNA::GetProjectGraphicsProfileEXT(), Graphics::GraphicsProfile::HiDef);

    GraphicsDeviceManager manager;
    EXPECT_EQ(manager.getGraphicsProfileProperty(), Graphics::GraphicsProfile::HiDef);
    manager.setGraphicsProfileProperty(Graphics::GraphicsProfile::Reach);
    EXPECT_EQ(manager.getGraphicsProfileProperty(), Graphics::GraphicsProfile::Reach);
}

namespace
{
    // Runs exactly one frame then exits, giving CreateDevice() a chance to run via
    // Game::DoInitialize() without blocking in the loop.
    class OneFrameGame : public Game
    {
    public:
        int unloadCount = 0;

    protected:
        void UnloadContent() override
        {
            ++unloadCount;
            Game::UnloadContent();
        }

        void Draw(const GameTime& gameTime) override
        {
            Game::Draw(gameTime);
            Exit();
        }
    };
}

TEST(GraphicsDeviceManagerTest, CreateDeviceIsReachableAfterRun)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    OneFrameGame game;
    GraphicsDeviceManager gdm(&game);

    ASSERT_NO_THROW(game.Run());

    EXPECT_NE(gdm.getGraphicsDeviceProperty(), nullptr);
}

// Regression guard: GraphicsDeviceManager's own self-initiated ApplyChanges() path already raises
// DeviceResetting/DeviceReset correctly (unconditionally, not gated on device ownership) -- this
// must keep working and must not double-raise.
TEST(GraphicsDeviceManagerTest, ApplyChangesRaisesResettingAndResetExactlyOnce)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    OneFrameGame game;
    GraphicsDeviceManager gdm(&game);
    ASSERT_NO_THROW(game.Run());

    int resettingCount = 0;
    int resetCount = 0;
    gdm.getDeviceResettingEvent() += [&](System::Object*, const System::EventArgs&) { ++resettingCount; };
    gdm.getDeviceResetEvent() += [&](System::Object*, const System::EventArgs&) { ++resetCount; };

    gdm.setPreferredBackBufferWidthProperty(gdm.getPreferredBackBufferWidthProperty() + 16);
    gdm.ApplyChanges();

    EXPECT_EQ(resettingCount, 1);
    EXPECT_EQ(resetCount, 1);
}

// (fixed): CreateDevice() now subscribes graphicsDevice_->DeviceResetting/
// DeviceReset, forwarding to this manager's own OnDeviceResetting()/OnDeviceReset(), matching FNA's
// IGraphicsDeviceManager.CreateDevice() (graphicsDevice.DeviceResetting += OnDeviceResetting;
// graphicsDevice.DeviceReset += OnDeviceReset;). A real renderer-detected device-lost/reset
// (GraphicsDevice's own deviceEventCallback seam, currently wired up only by the D3D9 renderer)
// raises GraphicsDevice::DeviceResetting/DeviceReset directly -- simulated here by raising those
// events directly on the managed GraphicsDevice instance, which is exactly what that callback does
// on a real device-lost, without requiring D3D9 hardware.
TEST(GraphicsDeviceManagerTest, RendererDetectedDeviceLostIsForwardedToManagerListeners)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    OneFrameGame game;
    GraphicsDeviceManager gdm(&game);
    ASSERT_NO_THROW(game.Run());

    Graphics::GraphicsDevice* device = gdm.getGraphicsDeviceProperty();
    ASSERT_NE(device, nullptr);

    int resettingCount = 0;
    int resetCount = 0;
    gdm.getDeviceResettingEvent() += [&](System::Object*, const System::EventArgs&) { ++resettingCount; };
    gdm.getDeviceResetEvent() += [&](System::Object*, const System::EventArgs&) { ++resetCount; };

    // Simulates what GraphicsDevice's internal deviceEventCallback does on a real renderer-detected
    // device-lost/reset cycle -- raised directly on the device's own public events.
    device->DeviceResetting.Raise(device, System::EventArgs::Empty);
    device->DeviceReset.Raise(device, System::EventArgs::Empty);

    EXPECT_EQ(resettingCount, 1);
    EXPECT_EQ(resetCount, 1);
}

// FNA fidelity detail: GraphicsDeviceManager's OnDeviceResetting/OnDeviceReset/OnDeviceDisposing
// all re-send with `this` as sender rather than forwarding whatever sender they were called with
// (an asymmetry present in the real FNA source -- OnDeviceCreated is the one exception, which does
// forward its sender). This only became observable once forwarding from the managed GraphicsDevice
// was wired up -- previously every call site already passed `this` regardless.
TEST(GraphicsDeviceManagerTest, ForwardedDeviceEventsReportTheManagerAsSender)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    OneFrameGame game;
    GraphicsDeviceManager gdm(&game);
    ASSERT_NO_THROW(game.Run());

    Graphics::GraphicsDevice* device = gdm.getGraphicsDeviceProperty();
    ASSERT_NE(device, nullptr);

    System::Object* resettingSender = nullptr;
    System::Object* resetSender = nullptr;
    gdm.getDeviceResettingEvent() += [&](System::Object* sender, const System::EventArgs&) { resettingSender = sender; };
    gdm.getDeviceResetEvent() += [&](System::Object* sender, const System::EventArgs&) { resetSender = sender; };

    device->DeviceResetting.Raise(device, System::EventArgs::Empty);
    device->DeviceReset.Raise(device, System::EventArgs::Empty);

    EXPECT_EQ(resettingSender, static_cast<System::Object*>(&gdm));
    EXPECT_EQ(resetSender, static_cast<System::Object*>(&gdm));
}

// Regression guard: a second explicit Dispose() call must not re-raise
// DeviceDisposing: the device's disposal guard is independent of C++ deletion ownership.
TEST(GraphicsDeviceManagerTest, RepeatedDisposeDoesNotReraiseDeviceDisposing)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    OneFrameGame game;
    GraphicsDeviceManager gdm(&game);
    ASSERT_NO_THROW(game.Run());

    int disposingCount = 0;
    gdm.getDeviceDisposingEvent() += [&](System::Object*, const System::EventArgs&) { ++disposingCount; };

    gdm.Dispose();
    ASSERT_NO_THROW(gdm.Dispose());

    EXPECT_EQ(disposingCount, 1);
}

TEST(GraphicsDeviceManagerTest, DirectDeviceDisposalIsForwardedOnceAndReleasesTheFrame)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    OneFrameGame game;
    GraphicsDeviceManager gdm(&game);
    ASSERT_NO_THROW(game.Run());
    ASSERT_NO_THROW(gdm.CreateDevice());
    EXPECT_EQ(game.unloadCount, 1); // XNA forced CreateDevice disposes the prior device.
    auto* device = gdm.getGraphicsDeviceProperty();
    ASSERT_NE(device, nullptr);

    int disposingCount = 0;
    System::Object* disposingSender = nullptr;
    gdm.DeviceDisposing += [&](System::Object* sender, const System::EventArgs&)
    {
        ++disposingCount;
        disposingSender = sender;
    };

    ASSERT_TRUE(gdm.BeginDraw());
    ASSERT_NO_THROW(device->Dispose());
    EXPECT_TRUE(device->getIsDisposedProperty());
    EXPECT_EQ(disposingCount, 1);
    EXPECT_EQ(disposingSender, static_cast<System::Object*>(&gdm));
    EXPECT_EQ(game.unloadCount, 2);
    EXPECT_NO_THROW(gdm.EndDraw());

    EXPECT_NO_THROW(device->Dispose());
    EXPECT_NO_THROW(gdm.Dispose());
    EXPECT_NO_THROW(gdm.Dispose());
    EXPECT_EQ(disposingCount, 1);
    EXPECT_EQ(game.unloadCount, 2);
}

TEST(GraphicsDeviceManagerTest, DisposedManagerUnsubscribesFromTheGameOwnedDevice)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    OneFrameGame game;
    int disposingCount = 0;
    Graphics::GraphicsDevice* device = nullptr;
    {
        GraphicsDeviceManager gdm(&game);
        ASSERT_NO_THROW(game.Run());
        device = gdm.getGraphicsDeviceProperty();
        ASSERT_NE(device, nullptr);
        gdm.DeviceDisposing += [&](System::Object*, const System::EventArgs&) { ++disposingCount; };
        gdm.Dispose();
        EXPECT_EQ(disposingCount, 1);
        EXPECT_TRUE(game.getGraphicsDeviceProperty().getIsDisposedProperty());
    }

    ASSERT_NO_THROW(device->Dispose());
    EXPECT_EQ(disposingCount, 1);
    EXPECT_EQ(game.unloadCount, 1);
}

// XNA's GraphicsDevice.Viewport is the backbuffer, whatever shape the window is; the presentation
// scales it. CNA's four other presentation modes are extensions, and one of them --
// FixedHeightDynamicWidth -- widens the LOGICAL width to the window's aspect, which changes the
// coordinate space a game reads back from Viewport.Width. It was the default until 2026-09-08 and
// put Yacht's phone-shaped 480x800 layout at a reported viewport of 1067x800 on an 800x600
// window, sending every right-anchored element outside the frame the rest of the game drew.
TEST(GraphicsDeviceManagerTest, DefaultPresentationModeIsLetterboxSoTheViewportIsTheBackbuffer)
{
    OneFrameGame game;
    GraphicsDeviceManager gdm(&game);

    EXPECT_EQ(gdm.getPreferredPresentationModeProperty(), PresentationMode::Letterbox);
}

TEST(GraphicsDeviceManagerTest, PresentationModeRoundTripsSoTheExtensionsStayReachable)
{
    OneFrameGame game;
    GraphicsDeviceManager gdm(&game);

    for (const PresentationMode mode : {PresentationMode::Overscan, PresentationMode::Stretch,
                                        PresentationMode::NativeBackBuffer,
                                        PresentationMode::FixedHeightDynamicWidth,
                                        PresentationMode::Letterbox})
    {
        gdm.setPreferredPresentationModeProperty(mode);
        EXPECT_EQ(gdm.getPreferredPresentationModeProperty(), mode);
    }
}

namespace
{
    class RankingProbe : public GraphicsDeviceManager
    {
    public:
        using GraphicsDeviceManager::RankDevices;
        GraphicsDeviceInformation Find(bool anySuitable = false) { return FindBestDevice(anySuitable); }
    };

    GraphicsDeviceInformation rankedCandidate(int id, int width = 800, int height = 480,
        Graphics::SurfaceFormat format = Graphics::SurfaceFormat::Color, int samples = 0,
        bool fullscreen = false, Graphics::GraphicsProfile profile = Graphics::GraphicsProfile::Reach)
    {
        GraphicsDeviceInformation result;
        result.setGraphicsProfileProperty(profile);
        auto& pp = result.getPresentationParametersProperty();
        pp.setBackBufferWidthProperty(width);
        pp.setBackBufferHeightProperty(height);
        pp.setBackBufferFormatProperty(format);
        pp.setMultiSampleCountProperty(samples);
        pp.setIsFullScreenProperty(fullscreen);
        // The window handle is deliberately ignored by XNA's comparer; it identifies entries.
        pp.setDeviceWindowHandleProperty(SharpRuntime::IntPtr(id));
        return result;
    }

    std::vector<SharpRuntime::IntPtr> candidateIds(const std::vector<GraphicsDeviceInformation>& devices)
    {
        std::vector<SharpRuntime::IntPtr> result;
        for (const auto& device : devices)
            result.push_back(device.getPresentationParametersProperty().getDeviceWindowHandleProperty());
        return result;
    }
}

// Oracle: Refresh GraphicsDeviceInformationComparer.Compare IL_0000-0098. Hand order:
// HiDef wins even with every later key worse; within Reach the requested window mode wins,
// exact Color beats equal-depth Rgba1010102, which beats Bgr565; only then samples descend.
TEST(GraphicsDeviceRankingTest, OrdersProfilesFullscreenFormatsAndSamplesLexicographically)
{
    RankingProbe manager;
    using Graphics::GraphicsProfile;
    using Graphics::SurfaceFormat;
    std::vector<GraphicsDeviceInformation> devices{
        rankedCandidate(6, 800, 480, SurfaceFormat::Bgr565, 16),
        rankedCandidate(3, 800, 480, SurfaceFormat::Color, 0),
        rankedCandidate(5, 800, 480, SurfaceFormat::Color, 16, true),
        rankedCandidate(4, 800, 480, SurfaceFormat::Rgba1010102, 16),
        rankedCandidate(1, 1, 100, SurfaceFormat::Bgr565, 0, true, GraphicsProfile::HiDef),
        rankedCandidate(2, 800, 480, SurfaceFormat::Color, 4)};
    manager.RankDevices(devices);
    EXPECT_EQ(candidateIds(devices), (std::vector<SharpRuntime::IntPtr>{1, 2, 3, 4, 6, 5}));
    manager.setIsFullScreenProperty(true);
    manager.RankDevices(devices);
    EXPECT_EQ(candidateIds(devices), (std::vector<SharpRuntime::IntPtr>{1, 5, 2, 3, 4, 6}));
}

// Oracle: RankFormat IL_0000-002f, SurfaceFormatBitDepth IL_0000-0024. Bgra4444 is
// exact; Bgr565/Bgra5551 share 16 bits and samples break their tie; all other depths lose.
TEST(GraphicsDeviceRankingTest, RecognizesAllThreeSixteenBitFormats)
{
    RankingProbe manager;
    using Graphics::SurfaceFormat;
    manager.setPreferredBackBufferFormatProperty(SurfaceFormat::Bgra4444);
    std::vector<GraphicsDeviceInformation> devices{
        rankedCandidate(4, 800, 480, SurfaceFormat::Color, 16),
        rankedCandidate(3, 800, 480, SurfaceFormat::Bgra5551, 2),
        rankedCandidate(2, 800, 480, SurfaceFormat::Bgr565, 4),
        rankedCandidate(1, 800, 480, SurfaceFormat::Bgra4444)};
    manager.RankDevices(devices);
    EXPECT_EQ(candidateIds(devices), (std::vector<SharpRuntime::IntPtr>{1, 2, 3, 4}));
}

// Oracle: the same bit-depth IL returns ZERO for every non-whitelisted format, including
// Single (physically 32 bits) and Alpha8. Thus Alpha8 shares Vector4's rank, but Color does not.
TEST(GraphicsDeviceRankingTest, UsesTheXnaBitDepthWhitelistRatherThanPhysicalStorageSize)
{
    RankingProbe manager;
    using Graphics::SurfaceFormat;
    manager.setPreferredBackBufferFormatProperty(SurfaceFormat::Vector4);
    std::vector<GraphicsDeviceInformation> devices{
        rankedCandidate(4, 800, 480, SurfaceFormat::Color, 16),
        rankedCandidate(3, 800, 480, SurfaceFormat::Single, 2),
        rankedCandidate(2, 800, 480, SurfaceFormat::Alpha8, 4),
        rankedCandidate(1, 800, 480, SurfaceFormat::Vector4)};
    manager.RankDevices(devices);
    EXPECT_EQ(candidateIds(devices), (std::vector<SharpRuntime::IntPtr>{1, 2, 3, 4}));
}

// Oracle: Compare IL_0099-0133, 0212-024f. Preferred aspect=1 and area=10000.
// 90/80=1.125 (error .125) is within .2 of 1, so |7200-10000|=2800 beats
// |1000000-10000|=990000. 100/50=2 differs by 1, so it loses despite area error 5000.
TEST(GraphicsDeviceRankingTest, AppliesAspectToleranceBeforeAbsolutePixelAreaDistance)
{
    RankingProbe manager;
    manager.setPreferredBackBufferWidthProperty(100);
    manager.setPreferredBackBufferHeightProperty(100);
    std::vector<GraphicsDeviceInformation> devices{
        rankedCandidate(3, 100, 50), rankedCandidate(2, 1000, 1000), rankedCandidate(1, 90, 80)};
    manager.RankDevices(devices);
    EXPECT_EQ(candidateIds(devices), (std::vector<SharpRuntime::IntPtr>{1, 2, 3}));
}

// Oracle: Compare IL_0119-0128 uses ble.un with 0.2f, including equality. Preferred
// aspect=1/5=.2f, area=5; aspect errors are .2f and 0. Area errors 5 and 15 decide.
TEST(GraphicsDeviceRankingTest, ExactPointTwoAspectDifferenceFallsThroughToArea)
{
    RankingProbe manager;
    manager.setPreferredBackBufferWidthProperty(1);
    manager.setPreferredBackBufferHeightProperty(5);
    std::vector<GraphicsDeviceInformation> devices{rankedCandidate(2, 2, 10), rankedCandidate(1, 0, 1)};
    manager.RankDevices(devices);
    EXPECT_EQ(candidateIds(devices), (std::vector<SharpRuntime::IntPtr>{1, 2}));
}

// Oracle: Compare IL_0212-023e wraps Int32 multiplication. 65536^2 wraps to 0:
// |0-10000|=10000 < |40000-10000|=30000, with both candidate aspects equal to 1.
TEST(GraphicsDeviceRankingTest, PixelAreaUsesUncheckedInt32Arithmetic)
{
    RankingProbe manager;
    manager.setPreferredBackBufferWidthProperty(100);
    manager.setPreferredBackBufferHeightProperty(100);
    std::vector<GraphicsDeviceInformation> devices{rankedCandidate(2, 200, 200), rankedCandidate(1, 65536, 65536)};
    manager.RankDevices(devices);
    EXPECT_EQ(candidateIds(devices), (std::vector<SharpRuntime::IntPtr>{1, 2}));
}

// Oracle: RankDevicesPlatform IL_0000-000c only calls List.Sort, which is unstable.
// Compare IL_027c-027d returns 0 for ties: ignored fields cannot filter/deduplicate them.
// No particular permutation of equal entries is promised by the managed API.
TEST(GraphicsDeviceRankingTest, PreservesEveryTiedCandidateWithoutAddingATieKey)
{
    RankingProbe manager;
    std::vector<GraphicsDeviceInformation> devices;
    manager.RankDevices(devices);
    EXPECT_TRUE(devices.empty());
    devices.push_back(rankedCandidate(1));
    manager.RankDevices(devices);
    EXPECT_EQ(candidateIds(devices), (std::vector<SharpRuntime::IntPtr>{1}));
    for (int id = 2; id <= 24; ++id) devices.push_back(rankedCandidate(id));
    devices[1].getPresentationParametersProperty().setDepthStencilFormatProperty(Graphics::DepthFormat::None);
    devices[2].getPresentationParametersProperty().setPresentationIntervalProperty(Graphics::PresentInterval::Immediate);
    manager.RankDevices(devices);
    auto ids = candidateIds(devices);
    std::sort(ids.begin(), ids.end());
    std::vector<SharpRuntime::IntPtr> expected;
    for (int id = 1; id <= 24; ++id) expected.push_back(id);
    EXPECT_EQ(ids, expected);
}

namespace
{
    class RankingOverrideProbe final : public RankingProbe
    {
    public:
        bool called = false;
        bool filterAll = false;
        std::vector<GraphicsDeviceInformation> observed;
    protected:
        void RankDevices(std::vector<GraphicsDeviceInformation>& devices) override
        {
            called = true;
            observed = devices;
            devices.clear();
            if (!filterAll)
            {
                devices.push_back(rankedCandidate(7, 321, 123));
                devices.push_back(rankedCandidate(8, 640, 480));
            }
        }
    };
}

// Oracle: FindBestDevice IL_0000-0007 -> FindBestPlatformDevice IL_0061-0083 invokes
// RankDevices virtually, selects index 0 without sorting again, or throws after filtering all.
TEST(GraphicsDeviceRankingTest, FindBestDeviceHonorsAnOverrideAndRejectsItsEmptyResult)
{
    RankingOverrideProbe manager;
    int preparing = 0;
    manager.PreparingDeviceSettings += [&](System::Object*, const PreparingDeviceSettingsEventArgs&) { ++preparing; };
    for (bool anySuitable : {false, true})
    {
        const auto selected = manager.Find(anySuitable);
        EXPECT_TRUE(manager.called);
        ASSERT_EQ(manager.observed.size(), 1u);
        EXPECT_EQ(selected.getPresentationParametersProperty().getDeviceWindowHandleProperty(), SharpRuntime::IntPtr(7));
        EXPECT_EQ(selected.getPresentationParametersProperty().getBackBufferWidthProperty(), 321);
        manager.called = false;
    }
    manager.filterAll = true;
    EXPECT_THROW(manager.Find(), Graphics::NoSuitableGraphicsDeviceException);
    EXPECT_TRUE(manager.called);
    EXPECT_EQ(preparing, 0);
}

namespace
{
    class RankingDisplays final : public CNA::Platform::IPlatformDisplays
    {
    public:
        std::vector<CNA::Platform::DisplayInfo> GetDisplays() const override
        {
            CNA::Platform::DisplayInfo first;
            first.id = 11;
            first.name = "Ranking primary";
            first.desktopMode = {800, 480, 60.0f};
            CNA::Platform::DisplayInfo second = first;
            second.id = 22;
            second.name = "Ranking secondary";
            return {first, second};
        }
        bool TryGetDisplayForWindow(const CNA::Platform::IPlatformWindow&,
                                   CNA::Platform::DisplayInfo&) const override { return false; }
        bool TryGetSafeAreaForWindow(const CNA::Platform::IPlatformWindow&,
                                    CNA::Platform::WindowBounds&) const override { return false; }
        std::vector<CNA::Platform::DisplayMode> GetDisplayModes(std::uint32_t) const override
        { return {{800, 480, 60.0f}}; }
        bool TryGetCurrentDisplayMode(std::uint32_t, CNA::Platform::DisplayMode& mode) const override
        {
            mode = {800, 480, 60.0f};
            return true;
        }
    };

    class RankingPlatform final : public CNA::Platform::Testing::PlatformTestDecorator
    {
    public:
        CNA::Platform::IPlatformDisplays* GetDisplays() override { return &displays; }
    private:
        RankingDisplays displays;
    };
}

// Oracle: Compare IL_0250-027d uses only IsDefaultAdapter after the earlier keys tie.
// Give the secondary MORE samples to prove default-adapter preference is the final key.
TEST(GraphicsDeviceRankingTest, DefaultAdapterBreaksOnlyTheFinalTie)
{
    RankingPlatform platform;
    CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);
    Graphics::GraphicsAdapter::AdaptersChanged();
    struct ClearAdapters
    {
        ~ClearAdapters() { Graphics::GraphicsAdapter::AdaptersChanged(); }
    } clearAdapters;
    const auto& adapters = Graphics::GraphicsAdapter::getAdaptersProperty();
    ASSERT_EQ(adapters.size(), 2u);
    auto primary = rankedCandidate(2);
    primary.setAdapterProperty(adapters[0].get());
    auto secondary = rankedCandidate(3);
    secondary.setAdapterProperty(adapters[1].get());
    auto multisampled = rankedCandidate(1, 800, 480, Graphics::SurfaceFormat::Color, 4);
    multisampled.setAdapterProperty(adapters[1].get());
    std::vector<GraphicsDeviceInformation> devices{secondary, primary, multisampled};
    RankingProbe manager;
    manager.RankDevices(devices);
    EXPECT_EQ(candidateIds(devices), (std::vector<SharpRuntime::IntPtr>{1, 2, 3}));
}

// Oracle: Compare IL_0212-023e wraps subtraction before Math.Abs(Int32), whose
// Int32.MinValue refusal is OverflowException. Preferred 65536*32768 wraps to MIN;
// 0*1 - MIN wraps to MIN too. Aspects are both zero for the two candidates.
TEST(GraphicsDeviceRankingTest, MinimumInt32AreaDifferenceRefusesInsteadOfOverflowingCpp)
{
    RankingProbe manager;
    manager.setPreferredBackBufferWidthProperty(65536);
    manager.setPreferredBackBufferHeightProperty(32768);
    std::vector<GraphicsDeviceInformation> devices{rankedCandidate(1, 0, 1), rankedCandidate(2, 0, 1)};
    // CLR 4.0 ArraySortHelper<T>.Sort IL_004b-005c wraps the Math.Abs failure.
    try
    {
        manager.RankDevices(devices);
        FAIL() << "Expected a wrapped Math.Abs(Int32.MinValue) failure";
    }
    catch (const System::InvalidOperationException& error)
    {
        ASSERT_TRUE(error.getInnerExceptionProperty());
        EXPECT_THROW(std::rethrow_exception(error.getInnerExceptionProperty()), System::OverflowException);
    }
}

// Oracle: CLR 4.0.30319.1 ArraySortHelper<T>.QuickSort IL_000c-002e leaves
// three equal entries unchanged during pivot selection; IL_0061-008f swaps 0 and 2.
TEST(GraphicsDeviceRankingTest, ClrFourPartitioningCanReverseEqualEntries)
{
    RankingProbe manager;
    std::vector<GraphicsDeviceInformation> devices{rankedCandidate(1), rankedCandidate(2), rankedCandidate(3)};
    // XNA Compare never reads depth, presentation interval or orientation: they remain ties.
    devices[0].getPresentationParametersProperty().setDepthStencilFormatProperty(Graphics::DepthFormat::Depth24Stencil8);
    devices[1].getPresentationParametersProperty().setPresentationIntervalProperty(Graphics::PresentInterval::Immediate);
    devices[2].getPresentationParametersProperty().setDisplayOrientationProperty(DisplayOrientation::Portrait);
    manager.RankDevices(devices);
    EXPECT_EQ(candidateIds(devices), (std::vector<SharpRuntime::IntPtr>{3, 2, 1}));
}

// Oracle: XNA Compare IL_0119-0133 and CLR QuickSort IL_000c-008f. Preferred=1,
// area=10000. A=(200,200): error0, area delta30000; B=(90,80): .125,2800;
// C=(100,80): .25,2000. B<A, C<B by area but A<C by aspect: a comparison cycle.
// Median swaps [A,B,C] -> [B,A,C] -> [C,A,B] -> [C,B,A]; scans leave it there.
TEST(GraphicsDeviceRankingTest, ReproducesClrPartitioningForTheNontransitiveAspectTolerance)
{
    RankingProbe manager;
    manager.setPreferredBackBufferWidthProperty(100);
    manager.setPreferredBackBufferHeightProperty(100);
    std::vector<GraphicsDeviceInformation> devices{
        rankedCandidate(1, 200, 200), rankedCandidate(2, 90, 80), rankedCandidate(3, 100, 80)};
    manager.RankDevices(devices);
    EXPECT_EQ(candidateIds(devices), (std::vector<SharpRuntime::IntPtr>{3, 2, 1}));
}
