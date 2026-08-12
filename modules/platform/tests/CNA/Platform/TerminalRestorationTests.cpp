// SPDX-License-Identifier: MS-PL
//
// PLAT-131: the terminal comes back, on every exit path including the ones that kill the process.
//
// A process that dies while holding the terminal leaves a real person with no echo, an invisible
// cursor, their scrollback replaced by half a frame, and a shell that looks broken. It is a
// hostile failure, and it is likeliest on exactly the paths nobody tests — a crash, an assertion,
// Ctrl-C, the window being closed.
//
// Four of the five paths destroy the process, so they cannot be asserted from inside this binary.
// Each is driven by spawning tools/platform/terminal_restoration_harness.cpp under a
// pseudo-terminal this test owns, and then checking that terminal — which this process still
// holds — after the child is gone.
//
// The `leak` mode is why the rest of these are worth anything. It takes the terminal over and
// calls _exit(), which runs no destructor, no atexit handler and no signal handler, so the
// terminal genuinely stays raw. Its assertions are the inverse of every other mode's. Without it
// a bug that made all these checks vacuous — a pty that reset itself, an assertion that could not
// fail — would look exactly like success.

#include "../../../src/Terminal/TerminalSession.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <gtest/gtest.h>

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <cctype>
#include <cstdlib>
#include <string>

extern char** environ;

namespace {

using namespace CNA::Platform::Terminal;

const char* HarnessPath()
{
#if defined(CNA_PLATFORM_TERMINAL_RESTORATION_HARNESS_PATH)
    return CNA_PLATFORM_TERMINAL_RESTORATION_HARNESS_PATH;
#else
    return nullptr;
#endif
}

/// A pseudo-terminal this test owns, plays and inspects.
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
    [[nodiscard]] int Device() const { return device_; }
    [[nodiscard]] int Controller() const { return controller_; }

    /// Everything the terminal has been sent that has not been read yet.
    [[nodiscard]] std::string DrainOutput(const int quietMilliseconds = 120) const
    {
        std::string collected;
        char buffer[1024];
        for (;;)
        {
            pollfd waiting{};
            waiting.fd = controller_;
            waiting.events = POLLIN;
            if (poll(&waiting, 1, quietMilliseconds) <= 0)
            {
                break;
            }
            const ssize_t count = read(controller_, buffer, sizeof(buffer));
            if (count <= 0)
            {
                break;
            }
            collected.append(buffer, static_cast<std::size_t>(count));
            if (collected.size() > 65536)
            {
                break;
            }
        }
        return collected;
    }

    [[nodiscard]] bool EchoIsOn() const
    {
        termios attributes{};
        if (tcgetattr(device_, &attributes) != 0)
        {
            return false;
        }
        return (attributes.c_lflag & static_cast<tcflag_t>(ECHO)) != 0;
    }

private:
    int controller_ = -1;
    int device_ = -1;
};

struct HarnessResult
{
    bool spawned = false;
    int status = 0;
    std::string terminalOutput;
    bool echoRestored = false;
};

/// Runs the harness with the pty's device end as its stdin, stdout and stderr, then reports what
/// the terminal looks like afterwards.
HarnessResult RunHarness(const std::string& mode)
{
    HarnessResult result;
    const char* path = HarnessPath();
    if (path == nullptr)
    {
        return result;
    }

    PseudoTerminal pty;
    if (!pty.IsOpen())
    {
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pty.Device(), STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pty.Device(), STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pty.Device(), STDERR_FILENO);

    char* argv[] = {const_cast<char*>(path), const_cast<char*>(mode.c_str()), nullptr};
    pid_t child = 0;
    const int spawned = posix_spawn(&child, path, &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);

    if (spawned != 0)
    {
        return result;
    }
    result.spawned = true;

    // Drain while the child runs: a pty buffer that fills would block the child mid-restore, and
    // the restoring write is the very thing being measured.
    result.terminalOutput = pty.DrainOutput();
    waitpid(child, &result.status, 0);
    result.terminalOutput += pty.DrainOutput();

    result.echoRestored = pty.EchoIsOn();
    return result;
}

bool HasEpilogue(const std::string& output)
{
    // Asserted against the sequence the session actually builds rather than a literal copied by
    // hand, so a change to one has to be a change to both.
    TerminalSessionOptions everything;
    everything.mouseReporting = true;
    return output.find(BuildSessionEpilogue(everything)) != std::string::npos;
}

class TerminalRestoration : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (HarnessPath() == nullptr)
        {
            GTEST_SKIP() << "the restoration harness was not built in this configuration";
        }
    }
};

// --- the paths that destroy the process ---------------------------------------------------------

TEST_F(TerminalRestoration, NormalExitGivesTheTerminalBack)
{
    const HarnessResult result = RunHarness("normal");
    ASSERT_TRUE(result.spawned);
    EXPECT_TRUE(WIFEXITED(result.status));
    EXPECT_EQ(WEXITSTATUS(result.status), 0);
    EXPECT_TRUE(result.echoRestored);
    EXPECT_TRUE(HasEpilogue(result.terminalOutput));
}

TEST_F(TerminalRestoration, CtrlCGivesTheTerminalBackAndStillKillsTheProcess)
{
    // Both halves matter. Restoring but swallowing the signal would leave a program that cannot
    // be interrupted; dying without restoring is the hostile failure this task exists to prevent.
    const HarnessResult result = RunHarness("sigint");
    ASSERT_TRUE(result.spawned);
    EXPECT_TRUE(WIFSIGNALED(result.status)) << "the process must still die of the signal it was sent";
    EXPECT_EQ(WTERMSIG(result.status), SIGINT);
    EXPECT_TRUE(result.echoRestored);
    EXPECT_TRUE(HasEpilogue(result.terminalOutput));
}

TEST_F(TerminalRestoration, SigtermGivesTheTerminalBack)
{
    const HarnessResult result = RunHarness("sigterm");
    ASSERT_TRUE(result.spawned);
    EXPECT_TRUE(WIFSIGNALED(result.status));
    EXPECT_EQ(WTERMSIG(result.status), SIGTERM);
    EXPECT_TRUE(result.echoRestored);
    EXPECT_TRUE(HasEpilogue(result.terminalOutput));
}

TEST_F(TerminalRestoration, SighupGivesTheTerminalBack)
{
    const HarnessResult result = RunHarness("sighup");
    ASSERT_TRUE(result.spawned);
    EXPECT_TRUE(WIFSIGNALED(result.status));
    EXPECT_EQ(WTERMSIG(result.status), SIGHUP);
    EXPECT_TRUE(result.echoRestored);
    EXPECT_TRUE(HasEpilogue(result.terminalOutput));
}

TEST_F(TerminalRestoration, AbortGivesTheTerminalBack)
{
    // How a failed assertion arrives, which makes this the path a developer hits most often.
    const HarnessResult result = RunHarness("abort");
    ASSERT_TRUE(result.spawned);
    EXPECT_TRUE(WIFSIGNALED(result.status));
    EXPECT_EQ(WTERMSIG(result.status), SIGABRT);
    EXPECT_TRUE(result.echoRestored);
    EXPECT_TRUE(HasEpilogue(result.terminalOutput));
}

TEST_F(TerminalRestoration, AnUncaughtExceptionGivesTheTerminalBackBeforeItsMessageIsPrinted)
{
    // The destructor cannot help here: an uncaught exception calls std::terminate *without*
    // unwinding, so restoration has to come from std::set_terminate. And it has to come first --
    // the runtime's "terminate called after throwing..." message printed while the alternate
    // screen is still up would scroll away with it, turning a diagnosable crash into a process
    // that disappeared for no visible reason.
    const HarnessResult result = RunHarness("throw");
    ASSERT_TRUE(result.spawned);
    EXPECT_TRUE(WIFSIGNALED(result.status));
    EXPECT_EQ(WTERMSIG(result.status), SIGABRT);
    EXPECT_TRUE(result.echoRestored);
    ASSERT_TRUE(HasEpilogue(result.terminalOutput));

    TerminalSessionOptions everything;
    everything.mouseReporting = true;
    const std::size_t restoredAt = result.terminalOutput.find(BuildSessionEpilogue(everything));
    const std::size_t messageAt = result.terminalOutput.find("PLAT-131 uncaught exception");
    if (messageAt != std::string::npos)
    {
        EXPECT_LT(restoredAt, messageAt)
            << "the terminal must be restored before the crash message is printed into it";
    }
}

// --- the control ---------------------------------------------------------------------------------

TEST_F(TerminalRestoration, ExitingWithoutUnwindingLeavesTheTerminalRawAsItMust)
{
    // _exit() runs nothing: no destructor, no atexit handler, no signal handler. The terminal
    // really does stay raw, and no amount of care in TerminalSession can change that.
    //
    // This test exists to make the five above meaningful. If it passed as well, every assertion
    // in this file would be vacuous -- a pty that resets itself between spawns, or an echo check
    // that could never read false, would look exactly like restoration working.
    const HarnessResult result = RunHarness("leak");
    ASSERT_TRUE(result.spawned);
    EXPECT_TRUE(WIFEXITED(result.status));
    EXPECT_FALSE(result.echoRestored)
        << "if the terminal came back here, the other tests in this file prove nothing";
    EXPECT_FALSE(HasEpilogue(result.terminalOutput));
}

// --- in-process behaviour ------------------------------------------------------------------------

TEST(TerminalSessionTest, RefusesToWriteEscapeSequencesIntoSomethingThatIsNotATerminal)
{
    // The failure this prevents is a build log full of escape bytes, or worse, a redirected save
    // file with a cursor-hide sequence in it.
    int ends[2] = {-1, -1};
    ASSERT_EQ(pipe(ends), 0);

    EXPECT_THROW((void)TerminalSession(ends[1], ends[0], TerminalSessionOptions{}),
                 CNA::Platform::PlatformException);
    EXPECT_FALSE(TerminalSession::IsActive());

    close(ends[0]);
    close(ends[1]);
}

TEST(TerminalSessionTest, ASecondConcurrentSessionIsRefused)
{
    // Allowing one would overwrite the first session's saved attributes with the raw ones it just
    // installed, so the eventual restore would hand the user back a terminal with no echo -- the
    // exact failure this whole task exists to prevent, arrived at by being helpful.
    PseudoTerminal pty;
    if (!pty.IsOpen())
    {
        GTEST_SKIP() << "this environment cannot allocate a pseudo-terminal";
    }

    TerminalSessionOptions options;
    options.alternateScreen = false;
    options.hideCursor = false;

    const TerminalSession first(pty.Device(), pty.Device(), options);
    EXPECT_TRUE(TerminalSession::IsActive());
    EXPECT_THROW((void)TerminalSession(pty.Device(), pty.Device(), options),
                 CNA::Platform::PlatformException);
}

TEST(TerminalSessionTest, TheSessionEndsWhenItIsDestroyedSoAnotherCanStart)
{
    PseudoTerminal pty;
    if (!pty.IsOpen())
    {
        GTEST_SKIP() << "this environment cannot allocate a pseudo-terminal";
    }
    TerminalSessionOptions options;
    options.alternateScreen = false;
    options.hideCursor = false;

    { const TerminalSession first(pty.Device(), pty.Device(), options); }
    EXPECT_FALSE(TerminalSession::IsActive());
    EXPECT_NO_THROW({ const TerminalSession second(pty.Device(), pty.Device(), options); });
    EXPECT_FALSE(TerminalSession::IsActive());
}

TEST(TerminalSessionTest, RawModeIsEnteredAndThenPutBackExactly)
{
    PseudoTerminal pty;
    if (!pty.IsOpen())
    {
        GTEST_SKIP() << "this environment cannot allocate a pseudo-terminal";
    }

    termios before{};
    ASSERT_EQ(tcgetattr(pty.Device(), &before), 0);
    ASSERT_NE(before.c_lflag & static_cast<tcflag_t>(ECHO), 0u);

    {
        const TerminalSession session(pty.Device(), pty.Device(), TerminalSessionOptions{});

        termios during{};
        ASSERT_EQ(tcgetattr(pty.Device(), &during), 0);
        EXPECT_EQ(during.c_lflag & static_cast<tcflag_t>(ECHO), 0u) << "echo must be off";
        EXPECT_EQ(during.c_lflag & static_cast<tcflag_t>(ICANON), 0u) << "canonical mode must be off";
    }

    termios after{};
    ASSERT_EQ(tcgetattr(pty.Device(), &after), 0);
    EXPECT_EQ(before.c_lflag, after.c_lflag);
    EXPECT_EQ(before.c_iflag, after.c_iflag);
    EXPECT_EQ(before.c_oflag, after.c_oflag);
    EXPECT_EQ(before.c_cc[VMIN], after.c_cc[VMIN]);
    EXPECT_EQ(before.c_cc[VTIME], after.c_cc[VTIME]);
}

TEST(TerminalSessionTest, ASessionTakesOnlyWhatItWasAskedFor)
{
    // Each part is separately switchable because each is separately needed, and a session that
    // silently enabled mouse reporting would put bytes on a link the frame budget is rationing.
    PseudoTerminal pty;
    if (!pty.IsOpen())
    {
        GTEST_SKIP() << "this environment cannot allocate a pseudo-terminal";
    }

    TerminalSessionOptions rawOnly;
    rawOnly.alternateScreen = false;
    rawOnly.hideCursor = false;
    rawOnly.mouseReporting = false;

    {
        const TerminalSession session(pty.Device(), pty.Device(), rawOnly);
    }
    const std::string emitted = pty.DrainOutput();
    EXPECT_EQ(emitted.find("\x1b[?1049h"), std::string::npos) << "no alternate screen was asked for";
    EXPECT_EQ(emitted.find("\x1b[?25l"), std::string::npos) << "no cursor hide was asked for";
    EXPECT_EQ(emitted.find("\x1b[?1006h"), std::string::npos) << "no mouse reporting was asked for";
}

// --- the sequences themselves ---------------------------------------------------------------------

TEST(TerminalSessionTest, TheEpilogueUndoesEveryModeThePrologueSet)
{
    // Checked as a property rather than by eye: for every DEC private mode the prologue touches,
    // the epilogue must touch the same mode with the *opposite* terminator. A mode added to one
    // and forgotten in the other is precisely how a terminal is left with mouse reporting on
    // forever, and by eye that is one wrong digit in a wall of escape bytes.
    //
    // Note the direction is per-mode, not global: hiding the cursor is `?25l` in the prologue and
    // `?25h` in the epilogue, the reverse of the alternate screen. Asserting "every h has an l"
    // would be wrong for exactly that mode.
    TerminalSessionOptions everything;
    everything.mouseReporting = true;

    const std::string prologue = BuildSessionPrologue(everything);
    const std::string epilogue = BuildSessionEpilogue(everything);

    int modesChecked = 0;
    for (std::size_t at = prologue.find("\x1b[?"); at != std::string::npos;
         at = prologue.find("\x1b[?", at + 1))
    {
        std::size_t digit = at + 3;
        while (digit < prologue.size() && std::isdigit(static_cast<unsigned char>(prologue[digit])))
        {
            ++digit;
        }
        ASSERT_LT(digit, prologue.size());

        const std::string mode = prologue.substr(at + 3, digit - (at + 3));
        const char setTo = prologue[digit];
        ASSERT_TRUE(setTo == 'h' || setTo == 'l') << "unrecognised terminator for mode " << mode;

        const std::string undo = "\x1b[?" + mode + (setTo == 'h' ? 'l' : 'h');
        EXPECT_NE(epilogue.find(undo), std::string::npos)
            << "the epilogue never puts private mode " << mode << " back";
        ++modesChecked;
    }

    // Otherwise a prologue that stopped emitting anything would make this pass vacuously.
    EXPECT_EQ(modesChecked, 5) << "alternate screen, cursor, and three mouse modes";
}

TEST(TerminalSessionTest, TheEpilogueLeavesTheAlternateScreenLast)
{
    // Order, not content. Colour and mouse reporting have to be reset while the session still owns
    // the screen; anything reset after the alternate screen is left is applied to the terminal the
    // user is looking at again.
    TerminalSessionOptions everything;
    everything.mouseReporting = true;
    const std::string epilogue = BuildSessionEpilogue(everything);

    const std::size_t leaveScreen = epilogue.find("\x1b[?1049l");
    ASSERT_NE(leaveScreen, std::string::npos);
    EXPECT_LT(epilogue.find("\x1b[?1006l"), leaveScreen);
    EXPECT_LT(epilogue.find("\x1b[0m"), leaveScreen);
    EXPECT_LT(epilogue.find("\x1b[?25h"), leaveScreen);
}

TEST(TerminalSessionTest, ColourIsAlwaysResetEvenWhenNothingElseWasTakenOver)
{
    // A shell that inherits a stray background colour is the most visible way to get this wrong,
    // and it happens with no alternate screen involved at all.
    TerminalSessionOptions minimal;
    minimal.alternateScreen = false;
    minimal.hideCursor = false;
    minimal.mouseReporting = false;
    EXPECT_NE(BuildSessionEpilogue(minimal).find("\x1b[0m"), std::string::npos);
}

} // namespace
