// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/DrawableGameComponent.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "RuntimePlatformTestSupport.hpp"
#include "Microsoft/Xna/Framework/Graphics/IGraphicsDeviceService.hpp"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::Xna::Framework;

namespace
{
    class OrderingGame final : public Game
    {
    public:
        explicit OrderingGame(std::vector<std::string>& trace) : trace_(trace) {}
        using Game::Draw;
        using Game::Update;

    protected:
        void LoadContent() override { trace_.emplace_back("load"); }

    private:
        std::vector<std::string>& trace_;
    };

    class OrderingComponent final : public DrawableGameComponent
    {
    public:
        OrderingComponent(Game& game, std::vector<std::string>& trace, std::string name)
            : DrawableGameComponent(game), trace_(trace), name_(std::move(name)) {}

        std::function<void()> initializeAction;
        std::function<void()> updateAction;
        std::function<void()> drawAction;

        void Initialize() override
        {
            trace_.push_back("init " + name_);
            DrawableGameComponent::Initialize();
            if (initializeAction) initializeAction();
        }
        void Update(GameTime&) override
        {
            trace_.push_back("update " + name_);
            if (updateAction) updateAction();
        }
        void Draw(const GameTime&) override
        {
            trace_.push_back("draw " + name_);
            if (drawAction) drawAction();
        }

    private:
        std::vector<std::string>& trace_;
        std::string name_;
    };

    struct OrderingDeviceService final : Graphics::IGraphicsDeviceService
    {
        Graphics::GraphicsDevice* device = nullptr;
        System::EventHandler<System::EventArgs> created, disposing, reset, resetting;
        Graphics::GraphicsDevice* getGraphicsDeviceProperty() const override { return device; }
        System::EventHandler<System::EventArgs>& getDeviceCreatedEvent() override { return created; }
        System::EventHandler<System::EventArgs>& getDeviceDisposingEvent() override { return disposing; }
        System::EventHandler<System::EventArgs>& getDeviceResetEvent() override { return reset; }
        System::EventHandler<System::EventArgs>& getDeviceResettingEvent() override { return resetting; }
    };

    class GameComponentOrderingTest : public ::testing::Test
    {
    protected:
        std::vector<std::string> trace;
        OrderingDeviceService service;
        std::unique_ptr<OrderingGame> game;
        GameTime time;

        void SetUp() override
        {
            if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
                GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
            game = std::make_unique<OrderingGame>(trace);
            game->setIsFixedTimeStepProperty(false);
        }

        std::shared_ptr<OrderingComponent> Component(const char* name)
        {
            return std::make_shared<OrderingComponent>(*game, trace, name);
        }

        void Start()
        {
            // Refresh DrawableGameComponent.Initialize IL_0011..0048 requires this service.
            service.device = &game->getGraphicsDeviceProperty();
            game->getServicesProperty().AddService<Graphics::IGraphicsDeviceService>(&service);
            game->RunFrameEXT();
            trace.clear();
        }
    };

    class ComponentPassOrderingTest : public GameComponentOrderingTest,
                                      public ::testing::WithParamInterface<bool>
    {
    protected:
        void Order(OrderingComponent& component, int value)
        {
            if (GetParam()) component.setDrawOrderProperty(value);
            else component.setUpdateOrderProperty(value);
        }
        void Enabled(OrderingComponent& component, bool value)
        {
            if (GetParam()) component.setVisibleProperty(value);
            else component.setEnabledProperty(value);
        }
        void Action(OrderingComponent& component, std::function<void()> action)
        {
            if (GetParam()) component.drawAction = std::move(action);
            else component.updateAction = std::move(action);
        }
        void Pass()
        {
            trace.clear();
            if (GetParam()) game->Draw(time);
            else game->Update(time);
        }
        std::string Entry(const char* name) const
        {
            return std::string(GetParam() ? "draw " : "update ") + name;
        }
    };
}

TEST_F(GameComponentOrderingTest, InitializeUsesInsertionOrderAndIncludesComponentsAddedDuringInitialization)
{
    auto first = Component("first");
    auto second = Component("second");
    auto added = Component("added");
    first->setUpdateOrderProperty(20);
    second->setUpdateOrderProperty(-20);
    first->initializeAction = [&] { game->getComponentsProperty().Add(added); };
    game->getComponentsProperty().Add(first);
    game->getComponentsProperty().Add(second);
    GraphicsDeviceManager manager(game.get());

    game->RunFrameEXT();

    EXPECT_EQ(trace, (std::vector<std::string>{"init first", "init second", "init added", "load",
                                            "update second", "update added", "update first",
                                            "draw first", "draw second", "draw added"}));
    EXPECT_EQ(game->getComponentsProperty().getCountProperty(), 3U);
    // Verify a later frame does not reinitialize any component.
    trace.clear();
    game->RunOneFrame();
    EXPECT_EQ(trace, (std::vector<std::string>{"update second", "update added", "update first",
                                            "draw first", "draw second", "draw added"}));
}

TEST_P(ComponentPassOrderingTest, SortsDistinctOrdersAndKeepsInsertionOrderForTies)
{
    auto first = Component("first");
    auto second = Component("second");
    auto third = Component("third");
    Order(*first, 10);
    Order(*second, -5);
    Order(*third, -5);
    game->getComponentsProperty().Add(first);
    game->getComponentsProperty().Add(second);
    game->getComponentsProperty().Add(third);
    Start();

    Pass();
    EXPECT_EQ(trace, (std::vector<std::string>{Entry("second"), Entry("third"), Entry("first")}));
    Order(*second, 10);
    Pass();
    EXPECT_EQ(trace, (std::vector<std::string>{Entry("third"), Entry("first"), Entry("second")}));
    Order(*first, 10); // An unchanged property must not reinsert it after its peers.
    Pass();
    EXPECT_EQ(trace, (std::vector<std::string>{Entry("third"), Entry("first"), Entry("second")}));
}

TEST_P(ComponentPassOrderingTest, OrderChangesDuringTraversalApplyToTheNextSnapshot)
{
    auto first = Component("first");
    auto second = Component("second");
    auto third = Component("third");
    Order(*first, 0);
    Order(*second, 10);
    Order(*third, 20);
    game->getComponentsProperty().Add(first);
    game->getComponentsProperty().Add(second);
    game->getComponentsProperty().Add(third);
    Start();
    Action(*first, [&] { Order(*third, -10); });

    Pass();
    EXPECT_EQ(trace, (std::vector<std::string>{Entry("first"), Entry("second"), Entry("third")}));
    Pass();
    EXPECT_EQ(trace, (std::vector<std::string>{Entry("third"), Entry("first"), Entry("second")}));
    Action(*first, {});
}

TEST_P(ComponentPassOrderingTest, EnabledOrVisibleIsReadWhenEachComponentIsReached)
{
    auto first = Component("first");
    auto second = Component("second");
    auto third = Component("third");
    game->getComponentsProperty().Add(first);
    game->getComponentsProperty().Add(second);
    game->getComponentsProperty().Add(third);
    Start();
    Enabled(*third, false);
    Action(*first, [&] { Enabled(*second, false); Enabled(*third, true); });

    Pass();
    EXPECT_EQ(trace, (std::vector<std::string>{Entry("first"), Entry("third")}));
    Action(*first, [&] { Enabled(*second, true); Enabled(*third, false); });
    Pass();
    EXPECT_EQ(trace, (std::vector<std::string>{Entry("first"), Entry("second")}));
    Action(*first, {});
}

TEST_P(ComponentPassOrderingTest, RemovedComponentsCannotRejoinByChangingTheirOrder)
{
    auto first = Component("first");
    auto removed = Component("removed");
    game->getComponentsProperty().Add(first);
    game->getComponentsProperty().Add(removed);
    Start();
    ASSERT_TRUE(game->getComponentsProperty().Remove(removed.get()));
    Order(*removed, -10);

    Pass();
    EXPECT_EQ(trace, (std::vector<std::string>{Entry("first")}));
    game->getComponentsProperty().Add(removed);
    EXPECT_EQ(trace.back(), "init removed");
    Pass();
    EXPECT_EQ(trace, (std::vector<std::string>{Entry("removed"), Entry("first")}));
}

TEST_F(GameComponentOrderingTest, AddedDuringUpdateInitializesImmediatelyButWaitsForTheNextUpdate)
{
    auto first = Component("first");
    auto second = Component("second");
    auto added = Component("added");
    added->setUpdateOrderProperty(-10);
    added->setDrawOrderProperty(-10);
    game->getComponentsProperty().Add(first);
    game->getComponentsProperty().Add(second);
    Start();
    first->updateAction = [&] { game->getComponentsProperty().Add(added); };

    game->Update(time);
    EXPECT_EQ(trace, (std::vector<std::string>{"update first", "init added", "update second"}));
    first->updateAction = {};
    trace.clear();
    game->Draw(time);
    EXPECT_EQ(trace, (std::vector<std::string>{"draw added", "draw first", "draw second"}));
    trace.clear();
    game->Update(time);
    EXPECT_EQ(trace, (std::vector<std::string>{"update added", "update first", "update second"}));
}

TEST_F(GameComponentOrderingTest, ComponentsAndServicesAreStableAndSeparateForEachGame)
{
    OrderingGame other(trace);
    auto component = Component("first");
    game->getComponentsProperty().Add(component);
    int service = 42;
    game->getServicesProperty().AddService<int>(&service);

    const Game& constGame = *game;
    EXPECT_EQ(&game->getComponentsProperty(), &constGame.getComponentsProperty());
    EXPECT_EQ(&game->getServicesProperty(), &constGame.getServicesProperty());
    EXPECT_NE(&game->getComponentsProperty(), &other.getComponentsProperty());
    EXPECT_NE(&game->getServicesProperty(), &other.getServicesProperty());
    EXPECT_EQ(constGame.getComponentsProperty()[0], component.get());
    EXPECT_EQ(constGame.getServicesProperty().GetService<int>(), &service);
    EXPECT_EQ(other.getComponentsProperty().getCountProperty(), 0U);
    EXPECT_EQ(other.getServicesProperty().GetService<int>(), nullptr);
}

INSTANTIATE_TEST_SUITE_P(UpdateAndDraw, ComponentPassOrderingTest, ::testing::Bool(),
    [](const ::testing::TestParamInfo<bool>& info) { return info.param ? "Draw" : "Update"; });

TEST_F(GameComponentOrderingTest, AddedDuringDrawInitializesNowAndJoinsOnlyTheNextDrawSnapshot)
{
    // XNA Game.Draw IL_0004..002b copies drawables; GameComponentAdded initializes immediately.
    // The new component must nevertheless be present for the following Update pass.
    auto first = Component("first");
    auto second = Component("second");
    auto added = Component("added");
    added->setDrawOrderProperty(-10);
    added->setUpdateOrderProperty(-10);
    game->getComponentsProperty().Add(first);
    game->getComponentsProperty().Add(second);
    Start();
    first->drawAction = [&] { game->getComponentsProperty().Add(added); };
    game->Draw(time);
    EXPECT_EQ(trace, (std::vector<std::string>{"draw first", "init added", "draw second"}));
    first->drawAction = {};
    trace.clear();
    game->Update(time);
    EXPECT_EQ(trace, (std::vector<std::string>{"update added", "update first", "update second"}));
    trace.clear();
    game->Draw(time);
    EXPECT_EQ(trace, (std::vector<std::string>{"draw added", "draw first", "draw second"}));
}
