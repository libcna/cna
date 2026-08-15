// SPDX-License-Identifier: MS-PL
//
// PLAT-130: terminal capability detection, driven by descriptors the test controls.
//
// The probe's whole difficulty is that a terminal cannot say "no" -- an unsupported query
// produces silence -- so every answer is a timed read, and a wrong timeout or a missing control
// query turns "capable" into "incapable" without any error anywhere. That is only testable if the
// thing on the other end can be made to answer some queries and not others, which is what the
// pseudo-terminal below is for.
//
// Probing the process's own stdin from a test would write escape sequences into whatever terminal
// was running the suite and swallow the developer's keystrokes waiting for a reply. The probe
// takes its descriptors as parameters precisely so this file never has to.

#include "../../../src/Terminal/TerminalCapabilityProbe.hpp"

#include <gtest/gtest.h>

#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>

namespace {

using namespace CNA::Platform::Terminal;

// --- not a terminal ----------------------------------------------------------------------------

TEST(TerminalCapabilityProbeTests, APipeIsNotATerminalAndNothingElseIsClaimed)
{
    // The case that matters most in practice: piped into a log, redirected to a file, captured by
    // CI. Detecting it is what lets the platform refuse instead of writing escape sequences into
    // somebody's build output.
    int ends[2] = {-1, -1};
    ASSERT_EQ(pipe(ends), 0);

    const TerminalCapabilities capabilities = DetectTerminalCapabilities(ends[1], ends[0], 20);

    EXPECT_FALSE(capabilities.isTerminal);
    EXPECT_FALSE(capabilities.canQuery);
    EXPECT_FALSE(capabilities.respondedToDeviceAttributes);
    EXPECT_FALSE(capabilities.hasKittyKeyboard);
    EXPECT_TRUE(capabilities.terminalName.empty());
    EXPECT_EQ(capabilities.colourDepth, TerminalColourDepth::Monochrome);

    close(ends[0]);
    close(ends[1]);
}

TEST(TerminalCapabilityProbeTests, DetectionOnAPipeReturnsPromptly)
{
    // Not a timing assertion about the machine -- an assertion that the not-a-terminal path never
    // reaches a query at all. If it did, this would sit through three timeouts.
    int ends[2] = {-1, -1};
    ASSERT_EQ(pipe(ends), 0);

    const auto before = std::chrono::steady_clock::now();
    (void)DetectTerminalCapabilities(ends[1], ends[0], 2000);
    const auto elapsed = std::chrono::steady_clock::now() - before;

    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 500);

    close(ends[0]);
    close(ends[1]);
}

// --- a real terminal, played by the test ---------------------------------------------------------

/// A pseudo-terminal whose controlling end this test drives, so the probe talks to something that
/// really is a tty and really can choose what to answer.
class PseudoTerminal
{
public:
    PseudoTerminal()
    {
        controller_ = posix_openpt(O_RDWR | O_NOCTTY);
        if (controller_ < 0 || grantpt(controller_) != 0 || unlockpt(controller_) != 0)
        {
            return;
        }
        const char* name = ptsname(controller_);
        if (name == nullptr)
        {
            return;
        }
        device_ = open(name, O_RDWR | O_NOCTTY);
    }

    ~PseudoTerminal()
    {
        if (device_ >= 0)
        {
            close(device_);
        }
        if (controller_ >= 0)
        {
            close(controller_);
        }
    }

    PseudoTerminal(const PseudoTerminal&) = delete;
    PseudoTerminal& operator=(const PseudoTerminal&) = delete;

    [[nodiscard]] bool IsOpen() const { return controller_ >= 0 && device_ >= 0; }

    /** The end the probe sees: a genuine terminal device. */
    [[nodiscard]] int Device() const { return device_; }

    /** The end this test plays the terminal from. */
    [[nodiscard]] int Controller() const { return controller_; }

private:
    int controller_ = -1;
    int device_ = -1;
};

/// Plays a terminal on the controller end: reads queries, answers the ones it was told to.
///
/// Runs on its own thread because the probe blocks waiting for replies -- a single-threaded test
/// would deadlock against itself.
class TerminalActor
{
public:
    TerminalActor(const int controller, const bool answerKitty,
                  const std::uint32_t kittyFlags = 15)
        : controller_(controller)
        , answerKitty_(answerKitty)
        , kittyFlags_(kittyFlags)
        , thread_([this] { Run(); })
    {
    }

    ~TerminalActor()
    {
        stop_ = true;
        thread_.join();
    }

    TerminalActor(const TerminalActor&) = delete;
    TerminalActor& operator=(const TerminalActor&) = delete;

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

            char buffer[256];
            const ssize_t count = read(controller_, buffer, sizeof(buffer));
            if (count <= 0)
            {
                continue;
            }
            pending.append(buffer, static_cast<std::size_t>(count));

            // Order matters: "\x1b[?u" contains "\x1b[" too, so the more specific queries are
            // matched first.
            if (Consume(pending, "\x1b[?u"))
            {
                if (answerKitty_)
                {
                    Reply("\x1b[?" + std::to_string(kittyFlags_) + "u");
                }
            }
            if (Consume(pending, "\x1b[>0q"))
            {
                Reply("\x1bP>|CnaTestTerm(1.0)\x1b\\");
            }
            if (Consume(pending, "\x1b[c"))
            {
                Reply("\x1b[?62;c");
            }
        }
    }

    static bool Consume(std::string& pending, const std::string& query)
    {
        const std::size_t at = pending.find(query);
        if (at == std::string::npos)
        {
            return false;
        }
        pending.erase(at, query.size());
        return true;
    }

    void Reply(const std::string& text) const
    {
        const ssize_t written = write(controller_, text.data(), text.size());
        (void)written;
    }

    int controller_;
    bool answerKitty_;
    std::uint32_t kittyFlags_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

TEST(TerminalCapabilityProbeTests, ATerminalAnsweringEveryQueryIsDetectedAsFullyCapable)
{
    PseudoTerminal pty;
    if (!pty.IsOpen())
    {
        GTEST_SKIP() << "this environment cannot allocate a pseudo-terminal";
    }
    const TerminalActor actor(pty.Controller(), /*answerKitty=*/true);

    const TerminalCapabilities capabilities =
        DetectTerminalCapabilities(pty.Device(), pty.Device(), 500);

    EXPECT_TRUE(capabilities.isTerminal);
    EXPECT_TRUE(capabilities.canQuery);
    EXPECT_TRUE(capabilities.respondedToDeviceAttributes);
    EXPECT_TRUE(capabilities.hasKittyKeyboard) << "PLAT-137's exact key-release path depends on this";
    EXPECT_EQ(capabilities.terminalName, "CnaTestTerm(1.0)");
}

TEST(TerminalCapabilityProbeTests, SilenceFromTheKittyQueryIsARealNegativeOnceDeviceAttributesAnswered)
{
    // The finding the whole spike existed to establish. With DA1 answered, silence from the Kitty
    // query means "not supported" rather than "nothing is listening" -- which is what makes
    // reporting exactKeyboardState = false honest rather than a guess, and sends Phase 10 down
    // PLAT-138's synthetic key-up path instead of PLAT-137's exact one.
    PseudoTerminal pty;
    if (!pty.IsOpen())
    {
        GTEST_SKIP() << "this environment cannot allocate a pseudo-terminal";
    }
    const TerminalActor actor(pty.Controller(), /*answerKitty=*/false);

    const TerminalCapabilities capabilities =
        DetectTerminalCapabilities(pty.Device(), pty.Device(), 500);

    EXPECT_TRUE(capabilities.respondedToDeviceAttributes);
    EXPECT_FALSE(capabilities.hasKittyKeyboard);
}

TEST(TerminalCapabilityProbeTests, PartialKittyEnhancementsAreNotAdvertisedAsExact)
{
    PseudoTerminal pty;
    if (!pty.IsOpen())
    {
        GTEST_SKIP() << "this environment cannot allocate a pseudo-terminal";
    }
    // Disambiguation plus event types, but no alternate keys or all-keys-as-CSI-u. In
    // particular ordinary WASD would still have no release events, so this is not exact.
    const TerminalActor actor(pty.Controller(), /*answerKitty=*/true, /*kittyFlags=*/3);

    const TerminalCapabilities capabilities =
        DetectTerminalCapabilities(pty.Device(), pty.Device(), 500);

    EXPECT_TRUE(capabilities.respondedToDeviceAttributes);
    EXPECT_FALSE(capabilities.hasKittyKeyboard);
}

TEST(TerminalCapabilityProbeTests, ATerminalThatAnswersNothingLeavesEveryQueriedCapabilityFalse)
{
    // No actor at all: the device is a genuine tty, but nobody is on the other end. DA1 goes
    // unanswered, so nothing after it is trusted -- silence here is ambiguous, not a negative.
    PseudoTerminal pty;
    if (!pty.IsOpen())
    {
        GTEST_SKIP() << "this environment cannot allocate a pseudo-terminal";
    }

    const TerminalCapabilities capabilities =
        DetectTerminalCapabilities(pty.Device(), pty.Device(), 20);

    EXPECT_TRUE(capabilities.isTerminal);
    EXPECT_TRUE(capabilities.canQuery);
    EXPECT_FALSE(capabilities.respondedToDeviceAttributes);
    EXPECT_FALSE(capabilities.hasKittyKeyboard);
    EXPECT_TRUE(capabilities.terminalName.empty());
}

TEST(TerminalCapabilityProbeTests, DetectionRestoresTheTerminalItBorrowed)
{
    // A probe that left a terminal in raw mode would leave a developer's shell without echo. The
    // guard is RAII, but "the destructor exists" is not the claim worth testing -- "the settings
    // that went in came back out" is.
    PseudoTerminal pty;
    if (!pty.IsOpen())
    {
        GTEST_SKIP() << "this environment cannot allocate a pseudo-terminal";
    }

    termios before{};
    ASSERT_EQ(tcgetattr(pty.Device(), &before), 0);
    ASSERT_NE(before.c_lflag & static_cast<tcflag_t>(ECHO), 0u) << "a fresh pty should start with echo";

    (void)DetectTerminalCapabilities(pty.Device(), pty.Device(), 20);

    termios after{};
    ASSERT_EQ(tcgetattr(pty.Device(), &after), 0);
    EXPECT_EQ(before.c_lflag, after.c_lflag);
    EXPECT_EQ(before.c_iflag, after.c_iflag);
    EXPECT_EQ(before.c_oflag, after.c_oflag);
    EXPECT_EQ(before.c_cc[VMIN], after.c_cc[VMIN]);
    EXPECT_EQ(before.c_cc[VTIME], after.c_cc[VTIME]);
}

// --- colour depth ------------------------------------------------------------------------------

TEST(TerminalCapabilityProbeTests, ColourDepthNamesAreStable)
{
    EXPECT_EQ(ToString(TerminalColourDepth::Monochrome), "Monochrome");
    EXPECT_EQ(ToString(TerminalColourDepth::Ansi16), "Ansi16");
    EXPECT_EQ(ToString(TerminalColourDepth::Indexed256), "Indexed256");
    EXPECT_EQ(ToString(TerminalColourDepth::TrueColour), "TrueColour");
}

/// Sets an environment variable for the duration of a test and puts it back afterwards, so the
/// suite stays order-independent under --gtest_shuffle.
class ScopedEnvironment
{
public:
    ScopedEnvironment(std::string name, const char* value) : name_(std::move(name))
    {
        const char* previous = getenv(name_.c_str());
        if (previous != nullptr)
        {
            had_ = true;
            previous_ = previous;
        }
        if (value != nullptr)
        {
            setenv(name_.c_str(), value, 1);
        }
        else
        {
            unsetenv(name_.c_str());
        }
    }

    ~ScopedEnvironment()
    {
        if (had_)
        {
            setenv(name_.c_str(), previous_.c_str(), 1);
        }
        else
        {
            unsetenv(name_.c_str());
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

private:
    std::string name_;
    std::string previous_;
    bool had_ = false;
};

TEST(TerminalCapabilityProbeTests, ColourDepthComesFromTheEnvironmentWhenATerminalIsPresent)
{
    PseudoTerminal pty;
    if (!pty.IsOpen())
    {
        GTEST_SKIP() << "this environment cannot allocate a pseudo-terminal";
    }

    const struct
    {
        const char* colorterm;
        const char* term;
        TerminalColourDepth expected;
    } cases[] = {
        {"truecolor", "xterm", TerminalColourDepth::TrueColour},
        {"24bit", "xterm", TerminalColourDepth::TrueColour},
        {nullptr, "xterm-direct", TerminalColourDepth::TrueColour},
        {nullptr, "xterm-256color", TerminalColourDepth::Indexed256},
        // TERM naming no depth still means the 16 ANSI colours: they predate everything else
        // here, so assuming them degrades gracefully where assuming none throws away colour the
        // terminal really has.
        {nullptr, "xterm", TerminalColourDepth::Ansi16},
        {nullptr, "dumb", TerminalColourDepth::Monochrome},
        {nullptr, nullptr, TerminalColourDepth::Monochrome},
    };

    for (const auto& testCase : cases)
    {
        const ScopedEnvironment colorterm("COLORTERM", testCase.colorterm);
        const ScopedEnvironment term("TERM", testCase.term);

        const TerminalCapabilities capabilities =
            DetectTerminalCapabilities(pty.Device(), pty.Device(), 5);
        EXPECT_EQ(capabilities.colourDepth, testCase.expected)
            << "COLORTERM=" << (testCase.colorterm != nullptr ? testCase.colorterm : "(unset)")
            << " TERM=" << (testCase.term != nullptr ? testCase.term : "(unset)");
    }
}

} // namespace
