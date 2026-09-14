// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0080/X11-0081: the clipboard against a genuinely external X client.
//
// A clipboard that works between two CNA windows is not a clipboard. X11 has no clipboard
// storage at all -- `CLIPBOARD` is an ownership token, and the owning client serves the data on
// request -- so "it round-trips inside my own process" proves only that a member variable was
// read back. The assertion that matters is that a completely separate program, with its own X
// connection and no CNA code in it, can paste what CNA copied and copy something CNA can paste.
//
// `xclip` is that program: a 500-line X client from 2001 that implements the ICCCM selection
// protocol and nothing else. When it is not installed the tests skip with that recorded, rather
// than falling back to an in-process check that would pass while proving nothing.

#include <gtest/gtest.h>

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <string>
#include <sys/types.h>
#include <thread>

namespace {

using namespace CNA::Platform;

bool HasDisplay()
{
    const char* display = std::getenv("DISPLAY");
    return display != nullptr && display[0] != '\0';
}

bool HasXclip()
{
    // `command -v` rather than a hardcoded path: the point of the X11 backend is not being
    // Ubuntu-specific, and that applies to its test scaffolding too.
    return std::system("command -v xclip >/dev/null 2>&1") == 0;
}

/// Runs a command and returns its standard output.
std::string Capture(const std::string& command)
{
    std::string output;
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(command.c_str(), "r"), pclose);
    if (pipe == nullptr)
    {
        return output;
    }
    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr)
    {
        output += buffer.data();
    }
    return output;
}

/// Starts xclip serving @p text as the CLIPBOARD owner, in the background, and returns its PID so
/// the test can stop exactly that process -- never every xclip on the machine, which is what a
/// pattern kill does to other tests and to a developer's own session.
pid_t StartExternalOwner(const std::string& text)
{
    const std::string command = "printf '%s' '" + text +
                                "' | xclip -quiet -selection clipboard -i >/dev/null 2>&1 & echo $!";
    return static_cast<pid_t>(std::atol(Capture(command).c_str()));
}

void StopExternalOwner(const pid_t pid)
{
    if (pid > 0)
    {
        ::kill(pid, SIGTERM);
    }
}

class X11ClipboardInterop : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!HasDisplay())
        {
            GTEST_SKIP() << "no DISPLAY: selection transfer needs a real X server";
        }
        if (!HasXclip())
        {
            GTEST_SKIP() << "xclip is not installed, so there is no external X client to exchange "
                            "selections with; an in-process check would prove nothing";
        }
        platform_ = PlatformFactory::Create("X11");
        try
        {
            platform_->AcquireSubsystem(PlatformSubsystem::Video);
        }
        catch (const PlatformException& error)
        {
            GTEST_SKIP() << "cannot reach the X server: " << error.what();
        }
        acquired_ = true;
        clipboard_ = platform_->GetClipboard();
        ASSERT_NE(clipboard_, nullptr);
    }

    void TearDown() override
    {
        if (acquired_)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
        platform_.reset();
    }

    /// Runs an external reader and pumps CNA's events until it has finished.
    ///
    /// CNA's clipboard is a *selection owner*: the data lives in this process, and an external
    /// paste arrives as a `SelectionRequest` that only the event pump can answer, so the pump
    /// has to run for as long as the reader does. Pumping for a fixed time and then waiting for the reader hung whenever the transfer took
    /// longer -- a loaded machine, a large INCR paste -- because nothing answered xclip's next
    /// request any more (plans/plan_native_platform_validation.md NPV-0120). The reader runs under
    /// `timeout`, so a transfer that never completes fails the test instead of hanging it.
    std::string ReadWhilePumping(const std::string& command, const int budgetSeconds = 20)
    {
        auto reader = std::async(std::launch::async, [command, budgetSeconds] {
            return Capture("timeout " + std::to_string(budgetSeconds) + " " + command);
        });
        std::vector<PlatformEvent> batch;
        while (reader.wait_for(std::chrono::milliseconds(2)) != std::future_status::ready)
        {
            platform_->PollEvents(batch);
        }
        return reader.get();
    }

    std::unique_ptr<IPlatform> platform_;
    IPlatformClipboard* clipboard_ = nullptr;
    bool acquired_ = false;
};

TEST_F(X11ClipboardInterop, AnExternalClientPastesWhatCnaCopied)
{
    const std::string text = "copied by CNA \xE2\x86\x92 pasted by xclip";
    clipboard_->SetText(text);

    // xclip runs concurrently and sends a SelectionRequest that only CNA's pump can answer, so
    // the pump has to be running while it asks. A background read plus a pumping foreground is
    // the shape the real interaction has.
    const std::string pasted = ReadWhilePumping("xclip -selection clipboard -o 2>/dev/null");

    EXPECT_EQ(pasted, text);
}

TEST_F(X11ClipboardInterop, CnaPastesWhatAnExternalClientCopied)
{
    const std::string text = "copied by xclip \xE2\x86\x90 pasted by CNA";
    // xclip -i must keep running to serve the selection it owns, so it is started detached and
    // stopped afterwards. Its own -quiet mode does exactly this.
    const pid_t owner = StartExternalOwner(text);
    ASSERT_GT(owner, 0);

    // Give the external owner a moment to take the selection, then ask for it. The wait is on
    // ownership, not on a fixed duration.
    bool sawIt = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (clipboard_->HasText() && clipboard_->GetText() == text)
        {
            sawIt = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    EXPECT_TRUE(sawIt) << "CNA read: '" << clipboard_->GetText() << "'";

    StopExternalOwner(owner);
}

TEST_F(X11ClipboardInterop, ALargeSelectionTransfersThroughIncrToAnExternalClient)
{
    // Past the server's maximum request size, which is what forces the INCR protocol. This is the
    // case an implementation that only ever tested short strings gets wrong: a single
    // XChangeProperty silently fails, and the external paste comes back empty.
    std::string large;
    large.reserve(512 * 1024);
    while (large.size() < 512u * 1024u)
    {
        large += "CNA-INCR-payload;";
    }
    clipboard_->SetText(large);

    const std::string pasted = ReadWhilePumping("xclip -selection clipboard -o 2>/dev/null");

    EXPECT_EQ(pasted.size(), large.size());
    EXPECT_EQ(pasted, large);
}

TEST_F(X11ClipboardInterop, CnaAdvertisesTheTargetsAnExternalClientLooksFor)
{
    clipboard_->SetText("targets");

    const std::string targets =
        ReadWhilePumping("xclip -selection clipboard -o -t TARGETS 2>/dev/null");

    // TARGETS must list itself -- a requestor asks "what can you give me" and then picks, and
    // omitting TARGETS from its own answer makes well-behaved clients conclude we offer nothing.
    EXPECT_NE(targets.find("TARGETS"), std::string::npos) << targets;
    EXPECT_NE(targets.find("UTF8_STRING"), std::string::npos) << targets;
    EXPECT_NE(targets.find("STRING"), std::string::npos) << targets;
}

TEST_F(X11ClipboardInterop, LosingOwnershipToAnotherClientClearsCnasCopy)
{
    clipboard_->SetText("CNA owns this");
    ASSERT_TRUE(clipboard_->HasText());

    const pid_t owner = StartExternalOwner("someone else owns this");
    ASSERT_GT(owner, 0);

    // SelectionClear arrives through the pump. Keeping the old text would make HasText()/GetText()
    // answer from a stale copy of what the user copied several applications ago.
    bool changed = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    std::vector<PlatformEvent> batch;
    while (std::chrono::steady_clock::now() < deadline)
    {
        platform_->PollEvents(batch);
        if (clipboard_->GetText() == "someone else owns this")
        {
            changed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    EXPECT_TRUE(changed) << "CNA still reports: '" << clipboard_->GetText() << "'";

    StopExternalOwner(owner);
}

} // namespace
