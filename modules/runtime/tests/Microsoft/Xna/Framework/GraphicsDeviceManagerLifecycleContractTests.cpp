// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "RuntimePlatformTestSupport.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;

TEST(GraphicsDeviceManagerLifecycleContractTest, ExplicitDisposeReleasesGameOwnedDeviceBeforeDisposed)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
        GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
    Game game;
    GraphicsDeviceManager manager(&game);
    manager.CreateDevice();
    auto* const device = manager.getGraphicsDeviceProperty();
    ASSERT_NE(device, nullptr);
    std::vector<std::string> trace;
    manager.DeviceDisposing += [&](System::Object* sender, const System::EventArgs&)
    {
        EXPECT_EQ(sender, &manager);
        EXPECT_TRUE(device->getIsDisposedProperty());
        EXPECT_EQ(manager.getGraphicsDeviceProperty(), device);
        trace.emplace_back("device");
    };
    manager.Disposed += [&](System::Object* sender, const System::EventArgs&)
    {
        EXPECT_EQ(sender, &manager);
        EXPECT_TRUE(device->getIsDisposedProperty());
        EXPECT_EQ(manager.getGraphicsDeviceProperty(), nullptr);
        EXPECT_EQ(game.getServicesProperty().GetService<Graphics::IGraphicsDeviceService>(), nullptr);
        trace.emplace_back("manager");
    };
    manager.Dispose();
    manager.Dispose();
    EXPECT_EQ(trace, (std::vector<std::string>{"device", "manager", "manager"}));
}

namespace
{
    class DeviceSelectionProbe final : public GraphicsDeviceManager
    {
    public:
        using GraphicsDeviceManager::GraphicsDeviceManager;
        bool CanReset(const GraphicsDeviceInformation& information) { return CanResetDevice(information); }
        GraphicsDeviceInformation FindBest(bool anySuitableDevice) { return FindBestDevice(anySuitableDevice); }
    };
}

// TQ-030. XNA CanResetDevice IL_0000-0016 answers whether the existing device already has the
// candidate's GraphicsProfile; ChangeDevice IL_00c4-00cc resets only then and re-creates otherwise.
TEST(GraphicsDeviceManagerLifecycleContractTest, CanResetDeviceRequiresTheDevicesProfile)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
        GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
    Game game;
    DeviceSelectionProbe manager(&game);
    manager.CreateDevice();
    auto* const device = manager.getGraphicsDeviceProperty();
    ASSERT_NE(device, nullptr);
    const Graphics::GraphicsProfile current = device->getGraphicsProfileProperty();
    GraphicsDeviceInformation same;
    same.setGraphicsProfileProperty(current);
    GraphicsDeviceInformation changed;
    changed.setGraphicsProfileProperty(current == Graphics::GraphicsProfile::Reach
                                           ? Graphics::GraphicsProfile::HiDef
                                           : Graphics::GraphicsProfile::Reach);
    EXPECT_TRUE(manager.CanReset(same));
    EXPECT_FALSE(manager.CanReset(changed));
    manager.Dispose();
}

// TQ-030. XNA FindBestDevice IL_0000-0007 -> FindBestPlatformDevice IL_0000-0083 only enumerates
// and ranks: AddDevices(bool, List) IL_004d-00a1 sets the profile, the game window's handle,
// IsFullScreen and the PresentInterval, AddDevices(adapter, mode, ...) IL_005f-00cc the windowed
// size, the depth format and multisample count 0. Nothing on that path calls
// OnPreparingDeviceSettings; only ChangeDevice IL_00bf and CreateDevice IL_0022 raise it.
TEST(GraphicsDeviceManagerLifecycleContractTest, FindBestDeviceDescribesThePreferencesWithoutPreparingSettings)
{
    Game game;
    DeviceSelectionProbe manager(&game);
    manager.setSynchronizeWithVerticalRetraceProperty(false);
    manager.setPreferredDepthStencilFormatProperty(Graphics::DepthFormat::Depth16);
    manager.setPreferredBackBufferWidthProperty(1024);
    manager.setPreferredBackBufferHeightProperty(576);
    int prepared = 0;
    manager.PreparingDeviceSettings += [&](System::Object*, const PreparingDeviceSettingsEventArgs&) { ++prepared; };

    const GraphicsDeviceInformation best = manager.FindBest(false);

    EXPECT_EQ(prepared, 0);
    EXPECT_NE(best.getAdapterProperty(), nullptr);
    EXPECT_EQ(best.getGraphicsProfileProperty(), manager.getGraphicsProfileProperty());
    const Graphics::PresentationParameters& parameters = best.getPresentationParametersProperty();
    EXPECT_EQ(parameters.getBackBufferWidthProperty(), 1024);
    EXPECT_EQ(parameters.getBackBufferHeightProperty(), 576);
    EXPECT_FALSE(parameters.getIsFullScreenProperty());
    EXPECT_EQ(parameters.getPresentationIntervalProperty(), Graphics::PresentInterval::Immediate);
    EXPECT_EQ(parameters.getMultiSampleCountProperty(), 0);
    EXPECT_EQ(parameters.getDepthStencilFormatProperty(), Graphics::DepthFormat::Depth16);
    EXPECT_EQ(parameters.getDeviceWindowHandleProperty(), game.getWindowProperty().getHandleProperty());
}

TEST(GraphicsDeviceManagerLifecycleContractTest, ReentrantAndThrowingDisposedHandlersCanBeInvokedAgain)
{
    GraphicsDeviceManager manager;
    int notifications = 0;
    manager.Disposed += [&](System::Object*, const System::EventArgs&)
    {
        ++notifications;
        if (notifications == 1) manager.Dispose();
        if (notifications == 2) throw std::runtime_error("disposed callback refusal");
    };
    EXPECT_THROW(manager.Dispose(), std::runtime_error);
    EXPECT_EQ(notifications, 2);
    EXPECT_NO_THROW(manager.Dispose());
    EXPECT_EQ(notifications, 3);
}

TEST(GraphicsDeviceManagerLifecycleContractTest, ReentrantDeviceDisposalCompletesInnerAndOuterNotifications)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
        GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
    Game game;
    GraphicsDeviceManager manager(&game);
    manager.CreateDevice();
    auto* const device = manager.getGraphicsDeviceProperty();
    ASSERT_NE(device, nullptr);
    std::vector<std::string> trace;
    manager.DeviceDisposing += [&](System::Object*, const System::EventArgs&)
    {
        trace.emplace_back("device-enter");
        manager.Dispose();
        EXPECT_EQ(manager.getGraphicsDeviceProperty(), nullptr);
        EXPECT_TRUE(device->getIsDisposedProperty());
        trace.emplace_back("device-leave");
    };
    manager.Disposed += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("manager"); };
    manager.Dispose();
    EXPECT_EQ(trace, (std::vector<std::string>{"device-enter", "manager", "device-leave", "manager"}));
    trace.clear();
    manager.Dispose();
    EXPECT_EQ(trace, (std::vector<std::string>{"manager"}));
}

TEST(GraphicsDeviceManagerLifecycleContractTest, DestructorDoesNotRaiseManagedDisposedEvent)
{
    int notifications = 0;
    {
        GraphicsDeviceManager manager;
        manager.Disposed += [&](System::Object*, const System::EventArgs&) { ++notifications; };
    }
    EXPECT_EQ(notifications, 0);
}


namespace {
class RecreatingGame final : public Game {
public:
    std::vector<std::string> trace;
    std::vector<Graphics::GraphicsDevice*> loadedDevices;
protected:
    void LoadContent() override {
        trace.emplace_back("load");
        loadedDevices.push_back(&getGraphicsDeviceProperty());
    }
    void UnloadContent() override { trace.emplace_back("unload"); }
};
}

// XNA ChangeDevice IL_00b6-0120 and CreateDevice IL_0000-00eb. The reset path
// prepares once; profile changes prepare twice; forced creation disposes before preparing.
TEST(GraphicsDeviceManagerLifecycleContractTest, ProfileChangeAndForcedCreateReplaceDeviceWithXnaTrace)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow()) GTEST_SKIP();
    RecreatingGame game;
    GraphicsDeviceManager manager(&game);
    game.setIsFixedTimeStepProperty(false);
    ASSERT_TRUE(game.RunFrameEXT());
    auto& trace = game.trace;
    auto* original = manager.getGraphicsDeviceProperty();
    ASSERT_NE(original, nullptr);
    const auto other = original->getGraphicsProfileProperty() == Graphics::GraphicsProfile::Reach
        ? Graphics::GraphicsProfile::HiDef : Graphics::GraphicsProfile::Reach;
    if (!original->getAdapterProperty().IsProfileSupported(other)) GTEST_SKIP();
    const auto window = game.getWindowProperty().getHandleProperty();
    manager.PreparingDeviceSettings += [&](System::Object*, const PreparingDeviceSettingsEventArgs&) { trace.emplace_back("prepare"); };
    manager.DeviceDisposing += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("disposing"); };
    manager.DeviceCreated += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("created"); };
    manager.DeviceResetting += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("resetting"); };
    manager.DeviceReset += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("reset"); };
    trace.clear();
    manager.setGraphicsProfileProperty(other);
    manager.ApplyChanges();
    EXPECT_EQ(trace, (std::vector<std::string>{"prepare", "unload", "disposing", "prepare", "load", "created"}));
    auto* replacement = manager.getGraphicsDeviceProperty();
    ASSERT_NE(replacement, nullptr);
    EXPECT_NE(replacement, original);
    EXPECT_TRUE(original->getIsDisposedProperty());
    EXPECT_FALSE(replacement->getIsDisposedProperty());
    EXPECT_EQ(replacement->getGraphicsProfileProperty(), other);
    EXPECT_EQ(&game.getGraphicsDeviceProperty(), replacement);
    EXPECT_EQ(game.loadedDevices, (std::vector<Graphics::GraphicsDevice*>{original, replacement}));
    EXPECT_EQ(game.getWindowProperty().getHandleProperty(), window);

    trace.clear();
    manager.ApplyChanges();
    EXPECT_TRUE(trace.empty());
    manager.setPreferredBackBufferWidthProperty(640);
    manager.ApplyChanges();
    EXPECT_EQ(trace, (std::vector<std::string>{"prepare", "resetting", "reset"}));
    EXPECT_EQ(manager.getGraphicsDeviceProperty(), replacement);
    EXPECT_EQ(replacement->getPresentationParametersProperty().getBackBufferWidthProperty(), 640);

    trace.clear();
    manager.CreateDevice();
    EXPECT_EQ(trace, (std::vector<std::string>{"unload", "disposing", "prepare", "load", "created"}));
    auto* forced = manager.getGraphicsDeviceProperty();
    ASSERT_NE(forced, nullptr);
    EXPECT_NE(forced, replacement);
    EXPECT_TRUE(replacement->getIsDisposedProperty());
    EXPECT_FALSE(forced->getIsDisposedProperty());
    EXPECT_EQ(forced->getGraphicsProfileProperty(), other);
    EXPECT_EQ(&game.getGraphicsDeviceProperty(), forced);
    EXPECT_EQ(game.loadedDevices, (std::vector<Graphics::GraphicsDevice*>{original, replacement, forced}));
    EXPECT_EQ(game.getWindowProperty().getHandleProperty(), window);
    trace.clear();
    original->DeviceReset.Raise(original, System::EventArgs::Empty);
    replacement->DeviceReset.Raise(replacement, System::EventArgs::Empty);
    EXPECT_TRUE(trace.empty());
    forced->DeviceReset.Raise(forced, System::EventArgs::Empty);
    EXPECT_EQ(trace, (std::vector<std::string>{"reset"}));
    manager.Dispose(); // All captured storage still lives.
}

// XNA CreateDevice IL_0000-0022 disposes before preparation, even if preparation throws.
TEST(GraphicsDeviceManagerLifecycleContractTest, RefusedForcedCreationLeavesOldDeviceDisposedAndCanRetry)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow()) GTEST_SKIP();
    Game game;
    GraphicsDeviceManager manager(&game);
    manager.CreateDevice();
    auto* old = manager.getGraphicsDeviceProperty();
    ASSERT_NE(old, nullptr);
    auto token = manager.PreparingDeviceSettings.Add([](System::Object*, const PreparingDeviceSettingsEventArgs&) {
        throw std::runtime_error("creation refused");
    });
    EXPECT_THROW(manager.CreateDevice(), std::runtime_error);
    EXPECT_TRUE(old->getIsDisposedProperty());
    EXPECT_EQ(manager.getGraphicsDeviceProperty(), nullptr);
    manager.PreparingDeviceSettings.Remove(token);
    manager.CreateDevice();
    ASSERT_NE(manager.getGraphicsDeviceProperty(), nullptr);
    EXPECT_NE(manager.getGraphicsDeviceProperty(), old);
    EXPECT_FALSE(manager.getGraphicsDeviceProperty()->getIsDisposedProperty());
    manager.Dispose();
}

// XNA ChangeDevice(true) IL_00ab-00ac skips the reset path; CreateDevice IL_0000-00eb
// disposes/prepares/constructs/notifies even when the profile is unchanged.
TEST(GraphicsDeviceManagerLifecycleContractTest, ForcedCreateReplacesDeviceWithoutAProfileChange)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow()) GTEST_SKIP();
    RecreatingGame game;
    GraphicsDeviceManager manager(&game);
    game.setIsFixedTimeStepProperty(false);
    ASSERT_TRUE(game.RunFrameEXT());
    auto* original = manager.getGraphicsDeviceProperty();
    ASSERT_NE(original, nullptr);
    const auto profile = original->getGraphicsProfileProperty();
    auto& trace = game.trace;
    manager.PreparingDeviceSettings += [&](System::Object*, const PreparingDeviceSettingsEventArgs& args) {
        trace.emplace_back("prepare");
        args.getGraphicsDeviceInformationEXT().getPresentationParametersProperty().setBackBufferWidthProperty(640);
    };
    manager.DeviceDisposing += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("disposing"); };
    manager.DeviceCreated += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("created"); };
    manager.DeviceResetting += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("resetting"); };
    manager.DeviceReset += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("reset"); };
    trace.clear();
    manager.CreateDevice();
    EXPECT_EQ(trace, (std::vector<std::string>{"unload", "disposing", "prepare", "load", "created"}));
    auto* replacement = manager.getGraphicsDeviceProperty();
    ASSERT_NE(replacement, nullptr);
    EXPECT_NE(replacement, original);
    EXPECT_TRUE(original->getIsDisposedProperty());
    EXPECT_FALSE(replacement->getIsDisposedProperty());
    EXPECT_EQ(replacement->getGraphicsProfileProperty(), profile);
    EXPECT_EQ(replacement->getPresentationParametersProperty().getBackBufferWidthProperty(), 640);
    manager.Dispose();
}

TEST(GraphicsDeviceManagerLifecycleContractTest, UnmanagedHookPreservesDeviceAndServicesForExplicitDispose)
{
    class FinalizerProbe final : public GraphicsDeviceManager
    {
    public:
        using GraphicsDeviceManager::GraphicsDeviceManager;
        void FinalizerPath() { Dispose(false); }
    };
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
        GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
    Game game;
    FinalizerProbe manager(&game);
    manager.CreateDevice();
    auto* const device = manager.getGraphicsDeviceProperty();
    ASSERT_NE(device, nullptr);
    std::vector<std::string> trace;
    manager.DeviceDisposing += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("device"); };
    manager.Disposed += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("manager"); };
    manager.FinalizerPath();
    EXPECT_TRUE(trace.empty());
    EXPECT_EQ(manager.getGraphicsDeviceProperty(), device);
    EXPECT_FALSE(device->getIsDisposedProperty());
    EXPECT_EQ(game.getServicesProperty().GetService<Graphics::IGraphicsDeviceService>(), &manager);
    EXPECT_FALSE(game.getWindowProperty().ClientSizeChanged.Empty());
    manager.Dispose();
    EXPECT_EQ(trace, (std::vector<std::string>{"device", "manager"}));
}

TEST(GraphicsDeviceManagerLifecycleContractTest, DestructionDetachesWindowCallbackAndPreservesReplacementServices)
{
    Game game;
    EXPECT_TRUE(game.getWindowProperty().ClientSizeChanged.Empty());
    GraphicsDeviceManager replacement;
    {
        GraphicsDeviceManager manager(&game);
        EXPECT_FALSE(game.getWindowProperty().ClientSizeChanged.Empty());
        game.getServicesProperty().RemoveService<Graphics::IGraphicsDeviceService>();
        game.getServicesProperty().RemoveService<IGraphicsDeviceManager>();
        game.getServicesProperty().AddService<Graphics::IGraphicsDeviceService>(&replacement);
        game.getServicesProperty().AddService<IGraphicsDeviceManager>(&replacement);
        // XNA Dispose(bool) IL_0011-0043 only removes the service if it is still this manager.
        manager.Dispose();
        EXPECT_EQ(game.getServicesProperty().GetService<Graphics::IGraphicsDeviceService>(), &replacement);
        EXPECT_EQ(game.getServicesProperty().GetService<IGraphicsDeviceManager>(), &replacement);
    }
    EXPECT_TRUE(game.getWindowProperty().ClientSizeChanged.Empty());
    EXPECT_EQ(game.getServicesProperty().GetService<Graphics::IGraphicsDeviceService>(), &replacement);
    EXPECT_EQ(game.getServicesProperty().GetService<IGraphicsDeviceManager>(), &replacement);
}

// TQ-030. XNA GraphicsDeviceManager..ctor IL_002a-003c refuses a null game with
// ArgumentNullException("game", Resources.GameCannotBeNull = "Game cannot be null.").
TEST(GraphicsDeviceManagerLifecycleContractTest, NullGameIsRefusedWithTheXnaParameterAndMessage)
{
    try
    {
        GraphicsDeviceManager manager(nullptr);
        FAIL() << "A null game was accepted";
    }
    catch (const System::ArgumentNullException& exception)
    {
        EXPECT_EQ(exception.getParamNameProperty(), "game");
        EXPECT_TRUE(exception.getMessageProperty().starts_with("Game cannot be null."))
            << exception.getMessageProperty();
    }
}

// TQ-030. ..ctor IL_0044-0065 refuses a second manager with ArgumentException(
// Resources.GraphicsDeviceManagerAlreadyPresent) before IL_0066-00d2 register a service or hook a
// window event, so the registered manager keeps its services and its window subscription.
TEST(GraphicsDeviceManagerLifecycleContractTest, ASecondManagerIsRefusedWithoutDetachingTheFirst)
{
    Game game;
    GraphicsDeviceManager first(&game);
    ASSERT_FALSE(game.getWindowProperty().ClientSizeChanged.Empty());
    try
    {
        GraphicsDeviceManager second(&game);
        FAIL() << "A second manager was accepted";
    }
    catch (const System::ArgumentException& exception)
    {
        EXPECT_TRUE(exception.getParamNameProperty().empty());
        EXPECT_TRUE(exception.getMessageProperty().starts_with(
            "A graphics device manager is already registered.  The graphics device manager "
            "cannot be changed once it is set."))
            << exception.getMessageProperty();
    }
    EXPECT_FALSE(game.getWindowProperty().ClientSizeChanged.Empty());
    EXPECT_EQ(game.getServicesProperty().GetService<IGraphicsDeviceManager>(), &first);
    EXPECT_EQ(game.getServicesProperty().GetService<Graphics::IGraphicsDeviceService>(), &first);
}

// TQ-030. When registration itself is refused -- another IGraphicsDeviceService is present, so the
// AddService at ..ctor IL_007c-008d throws -- XNA has hooked no window event yet (IL_0092-00d2
// follow). CNA's destructor then detaches the half-built manager: it must remove what this manager
// added (its IGraphicsDeviceManager registration, which C++ destruction cannot leave dangling,
// unlike XNA's collected object) and never another subscriber's window handler.
TEST(GraphicsDeviceManagerLifecycleContractTest, ARefusedRegistrationDetachesOnlyWhatTheManagerAdded)
{
    Game game;
    game.getWindowProperty().ClientSizeChanged += [](System::Object*, const System::EventArgs&) {};
    GraphicsDeviceManager foreign;
    game.getServicesProperty().AddService<Graphics::IGraphicsDeviceService>(&foreign);

    EXPECT_THROW({ GraphicsDeviceManager manager(&game); }, System::ArgumentException);

    EXPECT_FALSE(game.getWindowProperty().ClientSizeChanged.Empty());
    EXPECT_EQ(game.getServicesProperty().GetService<Graphics::IGraphicsDeviceService>(), &foreign);
    EXPECT_EQ(game.getServicesProperty().GetService<IGraphicsDeviceManager>(), nullptr);
    game.getServicesProperty().RemoveService<Graphics::IGraphicsDeviceService>();
}

TEST(GraphicsDeviceManagerLifecycleContractTest, ToggleRefusalKeepsPreferenceAndDeviceForRetry)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
        GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
    Game game;
    GraphicsDeviceManager manager(&game);
    manager.CreateDevice();
    auto* const device = manager.getGraphicsDeviceProperty();
    ASSERT_NE(device, nullptr);
    const bool initial = manager.getIsFullScreenProperty();
    std::vector<std::string> trace;
    bool refuse = true;
    manager.PreparingDeviceSettings += [&](System::Object*, const PreparingDeviceSettingsEventArgs&)
    {
        trace.emplace_back("prepare");
        if (refuse) throw std::runtime_error("toggle refusal");
    };
    manager.DeviceResetting += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("resetting"); };
    manager.DeviceReset += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("reset"); };
    EXPECT_THROW(manager.ToggleFullScreen(), std::runtime_error);
    EXPECT_EQ(trace, (std::vector<std::string>{"prepare"}));
    EXPECT_EQ(manager.getIsFullScreenProperty(), !initial);
    EXPECT_EQ(device->getPresentationParametersProperty().getIsFullScreenProperty(), initial);
    EXPECT_EQ(manager.getGraphicsDeviceProperty(), device);
    EXPECT_FALSE(device->getIsDisposedProperty());
    refuse = false;
    trace.clear();
    manager.ApplyChanges();
    EXPECT_EQ(trace, (std::vector<std::string>{"prepare", "resetting", "reset"}));
    EXPECT_EQ(device->getPresentationParametersProperty().getIsFullScreenProperty(), !initial);
    trace.clear();
    manager.ApplyChanges();
    EXPECT_TRUE(trace.empty());
    manager.Dispose();
}

// TQ-030. XNA GraphicsDeviceManager..ctor IL_0000-0024 stores SynchronizeWithVerticalRetrace = true,
// PreferredDepthStencilFormat = 2 (DepthFormat.Depth24) and the 800x480 back-buffer defaults that
// .cctor IL_0000-000f assigns; the other preferences keep their CLR zero: PreferredBackBufferFormat
// 0 (SurfaceFormat.Color), PreferMultiSampling false, IsFullScreen false, SupportedOrientations 0
// (DisplayOrientation.Default). IL_0066-008d register the manager under both service interfaces.
TEST(GraphicsDeviceManagerLifecycleContractTest, ConstructorDefaultsMatchXna)
{
    Game game;
    GraphicsDeviceManager manager(&game);
    EXPECT_EQ(GraphicsDeviceManager::DefaultBackBufferWidth, 800);
    EXPECT_EQ(GraphicsDeviceManager::DefaultBackBufferHeight, 480);
    EXPECT_TRUE(manager.getSynchronizeWithVerticalRetraceProperty());
    EXPECT_EQ(manager.getPreferredDepthStencilFormatProperty(), Graphics::DepthFormat::Depth24);
    EXPECT_EQ(manager.getPreferredBackBufferFormatProperty(), Graphics::SurfaceFormat::Color);
    EXPECT_FALSE(manager.getPreferMultiSamplingProperty());
    EXPECT_FALSE(manager.getIsFullScreenProperty());
    EXPECT_EQ(manager.getSupportedOrientationsProperty(), DisplayOrientation::Default);
    EXPECT_EQ(game.getServicesProperty().GetService<IGraphicsDeviceManager>(), &manager);
    EXPECT_EQ(game.getServicesProperty().GetService<Graphics::IGraphicsDeviceService>(), &manager);
}

// TQ-030. What a PreparingDeviceSettings handler receives is built from the preferences. XNA
// AddDevices(bool, List) IL_004d-00a1 copies GraphicsProfile, the window handle and IsFullScreen
// and maps SynchronizeWithVerticalRetrace to PresentInterval.One (1) or Immediate (3);
// AddDevices(adapter, mode, ...) IL_005f-007c copies the preferred size of a windowed device and
// IL_0081-00cc asks the adapter for the preferred depth format with a multisample count of 0 when
// PreferMultiSampling is false. Depth16 is available in every profile, so the query keeps it.
TEST(GraphicsDeviceManagerLifecycleContractTest, PreferencesReachThePreparingDeviceSettingsArguments)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
        GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
    Game game;
    GraphicsDeviceManager manager(&game);
    manager.setSynchronizeWithVerticalRetraceProperty(false);
    manager.setPreferredDepthStencilFormatProperty(Graphics::DepthFormat::Depth16);
    manager.setPreferredBackBufferWidthProperty(640);
    manager.setPreferredBackBufferHeightProperty(360);
    std::vector<Graphics::PresentationParameters> prepared;
    std::vector<Graphics::GraphicsProfile> profiles;
    manager.PreparingDeviceSettings += [&](System::Object*, const PreparingDeviceSettingsEventArgs& args)
    {
        const GraphicsDeviceInformation& information = args.getGraphicsDeviceInformationProperty();
        EXPECT_NE(information.getAdapterProperty(), nullptr);
        profiles.push_back(information.getGraphicsProfileProperty());
        prepared.push_back(information.getPresentationParametersProperty().Clone());
    };

    manager.CreateDevice();

    ASSERT_EQ(prepared.size(), 1U);
    EXPECT_EQ(profiles[0], manager.getGraphicsProfileProperty());
    EXPECT_EQ(prepared[0].getBackBufferWidthProperty(), 640);
    EXPECT_EQ(prepared[0].getBackBufferHeightProperty(), 360);
    EXPECT_FALSE(prepared[0].getIsFullScreenProperty());
    EXPECT_EQ(prepared[0].getPresentationIntervalProperty(), Graphics::PresentInterval::Immediate);
    EXPECT_EQ(prepared[0].getMultiSampleCountProperty(), 0);
    EXPECT_EQ(prepared[0].getDepthStencilFormatProperty(), Graphics::DepthFormat::Depth16);
    EXPECT_EQ(prepared[0].getDeviceWindowHandleProperty(), game.getWindowProperty().getHandleProperty());

    manager.setSynchronizeWithVerticalRetraceProperty(true);
    manager.ApplyChanges();

    ASSERT_EQ(prepared.size(), 2U);
    EXPECT_EQ(prepared[1].getPresentationIntervalProperty(), Graphics::PresentInterval::One);
    EXPECT_EQ(prepared[1].getBackBufferWidthProperty(), 640);
    EXPECT_EQ(prepared[1].getBackBufferHeightProperty(), 360);
    EXPECT_EQ(prepared[1].getDepthStencilFormatProperty(), Graphics::DepthFormat::Depth16);
    manager.Dispose();
}

// TQ-030. Every preference setter stores isDeviceDirty = true without comparing the old value
// (setters IL_0007-0009, the size setters IL_0022-0024), and ApplyChanges IL_0000-0011 skips only a
// clean existing device. Assigning the current value therefore still re-applies through a reset:
// ChangeDevice IL_00bf raises PreparingDeviceSettings, CanResetDevice holds for an unchanged
// profile, and Reset's DeviceResetting/DeviceReset are forwarded (HandleDeviceResetting/Reset).
// ChangeDevice IL_0172-0174 leaves the device clean for the next ApplyChanges.
TEST(GraphicsDeviceManagerLifecycleContractTest, ReassigningAnyPreferenceReappliesItOnce)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
        GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
    Game game;
    GraphicsDeviceManager manager(&game);
    manager.CreateDevice();
    std::vector<std::string> trace;
    manager.PreparingDeviceSettings += [&](System::Object*, const PreparingDeviceSettingsEventArgs&)
    { trace.emplace_back("prepare"); };
    manager.DeviceResetting += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("resetting"); };
    manager.DeviceReset += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("reset"); };
    manager.DeviceCreated += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("created"); };
    manager.DeviceDisposing += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("disposing"); };

    const char* const setters[] = {
        "GraphicsProfile", "PreferredDepthStencilFormat", "PreferredBackBufferFormat",
        "PreferredBackBufferWidth", "PreferredBackBufferHeight", "IsFullScreen",
        "SynchronizeWithVerticalRetrace", "PreferMultiSampling", "SupportedOrientations"};
    for (int setter = 0; setter < 9; ++setter)
    {
        SCOPED_TRACE(setters[setter]);
        trace.clear();
        manager.ApplyChanges();
        EXPECT_TRUE(trace.empty()) << "the device must start clean";
        switch (setter)
        {
        case 0: manager.setGraphicsProfileProperty(manager.getGraphicsProfileProperty()); break;
        case 1: manager.setPreferredDepthStencilFormatProperty(manager.getPreferredDepthStencilFormatProperty()); break;
        case 2: manager.setPreferredBackBufferFormatProperty(manager.getPreferredBackBufferFormatProperty()); break;
        case 3: manager.setPreferredBackBufferWidthProperty(manager.getPreferredBackBufferWidthProperty()); break;
        case 4: manager.setPreferredBackBufferHeightProperty(manager.getPreferredBackBufferHeightProperty()); break;
        case 5: manager.setIsFullScreenProperty(manager.getIsFullScreenProperty()); break;
        case 6: manager.setSynchronizeWithVerticalRetraceProperty(manager.getSynchronizeWithVerticalRetraceProperty()); break;
        case 7: manager.setPreferMultiSamplingProperty(manager.getPreferMultiSamplingProperty()); break;
        default: manager.setSupportedOrientationsProperty(manager.getSupportedOrientationsProperty()); break;
        }
        manager.ApplyChanges();
        EXPECT_EQ(trace, (std::vector<std::string>{"prepare", "resetting", "reset"}));
    }
    manager.Dispose();
}

// TQ-030. ToggleFullScreen IL_0000-0016 negates IsFullScreen and calls ChangeDevice(false) itself.
// The profile is unchanged, so each toggle is one reset whose prepared settings already carry the
// new mode (AddDevices IL_007e-008a), and toggling back restores the windowed parameters.
TEST(GraphicsDeviceManagerLifecycleContractTest, ToggleFullScreenResetsIntoTheNewModeAndBack)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
        GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
    Game game;
    GraphicsDeviceManager manager(&game);
    manager.CreateDevice();
    auto* const device = manager.getGraphicsDeviceProperty();
    ASSERT_NE(device, nullptr);
    ASSERT_FALSE(manager.getIsFullScreenProperty());
    std::vector<std::string> trace;
    manager.PreparingDeviceSettings += [&](System::Object*, const PreparingDeviceSettingsEventArgs& args)
    {
        trace.emplace_back(args.getGraphicsDeviceInformationProperty().getPresentationParametersProperty()
                               .getIsFullScreenProperty() ? "prepare-full" : "prepare-windowed");
    };
    manager.DeviceResetting += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("resetting"); };
    manager.DeviceReset += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("reset"); };
    manager.DeviceCreated += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("created"); };
    manager.DeviceDisposing += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("disposing"); };

    manager.ToggleFullScreen();
    EXPECT_TRUE(manager.getIsFullScreenProperty());
    EXPECT_TRUE(device->getPresentationParametersProperty().getIsFullScreenProperty());
    EXPECT_EQ(trace, (std::vector<std::string>{"prepare-full", "resetting", "reset"}));

    trace.clear();
    manager.ToggleFullScreen();
    EXPECT_FALSE(manager.getIsFullScreenProperty());
    EXPECT_FALSE(device->getPresentationParametersProperty().getIsFullScreenProperty());
    EXPECT_EQ(trace, (std::vector<std::string>{"prepare-windowed", "resetting", "reset"}));
    EXPECT_EQ(manager.getGraphicsDeviceProperty(), device);
    manager.Dispose();
}

// XNA GraphicsDeviceManager.Dispose(bool) IL_0011-0043 removes only its own
// IGraphicsDeviceService. C++ destruction must subsequently detach the raw manager pointer.
TEST(GraphicsDeviceManagerLifecycleContractTest, DisposeRetainsManagerUntilCppDestruction)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow()) GTEST_SKIP();
    Game game;
    auto& services = game.getServicesProperty();
    {
        GraphicsDeviceManager manager(&game);
        manager.Dispose();
        EXPECT_EQ(services.GetService<IGraphicsDeviceManager>(), &manager);
        EXPECT_EQ(services.GetService<Graphics::IGraphicsDeviceService>(), nullptr);
        EXPECT_EQ(manager.getGraphicsDeviceProperty(), nullptr);
        EXPECT_THROW(GraphicsDeviceManager second(&game), System::ArgumentException);
        manager.Dispose();
        EXPECT_EQ(services.GetService<IGraphicsDeviceManager>(), &manager);
    }
    EXPECT_EQ(services.GetService<IGraphicsDeviceManager>(), nullptr);
    EXPECT_EQ(services.GetService<Graphics::IGraphicsDeviceService>(), nullptr);
}
