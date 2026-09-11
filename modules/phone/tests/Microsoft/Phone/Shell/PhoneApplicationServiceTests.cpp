// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "Microsoft/Phone/Shell/PhoneApplicationService.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "System/EventArgs.hpp"
#include "System/Object.hpp"

using Microsoft::Phone::Shell::ActivatedEventArgs;
using Microsoft::Phone::Shell::ClosingEventArgs;
using Microsoft::Phone::Shell::DeactivatedEventArgs;
using Microsoft::Phone::Shell::LaunchingEventArgs;
using Microsoft::Phone::Shell::PhoneApplicationService;
using Microsoft::Xna::Framework::Game;

namespace {

// The transitions are only meaningful in order, so every test records the sequence rather than
// counting each event on its own.
class RecordingService {
public:
    explicit RecordingService(PhoneApplicationService& service)
    {
        service.Launching += [this](System::Object*, const LaunchingEventArgs&) {
            events.emplace_back("Launching");
        };
        service.Activated += [this](System::Object*, const ActivatedEventArgs& e) {
            events.emplace_back(e.getIsApplicationInstancePreservedProperty() ? "Activated(preserved)"
                                                                             : "Activated");
        };
        service.Deactivated += [this](System::Object*, const DeactivatedEventArgs&) {
            events.emplace_back("Deactivated");
        };
        service.Closing += [this](System::Object*, const ClosingEventArgs&) {
            events.emplace_back("Closing");
        };
    }

    std::vector<std::string> events;
};

class StoredValue : public System::Object {
public:
    explicit StoredValue(int value) : Value(value) {}
    int Value;
    [[nodiscard]] const std::string& GetTypeName() const override
    {
        static const std::string name = "StoredValue";
        return name;
    }
};

} // namespace

TEST(PhoneApplicationServiceTest, AttachingReportsAFreshStart)
{
    Game game;
    PhoneApplicationService service;
    RecordingService recorder(service);

    service.AttachEXT(game);

    EXPECT_EQ(recorder.events, (std::vector<std::string>{"Launching"}));
}

TEST(PhoneApplicationServiceTest, TheFirstActivationOfARunIsNotAReturnFromTheBackground)
{
    // A game raises Activated when its window first takes focus. On a phone that is part of
    // starting up, not a return -- an application that reloads its saved game from Activated
    // must not reload it on a fresh start.
    Game game;
    PhoneApplicationService service;
    RecordingService recorder(service);

    service.AttachEXT(game);
    game.Activated.Raise(&game, System::EventArgs());

    EXPECT_EQ(recorder.events, (std::vector<std::string>{"Launching"}));
}

TEST(PhoneApplicationServiceTest, GoingAwayAndComingBackIsDeactivatedThenActivated)
{
    Game game;
    PhoneApplicationService service;
    RecordingService recorder(service);

    service.AttachEXT(game);
    game.Deactivated.Raise(&game, System::EventArgs());
    game.Activated.Raise(&game, System::EventArgs());

    EXPECT_EQ(recorder.events,
              (std::vector<std::string>{"Launching", "Deactivated", "Activated(preserved)"}));
}

TEST(PhoneApplicationServiceTest, ExitingTheGameClosesTheApplication)
{
    Game game;
    PhoneApplicationService service;
    RecordingService recorder(service);

    service.AttachEXT(game);
    game.Exiting.Raise(&game, System::EventArgs());

    EXPECT_EQ(recorder.events, (std::vector<std::string>{"Launching", "Closing"}));
}

TEST(PhoneApplicationServiceTest, DetachingStopsTheServiceFollowingTheGame)
{
    Game game;
    PhoneApplicationService service;
    RecordingService recorder(service);

    service.AttachEXT(game);
    service.DetachEXT();
    game.Deactivated.Raise(&game, System::EventArgs());
    game.Exiting.Raise(&game, System::EventArgs());

    EXPECT_EQ(recorder.events, (std::vector<std::string>{"Launching"}));
}

TEST(PhoneApplicationServiceTest, AttachingASecondGameLeavesTheFirstOneBehind)
{
    Game first;
    Game second;
    PhoneApplicationService service;
    RecordingService recorder(service);

    service.AttachEXT(first);
    service.AttachEXT(second);
    first.Deactivated.Raise(&first, System::EventArgs());
    second.Deactivated.Raise(&second, System::EventArgs());

    // Two Launchings, one per attach, and only the second game's Deactivated.
    EXPECT_EQ(recorder.events,
              (std::vector<std::string>{"Launching", "Launching", "Deactivated"}));
}

TEST(PhoneApplicationServiceTest, DestroyingTheServiceUnsubscribesFromTheGame)
{
    // The game outlives the service here, which is the ordering that would leave a dangling
    // subscription if the destructor did not detach.
    Game game;
    std::vector<std::string> seen;
    {
        PhoneApplicationService service;
        service.Deactivated += [&seen](System::Object*, const DeactivatedEventArgs&) {
            seen.emplace_back("Deactivated");
        };
        service.AttachEXT(game);
    }
    game.Deactivated.Raise(&game, System::EventArgs());

    EXPECT_TRUE(seen.empty());
}

TEST(PhoneApplicationServiceTest, StateKeepsWhatTheApplicationPutsThere)
{
    PhoneApplicationService service;

    EXPECT_FALSE(service.getStateProperty().ContainsKey("Yacht_State"));

    service.getStateProperty().Add("Yacht_State", std::make_shared<StoredValue>(42));

    ASSERT_TRUE(service.getStateProperty().ContainsKey("Yacht_State"));
    // TryGetValue rather than operator[], which hands back an assignable proxy because .NET's
    // indexer creates on assignment.
    std::shared_ptr<System::Object> value;
    ASSERT_TRUE(service.getStateProperty().TryGetValue("Yacht_State", value));
    const auto stored = std::dynamic_pointer_cast<StoredValue>(value);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->Value, 42);
}

TEST(PhoneApplicationServiceTest, CurrentIsOneServiceForTheWholeProcess)
{
    EXPECT_EQ(&PhoneApplicationService::getCurrentProperty(),
              &PhoneApplicationService::getCurrentProperty());
}
