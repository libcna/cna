// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/DrawableGameComponent.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "RuntimePlatformTestSupport.hpp"
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;

namespace
{
class HookComponent final : public DrawableGameComponent
{
  public:
    explicit HookComponent(Game &game) : DrawableGameComponent(game)
    {
    }
    bool callBase = true;
    std::vector<std::string> trace;
    void FinalizerPath()
    {
        GameComponent::Dispose(false);
    }
    void BaseHooks(System::Object *sender)
    {
        GameComponent::OnEnabledChanged(sender, System::EventArgs::Empty);
        GameComponent::OnUpdateOrderChanged(sender, System::EventArgs::Empty);
        DrawableGameComponent::OnVisibleChanged(sender, System::EventArgs::Empty);
        DrawableGameComponent::OnDrawOrderChanged(sender, System::EventArgs::Empty);
    }

  protected:
    void OnEnabledChanged(System::Object *sender, const System::EventArgs &args) override
    {
        trace.emplace_back("enabled hook");
        if (callBase)
            GameComponent::OnEnabledChanged(sender, args);
    }
    void OnUpdateOrderChanged(System::Object *sender, const System::EventArgs &args) override
    {
        trace.emplace_back("update hook");
        if (callBase)
            GameComponent::OnUpdateOrderChanged(sender, args);
    }
    void OnVisibleChanged(System::Object *sender, const System::EventArgs &args) override
    {
        trace.emplace_back("visible hook");
        if (callBase)
            DrawableGameComponent::OnVisibleChanged(sender, args);
    }
    void OnDrawOrderChanged(System::Object *sender, const System::EventArgs &args) override
    {
        trace.emplace_back("draw hook");
        if (callBase)
            DrawableGameComponent::OnDrawOrderChanged(sender, args);
    }
};

class DisposalDrawable final : public DrawableGameComponent
{
  public:
    explicit DisposalDrawable(Game &game) : DrawableGameComponent(game)
    {
        Disposed += [this](System::Object *, const System::EventArgs &) { trace.emplace_back("event"); };
    }
    std::vector<std::string> trace;
    bool throwOnce = false;
    void FinalizerPath()
    {
        DrawableGameComponent::Dispose(false);
    }

  protected:
    void UnloadContent() override
    {
        trace.emplace_back("unload");
        if (throwOnce)
        {
            throwOnce = false;
            throw std::runtime_error("unload");
        }
    }
};

class OwnedHookComponent final : public DrawableGameComponent
{
  public:
    OwnedHookComponent(Game &game, bool &alive) : DrawableGameComponent(game), alive_(alive)
    {
        alive_ = true;
    }
    ~OwnedHookComponent() override
    {
        alive_ = false;
    }

  protected:
    void OnEnabledChanged(System::Object *sender, const System::EventArgs &args) override
    {
        bool &alive = alive_;
        GameComponent::OnEnabledChanged(sender, args);
        EXPECT_TRUE(alive); // External flag is safe even on the premature-destruction path.
    }
    void OnUpdateOrderChanged(System::Object *sender, const System::EventArgs &args) override
    {
        bool &alive = alive_;
        GameComponent::OnUpdateOrderChanged(sender, args);
        EXPECT_TRUE(alive);
    }
    void OnVisibleChanged(System::Object *sender, const System::EventArgs &args) override
    {
        bool &alive = alive_;
        DrawableGameComponent::OnVisibleChanged(sender, args);
        EXPECT_TRUE(alive);
    }
    void OnDrawOrderChanged(System::Object *sender, const System::EventArgs &args) override
    {
        bool &alive = alive_;
        DrawableGameComponent::OnDrawOrderChanged(sender, args);
        EXPECT_TRUE(alive);
    }

  private:
    bool &alive_;
};

class OwnedComponent final : public GameComponent
{
  public:
    OwnedComponent(Game &game, std::vector<std::string> &trace) : GameComponent(game), trace_(trace)
    {
    }
    ~OwnedComponent() override
    {
        trace_.emplace_back("destroy");
    }

  protected:
    void Dispose(bool disposing) override
    {
        GameComponent::Dispose(disposing);
        if (disposing)
            trace_.emplace_back("derived after base");
    }

  private:
    std::vector<std::string> &trace_;
};

class ComponentLifecycleCompatibilityTest : public ::testing::Test
{
  protected:
    std::unique_ptr<Game> game;
    void SetUp() override
    {
        if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
            GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
        game = std::make_unique<Game>();
    }
};
} // namespace

TEST_F(ComponentLifecycleCompatibilityTest, HooksDispatchAndCanSuppressAllFourChangeEvents)
{
    HookComponent c(*game);
    const auto handler = [&](const char *name) {
        return [&, name](System::Object *sender, const System::EventArgs &args) {
            EXPECT_EQ(sender, static_cast<GameComponent *>(&c));
            EXPECT_EQ(&args, &System::EventArgs::Empty);
            c.trace.emplace_back(name);
        };
    };
    c.EnabledChanged += handler("enabled event");
    c.UpdateOrderChanged += handler("update event");
    c.VisibleChanged += handler("visible event");
    c.DrawOrderChanged += handler("draw event");
    c.setEnabledProperty(false);
    c.setUpdateOrderProperty(1);
    c.setVisibleProperty(false);
    c.setDrawOrderProperty(1);
    EXPECT_EQ(c.trace,
              (std::vector<std::string>{"enabled hook", "enabled event", "update hook", "update event",
                                        "visible hook", "visible event", "draw hook", "draw event"}));
    c.trace.clear();
    c.setEnabledProperty(false);
    c.setUpdateOrderProperty(1);
    c.setVisibleProperty(false);
    c.setDrawOrderProperty(1);
    EXPECT_TRUE(c.trace.empty());
    c.callBase = false;
    c.setEnabledProperty(true);
    c.setUpdateOrderProperty(2);
    c.setVisibleProperty(true);
    c.setDrawOrderProperty(2);
    EXPECT_EQ(c.trace,
              (std::vector<std::string>{"enabled hook", "update hook", "visible hook", "draw hook"}));
    c.trace.clear();
    c.BaseHooks(game.get()); // The base hook uses the component, not the supplied sender.
    EXPECT_EQ(c.trace,
              (std::vector<std::string>{"enabled event", "update event", "visible event", "draw event"}));
}

TEST_F(ComponentLifecycleCompatibilityTest, ExplicitDisposalRepeatsButFinalizerPathIsSilent)
{
    HookComponent c(*game);
    int count = 0;
    c.Disposed += [&](System::Object *sender, const System::EventArgs &args) {
        EXPECT_EQ(sender, static_cast<GameComponent *>(&c));
        EXPECT_EQ(&args, &System::EventArgs::Empty);
        ++count;
    };
    static_cast<GameComponent &>(c).Dispose();
    static_cast<GameComponent &>(c).Dispose();
    c.FinalizerPath();
    EXPECT_EQ(count, 2);
}

TEST_F(ComponentLifecycleCompatibilityTest, ExplicitDisposalAllowsGuardedReentryAndLaterCallAfterException)
{
    GameComponent reentrant(*game), throwing(*game);
    int reentries = 0, attempts = 0;
    reentrant.Disposed += [&](System::Object *, const System::EventArgs &) {
        if (++reentries == 1)
            reentrant.Dispose();
    };
    reentrant.Dispose();
    EXPECT_EQ(reentries, 2);
    throwing.Disposed += [&](System::Object *, const System::EventArgs &) {
        if (++attempts == 1)
            throw std::runtime_error("handler");
    };
    EXPECT_THROW(throwing.Dispose(), std::runtime_error);
    EXPECT_NO_THROW(throwing.Dispose());
    EXPECT_EQ(attempts, 2);
}

// Genuine DrawableDisposalOracle.cs: managed UnloadContent is an override hook,
// not an idempotent resource flag, and runs even when Initialize was never called.
TEST_F(ComponentLifecycleCompatibilityTest, DrawableExplicitDisposalAlwaysUnloadsBeforeBaseEvent)
{
    DisposalDrawable c(*game);
    c.FinalizerPath();
    EXPECT_TRUE(c.trace.empty());
    static_cast<GameComponent &>(c).Dispose();
    static_cast<GameComponent &>(c).Dispose();
    EXPECT_EQ(c.trace, (std::vector<std::string>{"unload", "event", "unload", "event"}));
}

TEST_F(ComponentLifecycleCompatibilityTest, DrawableUnloadExceptionDoesNotSuppressALaterExplicitCall)
{
    DisposalDrawable c(*game);
    c.throwOnce = true;
    EXPECT_THROW(static_cast<GameComponent &>(c).Dispose(), std::runtime_error);
    EXPECT_EQ(c.trace, (std::vector<std::string>{"unload"}));
    EXPECT_NO_THROW(static_cast<GameComponent &>(c).Dispose());
    EXPECT_EQ(c.trace, (std::vector<std::string>{"unload", "unload", "event"}));
}

TEST_F(ComponentLifecycleCompatibilityTest, DisposalRemovesComponentBeforeRepeatedEvents)
{
    GameComponent c(*game);
    std::vector<std::string> trace;
    auto &components = game->getComponentsProperty();
    components.Add(&c);
    components.ComponentRemoved += [&](System::Object *, const GameComponentCollectionEventArgs &args) {
        EXPECT_EQ(args.getGameComponentProperty(), &c);
        EXPECT_EQ(components.getCountProperty(), 0U);
        trace.emplace_back("remove");
    };
    c.Disposed += [&](System::Object *, const System::EventArgs &) {
        EXPECT_FALSE(components.Contains(&c));
        trace.emplace_back("dispose");
    };
    c.Dispose();
    c.Dispose();
    EXPECT_EQ(trace, (std::vector<std::string>{"remove", "dispose", "dispose"}));
    // Explicitly remove a still-listed stack component on the pre-fix path.
    (void)components.Remove(&c);
}

TEST_F(ComponentLifecycleCompatibilityTest, LastOwnedReferenceSurvivesTheEntireVirtualDisposalCall)
{
    std::vector<std::string> trace;
    auto component = std::make_shared<OwnedComponent>(*game, trace);
    std::weak_ptr<OwnedComponent> weak = component;
    GameComponent *raw = component.get();
    component->Disposed += [&](System::Object *, const System::EventArgs &) {
        EXPECT_FALSE(weak.expired());
        trace.emplace_back("event");
    };
    game->getComponentsProperty().Add(std::move(component));
    raw->Dispose();
    EXPECT_TRUE(weak.expired());
    EXPECT_EQ(trace, (std::vector<std::string>{"event", "derived after base", "destroy"}));
    game->getComponentsProperty().Clear(); // Pre-fix cleanup while trace is still alive.
}

TEST_F(ComponentLifecycleCompatibilityTest, OwnedChangeCallbacksSurviveRemovalThroughTheEntireVirtualHook)
{
    for (int property = 0; property != 4; ++property)
    {
        SCOPED_TRACE(property);
        bool alive = false;
        auto component = std::make_shared<OwnedHookComponent>(*game, alive);
        std::weak_ptr<OwnedHookComponent> weak = component;
        OwnedHookComponent *raw = component.get();
        auto &components = game->getComponentsProperty();
        const auto remove = [&](System::Object *, const System::EventArgs &) {
            EXPECT_TRUE(alive);
            EXPECT_TRUE(components.Remove(raw));
            EXPECT_TRUE(alive);
        };
        switch (property)
        {
        case 0: component->EnabledChanged += remove; break;
        case 1: component->UpdateOrderChanged += remove; break;
        case 2: component->VisibleChanged += remove; break;
        case 3: component->DrawOrderChanged += remove; break;
        }
        components.Add(std::move(component));
        switch (property)
        {
        case 0: raw->setEnabledProperty(false); break;
        case 1: raw->setUpdateOrderProperty(1); break;
        case 2: raw->setVisibleProperty(false); break;
        case 3: raw->setDrawOrderProperty(1); break;
        }
        EXPECT_FALSE(alive);
        EXPECT_TRUE(weak.expired());
        EXPECT_EQ(components.getCountProperty(), 0U);
    }
}

TEST_F(ComponentLifecycleCompatibilityTest, GameDisposesAnOwningSnapshotDespiteCallbackRemoval)
{
    auto first = std::make_shared<GameComponent>(*game);
    auto second = std::make_shared<GameComponent>(*game);
    auto third = std::make_shared<GameComponent>(*game);
    auto &components = game->getComponentsProperty();
    GameComponent *secondRaw = second.get();
    std::vector<std::string> trace;
    first->Disposed += [&](System::Object *, const System::EventArgs &) {
        trace.emplace_back("first");
        EXPECT_TRUE(components.Remove(secondRaw));
    };
    second->Disposed += [&](System::Object *, const System::EventArgs &) { trace.emplace_back("second"); };
    third->Disposed += [&](System::Object *, const System::EventArgs &) { trace.emplace_back("third"); };
    components.Add(std::move(first));
    components.Add(std::move(second));
    components.Add(std::move(third));
    game->Dispose();
    EXPECT_EQ(trace, (std::vector<std::string>{"first", "second", "third"}));
    EXPECT_EQ(components.getCountProperty(), 0U);
    components.Clear();
}
