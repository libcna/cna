// SPDX-License-Identifier: MS-PL
//
// PLAT-137: the exact terminal-keyboard path. Every interesting test owns a pseudo-terminal, so
// the protocol is exercised in CI where the test process itself has no tty.

#include "../../../src/Terminal/TerminalKeyboard.hpp"
#include "../../../src/Terminal/TerminalPlatform.hpp"
#include "../../../src/Terminal/TerminalSurfacePresenter.hpp"
#include "PseudoTerminalHarness.hpp"

#include <gtest/gtest.h>

#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Terminal;
using CNA::Platform::TestSupport::PseudoTerminal;

bool HasPressedKey(const KeyboardSnapshot& snapshot, const KeyCode key)
{
    return std::find(snapshot.pressedKeys.begin(), snapshot.pressedKeys.end(),
                     static_cast<std::uint32_t>(key)) != snapshot.pressedKeys.end();
}

void Send(const PseudoTerminal& pty, const std::string& bytes)
{
    ASSERT_EQ(write(pty.Controller(), bytes.data(), bytes.size()),
              static_cast<ssize_t>(bytes.size()));
}

TEST(TerminalKeyboardTest, CanonicalEventsDecodePressRepeatReleaseAndModifiers)
{
    KeyEvent event;
    ASSERT_TRUE(DecodeKittyKeyEvent("\x1b[119;6:1u", event));
    EXPECT_EQ(event.keycode, KeyCode::W);
    EXPECT_EQ(event.scancode, Scancode::W);
    EXPECT_TRUE(event.pressed);
    EXPECT_FALSE(event.repeat);
    EXPECT_TRUE(HasModifier(event.modifiers, KeyModifier::Shift));
    EXPECT_TRUE(HasModifier(event.modifiers, KeyModifier::Control));

    ASSERT_TRUE(DecodeKittyKeyEvent("\x1b[119;1:2u", event));
    EXPECT_TRUE(event.pressed);
    EXPECT_TRUE(event.repeat);

    ASSERT_TRUE(DecodeKittyKeyEvent("\x1b[119;1:3u", event));
    EXPECT_FALSE(event.pressed);
    EXPECT_FALSE(event.repeat);
}

TEST(TerminalKeyboardTest, BaseLayoutAlternateKeyDeterminesThePhysicalScancode)
{
    // Primary A is what the current layout means; base-layout Q is where the key physically is.
    // This is the AZERTY distinction the contract's keycode/scancode split exists to preserve.
    KeyEvent event;
    ASSERT_TRUE(DecodeKittyKeyEvent("\x1b[97:65:113;1:1u", event));
    EXPECT_EQ(event.keycode, KeyCode::A);
    EXPECT_EQ(event.scancode, Scancode::Q);
}

TEST(TerminalKeyboardTest, FunctionalAndModifierKeysUseKittysPrivateUseTable)
{
    KeyEvent event;
    ASSERT_TRUE(DecodeKittyKeyEvent("\x1b[57352;1:1u", event));
    EXPECT_EQ(event.keycode, KeyCode::Up);
    EXPECT_EQ(event.scancode, Scancode::Up);

    ASSERT_TRUE(DecodeKittyKeyEvent("\x1b[57441;2:1u", event));
    EXPECT_EQ(event.keycode, KeyCode::LeftShift);
    EXPECT_EQ(event.scancode, Scancode::LeftShift);
}

TEST(TerminalKeyboardTest, KeypadKeysKeepTheirVirtualAndPhysicalIdentities)
{
    KeyEvent event;
    ASSERT_TRUE(DecodeKittyKeyEvent("\x1b[57400;1:1u", event));
    EXPECT_EQ(event.keycode, KeyCode::NumPad1);
    EXPECT_EQ(event.scancode, Scancode::Keypad1);

    ASSERT_TRUE(DecodeKittyKeyEvent("\x1b[57414;1:1u", event));
    EXPECT_EQ(event.keycode, KeyCode::Enter);
    EXPECT_EQ(event.scancode, Scancode::KeypadEnter);
}

TEST(TerminalKeyboardTest, MalformedInputIsRejectedWithoutTouchingTheOutput)
{
    KeyEvent event;
    event.keycode = KeyCode::F24;
    EXPECT_FALSE(DecodeKittyKeyEvent("not a key", event));
    EXPECT_EQ(event.keycode, KeyCode::F24);
    EXPECT_FALSE(DecodeKittyKeyEvent("\x1b[119;1:9u", event));
    EXPECT_EQ(event.keycode, KeyCode::F24);
}

TEST(TerminalKeyboardTest, SnapshotRemainsHeldAcrossRepeatAndClearsOnlyOnRealRelease)
{
    PseudoTerminal pty;
    ASSERT_TRUE(pty.IsOpen());
    auto sessions = std::make_shared<TerminalSessionController>(
        pty.Device(), pty.Device(), /*kittyKeyboardSupported=*/true);
    auto decoder = std::make_shared<TerminalInputDecoder>(sessions);
    TerminalKeyboard keyboard(decoder);

    keyboard.Update();
    const std::string prologue = pty.DrainOutput();
    EXPECT_NE(prologue.find("\x1b[>15u"), std::string::npos)
        << "all keys plus event types must be enabled before reading";

    Send(pty, "\x1b[119;1:1u");
    keyboard.Update();
    EXPECT_TRUE(HasPressedKey(keyboard.GetSnapshot(), KeyCode::W));

    Send(pty, "\x1b[119;1:2u");
    keyboard.Update();
    EXPECT_TRUE(HasPressedKey(keyboard.GetSnapshot(), KeyCode::W));
    EXPECT_EQ(keyboard.GetSnapshot().pressedKeys.size(), 1u)
        << "repeat must not duplicate a held key";

    Send(pty, "\x1b[119;1:3u");
    keyboard.Update();
    EXPECT_FALSE(HasPressedKey(keyboard.GetSnapshot(), KeyCode::W));
}

TEST(TerminalKeyboardTest, SplitSequencesWaitForTheirFinalByte)
{
    PseudoTerminal pty;
    ASSERT_TRUE(pty.IsOpen());
    auto sessions = std::make_shared<TerminalSessionController>(
        pty.Device(), pty.Device(), /*kittyKeyboardSupported=*/true);
    auto decoder = std::make_shared<TerminalInputDecoder>(sessions);
    TerminalKeyboard keyboard(decoder);
    keyboard.Update();
    (void)pty.DrainOutput();

    Send(pty, "\x1b[97;1:");
    keyboard.Update();
    EXPECT_FALSE(HasPressedKey(keyboard.GetSnapshot(), KeyCode::A));

    Send(pty, "1u");
    keyboard.Update();
    EXPECT_TRUE(HasPressedKey(keyboard.GetSnapshot(), KeyCode::A));
}

TEST(TerminalKeyboardTest, ReleasingOneOfTwoPhysicalEnterKeysKeepsEnterHeld)
{
    PseudoTerminal pty;
    ASSERT_TRUE(pty.IsOpen());
    auto sessions = std::make_shared<TerminalSessionController>(
        pty.Device(), pty.Device(), /*kittyKeyboardSupported=*/true);
    auto decoder = std::make_shared<TerminalInputDecoder>(sessions);
    TerminalKeyboard keyboard(decoder);
    keyboard.Update();
    (void)pty.DrainOutput();

    Send(pty, "\x1b[13;1:1u\x1b[57414;1:1u");
    keyboard.Update();
    EXPECT_TRUE(HasPressedKey(keyboard.GetSnapshot(), KeyCode::Enter));

    Send(pty, "\x1b[13;1:3u");
    keyboard.Update();
    EXPECT_TRUE(HasPressedKey(keyboard.GetSnapshot(), KeyCode::Enter));

    Send(pty, "\x1b[57414;1:3u");
    keyboard.Update();
    EXPECT_FALSE(HasPressedKey(keyboard.GetSnapshot(), KeyCode::Enter));
}

TEST(TerminalKeyboardTest, EventPumpAndSnapshotShareOneReadWithoutLosingEvents)
{
    PseudoTerminal pty;
    ASSERT_TRUE(pty.IsOpen());
    auto sessions = std::make_shared<TerminalSessionController>(
        pty.Device(), pty.Device(), /*kittyKeyboardSupported=*/true);
    auto decoder = std::make_shared<TerminalInputDecoder>(sessions);
    TerminalKeyboard keyboard(decoder);
    keyboard.Update();
    (void)pty.DrainOutput();

    Send(pty, "\x1b[97;1:1u\x1b[98;1:1u");
    keyboard.Update();  // consumes the bytes first

    std::vector<PlatformEvent> events;
    decoder->Pump();     // sees no bytes, but must not erase the queued events
    decoder->DrainEvents(events, 42);
    ASSERT_EQ(events.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<KeyEvent>(events[0]));
    EXPECT_EQ(std::get<KeyEvent>(events[0]).window, 42u);
    EXPECT_EQ(std::get<KeyEvent>(events[0]).keycode, KeyCode::A);
    EXPECT_EQ(std::get<KeyEvent>(events[1]).keycode, KeyCode::B);
    EXPECT_TRUE(HasPressedKey(keyboard.GetSnapshot(), KeyCode::A));
    EXPECT_TRUE(HasPressedKey(keyboard.GetSnapshot(), KeyCode::B));
}

TEST(TerminalKeyboardTest, KeyboardAndPresenterShareTheOneProcessSession)
{
    PseudoTerminal pty(8, 4);
    ASSERT_TRUE(pty.IsOpen());
    auto sessions = std::make_shared<TerminalSessionController>(
        pty.Device(), pty.Device(), /*kittyKeyboardSupported=*/true);

    {
        auto decoder = std::make_shared<TerminalInputDecoder>(sessions);
        TerminalKeyboard keyboard(decoder);
        keyboard.Update();
        (void)pty.DrainOutput();
        EXPECT_FALSE(pty.EchoIsOn());

        {
            TerminalSurfacePresenter presenter(sessions, TerminalColourDepth::Monochrome, 8, 8);
            const std::string upgraded = pty.DrainOutput();
            EXPECT_NE(upgraded.find("\x1b[?1049h"), std::string::npos);
            EXPECT_NE(upgraded.find("\x1b[>15u"), std::string::npos);
        }

        // Presentation released its lease, but the keyboard still needs raw exact input.
        EXPECT_FALSE(pty.EchoIsOn());
        const std::string downgraded = pty.DrainOutput();
        EXPECT_NE(downgraded.find("\x1b[?1049l"), std::string::npos);
        EXPECT_NE(downgraded.find("\x1b[>15u"), std::string::npos);
    }

    EXPECT_TRUE(pty.EchoIsOn());
    EXPECT_NE(pty.DrainOutput().find("\x1b[<u"), std::string::npos);
}

/// Answers the three capability queries while ignoring later session-control sequences.
class KittyTerminalActor
{
public:
    explicit KittyTerminalActor(const int controller)
        : controller_(controller), thread_([this] { Run(); })
    {
    }

    ~KittyTerminalActor()
    {
        stop_ = true;
        thread_.join();
    }

private:
    void Run()
    {
        std::string pending;
        while (!stop_)
        {
            pollfd waiting{};
            waiting.fd = controller_;
            waiting.events = POLLIN;
            if (poll(&waiting, 1, 20) <= 0)
            {
                continue;
            }
            char bytes[256];
            const ssize_t count = read(controller_, bytes, sizeof(bytes));
            if (count <= 0)
            {
                continue;
            }
            pending.append(bytes, static_cast<std::size_t>(count));
            Answer(pending, "\x1b[?u", "\x1b[?15u");
            Answer(pending, "\x1b[>0q", "\x1bP>|CnaKittyTest\x1b\\");
            Answer(pending, "\x1b[c", "\x1b[?62;c");
        }
    }

    void Answer(std::string& pending, const std::string& query, const std::string& reply) const
    {
        const std::size_t at = pending.find(query);
        if (at == std::string::npos)
        {
            return;
        }
        pending.erase(at, query.size());
        (void)write(controller_, reply.data(), reply.size());
    }

    int controller_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

TEST(TerminalKeyboardTest, PlatformAdvertisesExactStateOnlyAfterKittyWasDetected)
{
    PseudoTerminal pty;
    ASSERT_TRUE(pty.IsOpen());
    const KittyTerminalActor actor(pty.Controller());

    TerminalPlatform platform(pty.Device(), pty.Device());
    const PlatformCapabilities capabilities = platform.GetCapabilities();
    EXPECT_TRUE(capabilities.exactKeyboardState);
    EXPECT_NE(platform.GetKeyboard(), nullptr);
    EXPECT_TRUE(platform.GetKeyboard()->HasKeyboard());
}

} // namespace
