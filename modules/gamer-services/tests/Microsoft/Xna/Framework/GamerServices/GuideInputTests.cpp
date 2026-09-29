// SPDX-License-Identifier: MS-PL
// The Guide's own frame driven by real devices: a canned keyboard and a canned controller behind
// the platform. (Where notifications appear is GuideNotificationTest's.)
#include <gtest/gtest.h>
#include "../../../../../../platform/tests/CNA/Platform/CannedGamepad.hpp"
#include "../../../../../../platform/tests/CNA/Platform/PlatformTestDecorator.hpp"
#include "../../../../../src/Internal/GuideOverlay.hpp"
#include "../../../../../src/Internal/Guide/GuideUi.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
#include "System/IServiceProvider.hpp"
#include <chrono>
#include <thread>

namespace Service = CNA::Internal::GamerServices;
namespace Ui = CNA::Internal::GamerServices::GuideUi;
using namespace Microsoft::Xna::Framework::GamerServices;
using Microsoft::Xna::Framework::PlayerIndex;
using CNA::Platform::KeyCode;

namespace {
struct Provider final : System::IServiceProvider {
    void* GetService(const std::type_info&) const override { return nullptr; }
};
class Keyboard final : public CNA::Platform::IPlatformKeyboard {
public:
    CNA::Platform::KeyboardSnapshot snapshot;
    void Update() override {}
    [[nodiscard]] const CNA::Platform::KeyboardSnapshot& GetSnapshot() const override { return snapshot; }
    [[nodiscard]] bool HasKeyboard() const override { return true; }
};
class Devices final : public CNA::Platform::Testing::PlatformTestDecorator {
public:
    Keyboard keyboard;
    CNA::Platform::Testing::CannedGamepad gamepad;
    [[nodiscard]] CNA::Platform::IPlatformKeyboard* GetKeyboard() override { return &keyboard; }
    [[nodiscard]] CNA::Platform::IPlatformGamepad* GetGamepad() override { return &gamepad; }
};

class GuideInputTest : public ::testing::Test {
protected:
    void SetUp() override {
        previous_ = Service::backend();
        Service::ServiceIdentity alice;
        alice.userId = "a";
        alice.gamertag = "Alice";
        service_ = Service::makeFakeBackend({alice});
        Service::setBackendForTesting(service_);
        Service::resetInvitationsForTesting();
        if (!GamerServicesDispatcher::getIsInitializedProperty()) GamerServicesDispatcher::Initialize(provider_);
        service_->signIn(0, "Alice", "fixture");
        Settle([] { return Gamer::getSignedInGamersProperty()->getCountProperty() == 1; });
    }
    void TearDown() override {
        Ui::closeAll();
        service_->signOut(0);
        Settle([] { return Gamer::getSignedInGamersProperty()->getCountProperty() == 0; });
        Service::setBackendForTesting(previous_);
    }
    template <typename Condition>
    static bool Settle(Condition condition) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!condition()) {
            if (std::chrono::steady_clock::now() > deadline) return false;
            GamerServicesDispatcher::Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
    Provider provider_;
    std::shared_ptr<Service::IGamerServicesBackend> previous_, service_;
};
}

TEST_F(GuideInputTest, AKeyboardDrivesTheGuideFrame) {
    Devices devices;
    CNA::Platform::Testing::ScopedCurrentPlatform scope(devices);
    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
    auto frame = [&](std::vector<KeyCode> keys) {
        devices.keyboard.snapshot.pressedKeys = std::move(keys);
        Ui::draw(device);
    };
    Ui::open(Ui::homeScreen(PlayerIndex::One), PlayerIndex::One);
    // The key already down when the Guide opens does nothing; then Down moves, Enter chooses.
    frame({KeyCode::Enter});
    EXPECT_EQ("home", Ui::currentScreenForTesting());
    frame({});
    frame({KeyCode::Down});
    EXPECT_EQ(1, Ui::focusForTesting());
    frame({});
    frame({KeyCode::Enter});
    EXPECT_EQ("messages", Ui::currentScreenForTesting());
    // E switches the rail's category; Escape leaves the root and closes the Guide.
    frame({});
    frame({KeyCode::E});
    EXPECT_EQ("achievements", Ui::currentScreenForTesting());
    frame({});
    frame({KeyCode::Escape});
    EXPECT_FALSE(Ui::visible());
}

TEST_F(GuideInputTest, AControllerDrivesTheGuideFrame) {
    Devices devices;
    CNA::Platform::Testing::ScopedCurrentPlatform scope(devices);
    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
    using Microsoft::Xna::Framework::Input::Buttons;
    auto frame = [&](std::uint32_t buttons) {
        CNA::Platform::GamepadSnapshot snapshot;
        snapshot.connected = true;
        snapshot.buttons = buttons;
        devices.gamepad.SetPendingSnapshot(0, snapshot);
        devices.gamepad.Update();
        Ui::draw(device);
    };
    Ui::open(Ui::homeScreen(PlayerIndex::One), PlayerIndex::One);
    frame(0);
    frame(static_cast<std::uint32_t>(Buttons::DPadDown));
    frame(0);
    frame(static_cast<std::uint32_t>(Buttons::DPadDown));
    EXPECT_EQ(2, Ui::focusForTesting());
    frame(0);
    frame(static_cast<std::uint32_t>(Buttons::A));
    EXPECT_EQ("achievements", Ui::currentScreenForTesting());
    // LB goes back along the rail; B closes.
    frame(0);
    frame(static_cast<std::uint32_t>(Buttons::LeftShoulder));
    EXPECT_EQ("messages", Ui::currentScreenForTesting());
    frame(0);
    frame(static_cast<std::uint32_t>(Buttons::B));
    EXPECT_FALSE(Ui::visible());
}
