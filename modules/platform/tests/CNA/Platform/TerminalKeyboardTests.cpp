// SPDX-License-Identifier: MS-PL
//
// PLAT-137/138: exact and timed-fallback terminal keyboard paths. Every interesting test owns a
// pseudo-terminal, so both protocols are exercised in CI where the test process itself has no tty.

#include "../../../src/Terminal/TerminalKeyboard.hpp"
#include "../../../src/Terminal/TerminalPlatform.hpp"
#include "../../../src/Terminal/TerminalSurfacePresenter.hpp"
#include "PseudoTerminalHarness.hpp"

#include <gtest/gtest.h>

#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
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
    return std::find(snapshot.pressedKeys.begin(), snapshot.pressedKeys.end(), key) !=
           snapshot.pressedKeys.end();
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

    ASSERT_TRUE(DecodeKittyKeyEvent("\x1b[57376;1:1u", event));
    EXPECT_EQ(event.keycode, KeyCode::F13);
    EXPECT_EQ(event.scancode, Scancode::F13)
        << "HID leaves a gap between F12 and F13";
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

TEST(TerminalKeyboardTest, LegacyInputDecodesAsciiControlNavigationAndFunctionKeys)
{
    KeyEvent event;
    ASSERT_TRUE(DecodeLegacyKeyEvent("w", event));
    EXPECT_EQ(event.keycode, KeyCode::W);
    EXPECT_EQ(event.scancode, Scancode::W);
    EXPECT_EQ(event.modifiers, 0u);

    ASSERT_TRUE(DecodeLegacyKeyEvent("W", event));
    EXPECT_EQ(event.keycode, KeyCode::W);
    EXPECT_TRUE(HasModifier(event.modifiers, KeyModifier::Shift));

    ASSERT_TRUE(DecodeLegacyKeyEvent("\x17", event));
    EXPECT_EQ(event.keycode, KeyCode::W);
    EXPECT_TRUE(HasModifier(event.modifiers, KeyModifier::Control));

    ASSERT_TRUE(DecodeLegacyKeyEvent("\b", event));
    EXPECT_EQ(event.keycode, KeyCode::Back);
    EXPECT_FALSE(HasModifier(event.modifiers, KeyModifier::Control));

    ASSERT_TRUE(DecodeLegacyKeyEvent("\x1b[1;6A", event));
    EXPECT_EQ(event.keycode, KeyCode::Up);
    EXPECT_TRUE(HasModifier(event.modifiers, KeyModifier::Shift));
    EXPECT_TRUE(HasModifier(event.modifiers, KeyModifier::Control));

    ASSERT_TRUE(DecodeLegacyKeyEvent("\x1b[3~", event));
    EXPECT_EQ(event.keycode, KeyCode::Delete);
    ASSERT_TRUE(DecodeLegacyKeyEvent("\x1bOS", event));
    EXPECT_EQ(event.keycode, KeyCode::F4);
    ASSERT_TRUE(DecodeLegacyKeyEvent("\x1b[25~", event));
    EXPECT_EQ(event.keycode, KeyCode::F13);
    EXPECT_EQ(event.scancode, Scancode::F13);

    ASSERT_TRUE(DecodeLegacyKeyEvent("\x1b" "a", event));
    EXPECT_EQ(event.keycode, KeyCode::A);
    EXPECT_TRUE(HasModifier(event.modifiers, KeyModifier::Alt));
    EXPECT_FALSE(event.repeat);
    EXPECT_TRUE(event.pressed);
}

TEST(TerminalKeyboardTest, LegacyPressGetsOneTimedSyntheticRelease)
{
    using namespace std::chrono_literals;
    PseudoTerminal pty;
    ASSERT_TRUE(pty.IsOpen());
    auto sessions = std::make_shared<TerminalSessionController>(
        pty.Device(), pty.Device(), /*kittyKeyboardSupported=*/false);
    auto decoder = std::make_shared<TerminalInputDecoder>(sessions);
    TerminalKeyboard keyboard(decoder);
    const auto start = std::chrono::steady_clock::now();

    decoder->PumpAt(start);
    EXPECT_EQ(pty.DrainOutput().find("\x1b[>15u"), std::string::npos);
    EXPECT_FALSE(pty.EchoIsOn());

    Send(pty, "W");
    decoder->PumpAt(start + 1ms);
    EXPECT_TRUE(HasPressedKey(keyboard.GetSnapshot(), KeyCode::W));
    EXPECT_TRUE(HasModifier(keyboard.GetSnapshot().modifiers, KeyModifier::Shift));

    std::vector<PlatformEvent> events;
    decoder->DrainEvents(events, 7);
    ASSERT_EQ(events.size(), 1u);
    const KeyEvent press = std::get<KeyEvent>(events.front());
    EXPECT_TRUE(press.pressed);
    EXPECT_FALSE(press.repeat);
    EXPECT_TRUE(HasModifier(press.modifiers, KeyModifier::Shift));
    EXPECT_EQ(press.window, 7u);

    decoder->PumpAt(start + 50ms);
    EXPECT_TRUE(HasPressedKey(keyboard.GetSnapshot(), KeyCode::W));
    decoder->PumpAt(start + 102ms);
    EXPECT_FALSE(HasPressedKey(keyboard.GetSnapshot(), KeyCode::W));
    EXPECT_FALSE(HasModifier(keyboard.GetSnapshot().modifiers, KeyModifier::Shift));

    events.clear();
    decoder->DrainEvents(events, 7);
    ASSERT_EQ(events.size(), 1u);
    const KeyEvent release = std::get<KeyEvent>(events.front());
    EXPECT_FALSE(release.pressed);
    EXPECT_FALSE(release.repeat);
    EXPECT_EQ(release.keycode, KeyCode::W);
}

TEST(TerminalKeyboardTest, LegacyRepeatRefreshesTheReleaseDeadline)
{
    using namespace std::chrono_literals;
    PseudoTerminal pty;
    ASSERT_TRUE(pty.IsOpen());
    auto sessions = std::make_shared<TerminalSessionController>(
        pty.Device(), pty.Device(), /*kittyKeyboardSupported=*/false);
    auto decoder = std::make_shared<TerminalInputDecoder>(sessions);
    const auto start = std::chrono::steady_clock::now();
    decoder->PumpAt(start);
    (void)pty.DrainOutput();

    Send(pty, "w");
    decoder->PumpAt(start + 1ms);
    Send(pty, "w");
    decoder->PumpAt(start + 80ms);

    std::vector<PlatformEvent> events;
    decoder->DrainEvents(events, 0);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_FALSE(std::get<KeyEvent>(events[0]).repeat);
    EXPECT_TRUE(std::get<KeyEvent>(events[1]).repeat);

    decoder->PumpAt(start + 150ms);
    EXPECT_TRUE(HasPressedKey(decoder->GetSnapshot(), KeyCode::W));
    decoder->PumpAt(start + 181ms);
    EXPECT_FALSE(HasPressedKey(decoder->GetSnapshot(), KeyCode::W));
}

TEST(TerminalKeyboardTest, SplitEscapeSequenceWinsOverTheStandaloneEscapeTimeout)
{
    using namespace std::chrono_literals;
    PseudoTerminal pty;
    ASSERT_TRUE(pty.IsOpen());
    auto sessions = std::make_shared<TerminalSessionController>(
        pty.Device(), pty.Device(), /*kittyKeyboardSupported=*/false);
    auto decoder = std::make_shared<TerminalInputDecoder>(sessions);
    const auto start = std::chrono::steady_clock::now();
    decoder->PumpAt(start);
    (void)pty.DrainOutput();

    Send(pty, "\x1b");
    decoder->PumpAt(start + 1ms);
    EXPECT_FALSE(HasPressedKey(decoder->GetSnapshot(), KeyCode::Escape));
    Send(pty, "[A");
    decoder->PumpAt(start + 10ms);
    EXPECT_TRUE(HasPressedKey(decoder->GetSnapshot(), KeyCode::Up));

    std::vector<PlatformEvent> events;
    decoder->DrainEvents(events, 0);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(std::get<KeyEvent>(events.front()).keycode, KeyCode::Up);

    Send(pty, "\x1b");
    decoder->PumpAt(start + 20ms);
    decoder->PumpAt(start + 51ms);
    EXPECT_TRUE(HasPressedKey(decoder->GetSnapshot(), KeyCode::Escape));

    events.clear();
    decoder->DrainEvents(events, 0);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(std::get<KeyEvent>(events.front()).keycode, KeyCode::Escape);
}

TEST(TerminalKeyboardTest, LegacySessionNeverPushesOrPopsTheKittyStack)
{
    PseudoTerminal pty;
    ASSERT_TRUE(pty.IsOpen());
    {
        auto sessions = std::make_shared<TerminalSessionController>(
            pty.Device(), pty.Device(), /*kittyKeyboardSupported=*/false);
        auto decoder = std::make_shared<TerminalInputDecoder>(sessions);
        decoder->Pump();
        EXPECT_EQ(pty.DrainOutput().find("\x1b[>15u"), std::string::npos);
        EXPECT_FALSE(pty.EchoIsOn());
    }

    EXPECT_TRUE(pty.EchoIsOn());
    const std::string restored = pty.DrainOutput();
    EXPECT_EQ(restored.find("\x1b[<u"), std::string::npos);
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
class KeyboardTerminalActor
{
public:
    KeyboardTerminalActor(const int controller, const bool kitty)
        : controller_(controller), kitty_(kitty), thread_([this] { Run(); })
    {
    }

    ~KeyboardTerminalActor()
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
            if (kitty_)
            {
                Answer(pending, "\x1b[?u", "\x1b[?15u");
            }
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
    bool kitty_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

TEST(TerminalKeyboardTest, PlatformAdvertisesExactStateOnlyAfterKittyWasDetected)
{
    PseudoTerminal pty;
    ASSERT_TRUE(pty.IsOpen());
    const KeyboardTerminalActor actor(pty.Controller(), /*kitty=*/true);

    TerminalPlatform platform(pty.Device(), pty.Device());
    const PlatformCapabilities capabilities = platform.GetCapabilities();
    EXPECT_TRUE(capabilities.exactKeyboardState);
    EXPECT_NE(platform.GetKeyboard(), nullptr);
    EXPECT_TRUE(platform.GetKeyboard()->HasKeyboard());
}

TEST(TerminalKeyboardTest, PlatformProvidesFallbackWhileReportingStateAsInexact)
{
    PseudoTerminal pty;
    ASSERT_TRUE(pty.IsOpen());
    const KeyboardTerminalActor actor(pty.Controller(), /*kitty=*/false);

    TerminalPlatform platform(pty.Device(), pty.Device());
    const PlatformCapabilities capabilities = platform.GetCapabilities();
    EXPECT_FALSE(capabilities.exactKeyboardState);
    ASSERT_NE(platform.GetKeyboard(), nullptr);
    EXPECT_TRUE(platform.GetKeyboard()->HasKeyboard());

    platform.GetKeyboard()->Update();
    EXPECT_FALSE(pty.EchoIsOn());
}

} // namespace
