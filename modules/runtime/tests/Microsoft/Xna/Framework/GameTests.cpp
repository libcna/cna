// SPDX-License-Identifier: MS-PL
// PLAT-56: Game requires the selected platform/renderer combination to construct its ordinary
// hidden window. Probe that capability through IPlatform so this suite is unchanged across
// platform selections and never names a backend API.

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/DrawableGameComponent.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/IGraphicsDeviceService.hpp"
#include "System/EventArgs.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "RuntimePlatformTestSupport.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

using namespace Microsoft::Xna::Framework;

namespace CNA::Internal
{
    class GameTestPeer
    {
    public:
        static bool IsSuspended(const Microsoft::Xna::Framework::Game& game) { return game.isSuspended_; }
        static void PollEvents(Microsoft::Xna::Framework::Game& game) { game.PollEvents(); }
    };
}

namespace
{
    // A real platform with only its event source scripted, so a test can state a lifecycle
    // transition in CNA's own vocabulary instead of injecting a backend event.
    class ScriptedEventPlatform final : public CNA::Platform::Testing::PlatformTestDecorator
    {
    public:
        explicit ScriptedEventPlatform(std::unique_ptr<CNA::Platform::IPlatform> inner)
            : PlatformTestDecorator(std::move(inner))
        {
        }

        void Queue(const std::vector<CNA::Platform::PlatformEvent>& events)
        {
            queued_.insert(queued_.end(), events.begin(), events.end());
        }

        void PollEvents(std::vector<CNA::Platform::PlatformEvent>& destination) override
        {
            destination.clear();
            destination.insert(destination.end(), queued_.begin(), queued_.end());
            queued_.clear();
        }

    private:
        std::vector<CNA::Platform::PlatformEvent> queued_;
    };

    // Records lifecycle call order/counts and exits after the first Draw().
    class LifecycleTestGame : public Game
    {
    public:
        LifecycleTestGame() = default;

        explicit LifecycleTestGame(std::unique_ptr<CNA::Platform::IPlatform> platform)
            : Game(std::move(platform))
        {
        }

        int initializeCalls = 0;
        int loadContentCalls = 0;
        int updateCalls = 0;
        int drawCalls = 0;
        int unloadContentCalls = 0;

    protected:
        void Initialize() override
        {
            ++initializeCalls;
            Game::Initialize(); // XNA contract: base Initialize() invokes LoadContent().
        }

        void LoadContent() override
        {
            ++loadContentCalls;
        }

        void Update(GameTime& gameTime) override
        {
            ++updateCalls;
            Game::Update(gameTime);
        }

        void Draw(const GameTime& gameTime) override
        {
            ++drawCalls;
            Game::Draw(gameTime);
            Exit();
        }

        void UnloadContent() override
        {
            ++unloadContentCalls;
        }
    };

    // Minimal IGraphicsDeviceService test double, independent of GraphicsDeviceManager, used to
    // exercise Game::Initialize()'s deferred-LoadContent branch (a registered service whose device
    // is not yet available at Initialize() time) without needing a second real GraphicsDevice.
    class DeferredDeviceService : public Graphics::IGraphicsDeviceService
    {
    public:
        Graphics::GraphicsDevice* device = nullptr;
        System::EventHandler<System::EventArgs> DeviceCreatedEvt;
        System::EventHandler<System::EventArgs> DeviceDisposingEvt;
        System::EventHandler<System::EventArgs> DeviceResetEvt;
        System::EventHandler<System::EventArgs> DeviceResettingEvt;

        [[nodiscard]] Graphics::GraphicsDevice* getGraphicsDeviceProperty() const override { return device; }
        [[nodiscard]] System::EventHandler<System::EventArgs>& getDeviceCreatedEvent() override { return DeviceCreatedEvt; }
        [[nodiscard]] System::EventHandler<System::EventArgs>& getDeviceDisposingEvent() override { return DeviceDisposingEvt; }
        [[nodiscard]] System::EventHandler<System::EventArgs>& getDeviceResetEvent() override { return DeviceResetEvt; }
        [[nodiscard]] System::EventHandler<System::EventArgs>& getDeviceResettingEvent() override { return DeviceResettingEvt; }
    };
}

namespace
{
    // SAMPLE-065/SAMPLE-061: the XNA loading-screen pattern builds the next screen's components on
    // a background thread and adds them to Game.Components while the game keeps drawing the
    // loading art, so Game's two ordered component lists are written from one thread and iterated
    // from another. Before they were guarded, a NinjAcademy resume reproducibly aborted inside
    // Game::Update with "pure virtual method called": the frame's snapshot copy was reading
    // updateableComponents_ while the loader thread's insert reallocated it.
    class ComponentAddingGame : public Game
    {
    public:
        using Game::Draw;
        using Game::Update;
    };

    class CountingComponent final : public DrawableGameComponent
    {
    public:
        explicit CountingComponent(Game& game) : DrawableGameComponent(game) { }

        void Update(GameTime& gameTime) override
        {
            (void) gameTime;
            ++updates;
        }

        void Draw(const GameTime& gameTime) override
        {
            (void) gameTime;
            ++draws;
        }

        std::atomic<int> updates{0};
        std::atomic<int> draws{0};
    };
}

// SAMPLE-065: a component removed while the frame that snapshotted it is still running must not
// be called afterwards. XNA can leave it in the snapshot because the snapshot holds a strong
// reference; CNA's snapshot is raw pointers, and the code that removes a component is usually the
// same code that owns and frees it -- here a screen's Update() dropping the screen that owns them,
// which is what NinjAcademy's menu does every frame while it transitions to its loading screen.
TEST(GameTest, AComponentRemovedMidFrameIsNotCalledLaterInThatSameFrame)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    ComponentAddingGame game;
    game.RunOneFrame();

    auto victim = std::make_unique<CountingComponent>(game);

    // Removes the victim from inside its own Update()/Draw(), then frees it, exactly as a screen
    // manager does when the update it is running drops the screen that owns the components.
    class RemovingComponent final : public DrawableGameComponent
    {
    public:
        RemovingComponent(Game& game, std::unique_ptr<CountingComponent>& target)
            : DrawableGameComponent(game), target_(target) { }

        void Update(GameTime& gameTime) override
        {
            (void) gameTime;
            Drop();
        }

        void Draw(const GameTime& gameTime) override
        {
            (void) gameTime;
            Drop();
        }

    private:
        void Drop()
        {
            if (target_)
            {
                getGameProperty().getComponentsProperty().Remove(target_.get());
                target_.reset();
            }
        }

        std::unique_ptr<CountingComponent>& target_;
    };

    RemovingComponent remover(game, victim);
    remover.setUpdateOrderProperty(-1);
    remover.setDrawOrderProperty(-1);

    game.getComponentsProperty().Add(&remover);
    game.getComponentsProperty().Add(victim.get());

    GameTime gameTime;
    ASSERT_NO_FATAL_FAILURE(game.Update(gameTime));
    ASSERT_NO_FATAL_FAILURE(game.Draw(gameTime));
    EXPECT_EQ(victim, nullptr);

    game.getComponentsProperty().Remove(&remover);
}

TEST(GameTest, ComponentsAddedFromALoadingThreadSurviveTheFrameThatIsIteratingThem)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    ComponentAddingGame game;
    game.RunOneFrame();

    constexpr int componentCount = 200;
    std::vector<std::unique_ptr<CountingComponent>> components;
    components.reserve(componentCount);
    for (int i = 0; i < componentCount; ++i)
    {
        components.push_back(std::make_unique<CountingComponent>(game));
    }

    std::atomic<bool> adding{true};
    std::thread loader([&]
    {
        for (auto& component : components)
        {
            game.getComponentsProperty().Add(component.get());
        }
        adding = false;
    });

    GameTime gameTime;
    while (adding)
    {
        game.Update(gameTime);
        game.Draw(gameTime);
    }
    loader.join();

    // Drain whatever the last additions missed, then every component must be live in both lists.
    game.Update(gameTime);
    game.Draw(gameTime);
    for (const auto& component : components)
    {
        EXPECT_GT(component->updates.load(), 0);
        EXPECT_GT(component->draws.load(), 0);
    }

    for (const auto& component : components)
    {
        game.getComponentsProperty().Remove(component.get());
    }
}

TEST(GameTest, ConstructionRegistersBuiltInXnbReadersBeforeLoadContent)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
    ContentTypeReaderManager::ClearTypeCreators();

    LifecycleTestGame game;

    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(
        "Microsoft.Xna.Framework.Content.ModelReader"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(
        "Microsoft.Xna.Framework.Content.EffectReader"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(
        "Microsoft.Xna.Framework.Content.SpriteFontReader"));
}

TEST(GameTest, RunExecutesLifecycleInDocumentedOrder)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    LifecycleTestGame game;
    GraphicsDeviceManager gdm(&game);

    ASSERT_NO_THROW(game.Run());

    EXPECT_EQ(game.initializeCalls, 1);
    EXPECT_EQ(game.loadContentCalls, 1);
    EXPECT_GE(game.updateCalls, 1);
    EXPECT_GE(game.drawCalls, 1);
}

TEST(GameTest, MobileLifecycleEventsSuspendResumeAndTerminateTheLoop)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a window in this environment.";
    }

    // plans/plan_apple.md APPLE-7. The lifecycle transitions are scripted as CNA platform events rather
    // than injected into a backend queue: what is under test is the loop's reaction, and stating
    // it this way keeps the case true for every platform implementation instead of only the one
    // whose native constants it happened to name.
    auto scripted = std::make_unique<ScriptedEventPlatform>(
        CNA::Platform::PlatformFactory::Create());
    ScriptedEventPlatform& events = *scripted;
    LifecycleTestGame game(std::move(scripted));
    ASSERT_FALSE(CNA::Internal::GameTestPeer::IsSuspended(game));

    events.Queue({CNA::Platform::AppLifecycleEvent{
        CNA::Platform::AppLifecycleKind::WillEnterBackground}});
    CNA::Internal::GameTestPeer::PollEvents(game);
    EXPECT_TRUE(CNA::Internal::GameTestPeer::IsSuspended(game));
    EXPECT_FALSE(game.getIsActiveProperty());

    events.Queue({CNA::Platform::AppLifecycleEvent{
        CNA::Platform::AppLifecycleKind::DidEnterForeground}});
    CNA::Internal::GameTestPeer::PollEvents(game);
    EXPECT_FALSE(CNA::Internal::GameTestPeer::IsSuspended(game));
    EXPECT_TRUE(game.getIsActiveProperty());

    // Termination is already decided by the operating system, so it must both end the loop and
    // leave it able to reach OnExiting -- a process that stays parked in the suspended wait would
    // be killed before shutting down.
    events.Queue({CNA::Platform::AppLifecycleEvent{
                      CNA::Platform::AppLifecycleKind::WillEnterBackground},
                  CNA::Platform::AppLifecycleEvent{
                      CNA::Platform::AppLifecycleKind::Terminating}});
    CNA::Internal::GameTestPeer::PollEvents(game);
    EXPECT_FALSE(CNA::Internal::GameTestPeer::IsSuspended(game));
    EXPECT_FALSE(game.RunApplication);
}

// REMED-CORE-006 (fixed): Game::Initialize() now subscribes graphicsDeviceService.DeviceDisposing
// += (o,e) => UnloadContent(), matching FNA (Game.cs:649-662). Disposing the game's registered
// IGraphicsDeviceService (the GraphicsDeviceManager here) invokes UnloadContent() exactly once via
// the real public disposal path: Game::Dispose() -> Dispose(true) -> disposes
// graphicsDeviceService_ -> GraphicsDeviceManager::Dispose(true) -> OnDeviceDisposing()
// (REMED-CORE-014's fix: no longer gated on ownsGraphicsDevice_, which is always false for a
// Game-attached manager).
TEST(GameTest, DisposingDeviceInvokesUnloadContent)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    LifecycleTestGame game;
    GraphicsDeviceManager gdm(&game);

    ASSERT_NO_THROW(game.Run());
    game.Dispose();

    EXPECT_EQ(game.unloadContentCalls, 1);
}

// Regression guard: a second explicit Dispose() call must not re-invoke UnloadContent().
// GraphicsDeviceManager::Dispose(bool)'s own disposed_ guard makes the underlying DeviceDisposing
// raise a one-time event even though Game::Dispose() itself unconditionally re-raises Disposed on
// every call (FNA-faithful; Game.cs:296-304 does the same).
TEST(GameTest, RepeatedDisposeDoesNotReinvokeUnloadContent)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    LifecycleTestGame game;
    GraphicsDeviceManager gdm(&game);

    ASSERT_NO_THROW(game.Run());
    game.Dispose();
    ASSERT_NO_THROW(game.Dispose());

    EXPECT_EQ(game.unloadContentCalls, 1);
}

// Repeated Game/GraphicsDeviceManager construction and full lifecycle (including disposal) in one
// process must behave identically each time -- no leftover static/global state from one instance's
// DeviceDisposing subscription or disposal should affect the next.
TEST(GameTest, UnloadContentWorksAcrossRepeatedGameInstancesInOneProcess)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    for (int i = 0; i < 2; ++i)
    {
        LifecycleTestGame game;
        GraphicsDeviceManager gdm(&game);

        ASSERT_NO_THROW(game.Run()) << "iteration " << i;
        game.Dispose();

        EXPECT_EQ(game.unloadContentCalls, 1) << "iteration " << i;
    }
}

// REMED-CORE-006 full-fidelity branch: FNA's Initialize() also subscribes
// graphicsDeviceService.DeviceCreated += (o,e) => LoadContent(); when the registered service's
// device is not yet available at Initialize() time, deferring LoadContent() rather than skipping
// it. Exercised directly against a minimal IGraphicsDeviceService double (no GraphicsDeviceManager
// attached) since the Game+GraphicsDeviceManager combination always creates the device before
// Initialize() runs (DoInitialize() calls CreateDevice() first), making this branch otherwise
// unreachable through the normal production path.
TEST(GameTest, DeferredLoadContentFiresOnDeviceCreatedWhenServiceHasNoDeviceAtInitializeTime)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    LifecycleTestGame game;
    DeferredDeviceService fakeService;
    game.getServicesProperty().AddService<Graphics::IGraphicsDeviceService>(&fakeService);

    game.RunOneFrame();

    EXPECT_EQ(game.loadContentCalls, 0)
        << "LoadContent() must not fire immediately when the registered IGraphicsDeviceService has "
           "no device yet.";

    fakeService.DeviceCreatedEvt.Raise(nullptr, System::EventArgs::Empty);

    EXPECT_EQ(game.loadContentCalls, 1);
}
