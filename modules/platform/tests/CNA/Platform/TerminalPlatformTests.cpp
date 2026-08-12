// SPDX-License-Identifier: MS-PL
//
// PLAT-130: the TerminalPlatform skeleton.
//
// Reached through PlatformFactory rather than by constructing the class, because being reachable
// by name is half of what the task delivers -- and because that is exactly how the conformance
// suite (PLAT-141) will find it. The behaviour every implementation shares is already asserted
// there, parameterised over everything compiled in; what belongs here is only what is true of
// this implementation and no other.

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>

#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace CNA::Platform;

std::unique_ptr<IPlatform> MakeTerminal() { return PlatformFactory::Create("Terminal"); }

TEST(TerminalPlatformTest, IsAvailableAndNamesItself)
{
    const std::vector<std::string> available = PlatformFactory::GetAvailable();
    ASSERT_NE(std::find(available.begin(), available.end(), "Terminal"), available.end())
        << "Terminal is compiled on every POSIX target, whatever CNA_PLATFORM says";

    EXPECT_EQ(MakeTerminal()->GetName(), "Terminal");
}

TEST(TerminalPlatformTest, ConstructionTouchesNoTerminalState)
{
    // The property that lets the conformance suite build one in a process whose terminal belongs
    // to somebody else. Constructing must not enter raw mode, so a terminal attached to this test
    // process still has canonical mode and echo afterwards. Asserted on the process's real stdin
    // deliberately: that is the terminal a careless constructor would damage.
    termios before{};
    const bool haveTerminal = tcgetattr(STDIN_FILENO, &before) == 0;

    const std::unique_ptr<IPlatform> platform = MakeTerminal();
    ASSERT_NE(platform, nullptr);

    if (!haveTerminal)
    {
        GTEST_SKIP() << "no terminal on stdin here; nothing could have been damaged";
    }
    termios after{};
    ASSERT_EQ(tcgetattr(STDIN_FILENO, &after), 0);
    EXPECT_EQ(before.c_lflag, after.c_lflag) << "constructing a platform must not enter raw mode";
    EXPECT_EQ(before.c_iflag, after.c_iflag);
    EXPECT_EQ(before.c_oflag, after.c_oflag);
}

TEST(TerminalPlatformTest, OnlyTheCapabilitiesWhoseTasksHaveLandedAreAdvertised)
{
    // Not an aspiration: the synthetic keyboard (PLAT-138) and mouse (PLAT-139) each land in
    // their own task, and a capability advertised before its implementation exists is the silent
    // no-op the contract forbids. Exact keyboard state is conditional on a successful Kitty
    // probe, so its service and flag must agree rather than being hard-coded here.
    //
    // Surface presentation is the exception, and conditionally: it is implemented (PLAT-132), so
    // it is true exactly when standard output really is a terminal and false otherwise. Asserting
    // it unconditionally either way would be wrong in one of the two environments this suite
    // runs in.
    const std::unique_ptr<IPlatform> platform = MakeTerminal();
    const PlatformCapabilities capabilities = platform->GetCapabilities();

    EXPECT_EQ(capabilities.surfacePresentation, isatty(STDOUT_FILENO) != 0);
    EXPECT_EQ(capabilities.exactKeyboardState, platform->GetKeyboard() != nullptr);

    for (const PlatformCapability capability : AllCapabilities())
    {
        if (capability == PlatformCapability::SurfacePresentation ||
            capability == PlatformCapability::ExactKeyboardState)
        {
            continue;
        }
        EXPECT_FALSE(Supports(capabilities, capability)) << ToString(capability);
    }
}

TEST(TerminalPlatformTest, TheWindowIsTheResolutionTheGameAskedForNotTheCharacterGrid)
{
    // A terminal is 80x24 cells, but a game asks for the resolution it wants to draw at and must
    // get it -- the quantisation to glyphs is the presenter's job (PLAT-132). Reporting the cell
    // grid as the window size would break every layout computation a game has.
    const std::unique_ptr<IPlatform> platform = MakeTerminal();
    platform->AcquireSubsystem(PlatformSubsystem::Video);

    WindowDescription description;
    description.title = "terminal";
    description.width = 640;
    description.height = 360;

    const std::unique_ptr<IPlatformWindow> window = platform->CreateWindow(description);
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(window->GetClientBounds().width, 640);
    EXPECT_EQ(window->GetClientBounds().height, 360);
    EXPECT_EQ(window->GetPixelSize().width, 640);
    EXPECT_EQ(window->GetPixelSize().height, 360);
}

TEST(TerminalPlatformTest, ASecondWindowIsRefusedNamingTheCapability)
{
    // There is exactly one terminal. Handing back a second window would alias it onto the first
    // and leave two callers each believing they owned the screen.
    const std::unique_ptr<IPlatform> platform = MakeTerminal();
    WindowDescription description;
    description.width = 320;
    description.height = 240;

    const std::unique_ptr<IPlatformWindow> first = platform->CreateWindow(description);
    ASSERT_NE(first, nullptr);

    try
    {
        (void)platform->CreateWindow(description);
        FAIL() << "a second window must be refused, not silently aliased onto the first";
    }
    catch (const PlatformNotSupportedException& refusal)
    {
        EXPECT_EQ(refusal.GetCapability(), PlatformCapability::MultipleWindows);
    }
}

TEST(TerminalPlatformTest, DestroyingTheWindowFreesTheSingleSlot)
{
    // Otherwise a game that recreated its window -- which XNA's GraphicsDeviceManager does on a
    // device reset -- would be refused forever after the first one.
    const std::unique_ptr<IPlatform> platform = MakeTerminal();
    WindowDescription description;
    description.width = 320;
    description.height = 240;

    { const std::unique_ptr<IPlatformWindow> first = platform->CreateWindow(description); }
    EXPECT_NO_THROW({ const std::unique_ptr<IPlatformWindow> second = platform->CreateWindow(description); });
}

TEST(TerminalPlatformTest, AWindowOutlivingItsPlatformIsNotUndefinedBehaviour)
{
    // The single-window slot has to be released by the window on destruction, and nothing in the
    // contract forbids destroying the platform first. A back-pointer would dangle in exactly this
    // order and in no other -- which is the kind of defect that passes every test until the one
    // process that happens to tear down that way.
    WindowDescription description;
    description.width = 320;
    description.height = 240;

    std::unique_ptr<IPlatformWindow> window;
    {
        const std::unique_ptr<IPlatform> platform = MakeTerminal();
        window = platform->CreateWindow(description);
    }
    EXPECT_NE(window, nullptr);
    EXPECT_NO_THROW(window.reset());
}

TEST(TerminalPlatformTest, TheWindowOffersNoNativeHandleForARendererToDereference)
{
    // No GPU renderer can run here. Reporting a handle-less window is what makes that a refusal
    // at renderer selection rather than a null dereference at first draw.
    const std::unique_ptr<IPlatform> platform = MakeTerminal();
    WindowDescription description;
    description.width = 320;
    description.height = 240;

    const std::unique_ptr<IPlatformWindow> window = platform->CreateWindow(description);
    EXPECT_FALSE(HasNativeWindow(window->GetNativeHandle()));
}

TEST(TerminalPlatformTest, SettingATitleDoesNotWriteToStandardOutput)
{
    // A terminal title is an OSC sequence, and emitting one from SetTitle would put escape bytes
    // into a redirected log -- or into the middle of a frame. It is stored until the session owns
    // the terminal (PLAT-131), so it must round-trip without any output at all.
    const std::unique_ptr<IPlatform> platform = MakeTerminal();
    WindowDescription description;
    description.title = "before";
    description.width = 320;
    description.height = 240;
    const std::unique_ptr<IPlatformWindow> window = platform->CreateWindow(description);

    ::testing::internal::CaptureStdout();
    window->SetTitle("after");
    const std::string emitted = ::testing::internal::GetCapturedStdout();

    EXPECT_EQ(window->GetTitle(), "after");
    EXPECT_TRUE(emitted.empty()) << "SetTitle wrote to stdout: " << emitted;
}

TEST(TerminalPlatformTest, PollingEmitsNothingAndProducesNoEventsYet)
{
    // With output redirected there is no terminal to query and no input service to start, so
    // polling remains silent in both senses: no events invented and no escape bytes in the log.
    if (isatty(STDOUT_FILENO) != 0)
    {
        GTEST_SKIP() << "this assertion covers the redirected-output path";
    }
    const std::unique_ptr<IPlatform> platform = MakeTerminal();

    ::testing::internal::CaptureStdout();
    std::vector<PlatformEvent> batch;
    platform->PollEvents(batch);
    const std::string emitted = ::testing::internal::GetCapturedStdout();

    EXPECT_TRUE(batch.empty());
    EXPECT_TRUE(emitted.empty()) << "PollEvents wrote to stdout: " << emitted;
}

TEST(TerminalPlatformTest, TheFilesystemIsRealAndItsPreferencesPathIsItsOwn)
{
    // Two platforms live in one process must not share a preferences directory, or a test that
    // writes through one would be read back through the other.
    const std::unique_ptr<IPlatform> terminal = MakeTerminal();
    const std::unique_ptr<IPlatform> headless = PlatformFactory::Create("Headless");

    const std::string terminalPath =
        terminal->GetFileSystem()->GetPreferencesPath("CnaTests", "PlatformSeparation");
    const std::string headlessPath =
        headless->GetFileSystem()->GetPreferencesPath("CnaTests", "PlatformSeparation");

    EXPECT_FALSE(terminalPath.empty());
    EXPECT_NE(terminalPath, headlessPath);
}

} // namespace
