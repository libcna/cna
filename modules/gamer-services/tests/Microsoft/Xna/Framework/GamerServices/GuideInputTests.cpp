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
#include "CNA/Platform/Input/IPlatformTextInput.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
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
class TextInput final : public CNA::Platform::IPlatformTextInput {
public:
    bool active = false;
    void Start(CNA::Platform::WindowId, CNA::Platform::TextInputType) override { active = true; }
    void Stop(CNA::Platform::WindowId) override { active = false; }
    [[nodiscard]] bool IsActive(CNA::Platform::WindowId) const override { return active; }
    [[nodiscard]] bool IsScreenKeyboardShown(CNA::Platform::WindowId) const override { return false; }
    void SetInputArea(CNA::Platform::WindowId, const CNA::Platform::TextInputArea&) override {}
};
class Devices final : public CNA::Platform::Testing::PlatformTestDecorator {
public:
    Keyboard keyboard;
    CNA::Platform::Testing::CannedGamepad gamepad;
    TextInput text;
    [[nodiscard]] CNA::Platform::IPlatformKeyboard* GetKeyboard() override { return &keyboard; }
    [[nodiscard]] CNA::Platform::IPlatformGamepad* GetGamepad() override { return &gamepad; }
    [[nodiscard]] CNA::Platform::IPlatformTextInput* GetTextInput() override { return &text; }
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
    // While the Guide is up the game reads no keys.
    frame({KeyCode::Down});
    EXPECT_TRUE(Microsoft::Xna::Framework::Input::Keyboard::GetState().GetPressedKeys().empty());
    frame({});
    frame({KeyCode::Escape});
    EXPECT_FALSE(Ui::visible());
    // The Escape that closed it is still held: the game does not see it until it is released.
    using Microsoft::Xna::Framework::Input::Keys;
    EXPECT_TRUE(Microsoft::Xna::Framework::Input::Keyboard::GetState().IsKeyUp(Keys::Escape));
    devices.keyboard.snapshot.pressedKeys = {};
    (void)Microsoft::Xna::Framework::Input::Keyboard::GetState();
    devices.keyboard.snapshot.pressedKeys = {KeyCode::Escape};
    EXPECT_TRUE(Microsoft::Xna::Framework::Input::Keyboard::GetState().IsKeyDown(Keys::Escape));
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

// The sign-in picker takes typing as the start of a name: the platform must deliver text while it is
// on top, and text input is off again once it closes.
TEST_F(GuideInputTest, TheSignInPickerTurnsTextInputOnWhileItIsUp) {
    using Microsoft::Xna::Framework::Input::TextInputEXT;
    Devices devices;
    CNA::Platform::Testing::ScopedCurrentPlatform scope(devices);
    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
    // A game window, as text input needs one.
    TextInputEXT::setWindowHandleProperty(1);
    TextInputEXT::INTERNAL_setWindowId(1);
    struct Restore { ~Restore() { TextInputEXT::setWindowHandleProperty(0); } } restore;
    const bool before = TextInputEXT::IsTextInputActive();
    Guide::ShowSignIn(2, false);
    ASSERT_EQ("signIn", Ui::currentScreenForTesting());
    Ui::draw(device);
    Ui::draw(device);
    EXPECT_TRUE(TextInputEXT::IsTextInputActive());
    Ui::sendForTesting(Ui::Command::Back);
    EXPECT_FALSE(Ui::visible());
    EXPECT_EQ(before, TextInputEXT::IsTextInputActive());
}
