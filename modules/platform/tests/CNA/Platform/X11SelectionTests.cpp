// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0157: the primary selection -- what an X11 desktop pastes with the middle
// mouse button -- in both directions, against another X client: this binary started again as a
// separate process (X11SelectionPeer below) that owns or reads a selection exactly as any X
// application does, INCR included.
//
// These change the server's selections, so they run only on the launcher's own server
// (CNA_X11_PRIVATE_TEST_SERVER): on a desktop they would take the user's selection away.

#include <gtest/gtest.h>

#include "../../../src/X11/X11Clipboard.hpp"
#include "../../../src/X11/X11Headers.hpp"

#include "CNA/Platform/PlatformFactory.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace {

using namespace CNA::Platform;
using CNA::Platform::X11::kCurrentTime;
using CNA::Platform::X11::kNone;
using CNA::Platform::X11::kXFalse;

std::vector<std::string> Split(const std::string& text, const char separator)
{
    std::vector<std::string> parts;
    std::string part;
    std::istringstream stream(text);
    while (std::getline(stream, part, separator))
    {
        if (!part.empty()) { parts.push_back(part); }
    }
    return parts;
}

std::string ReadFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string TemporaryFile(const char* prefix, const std::string& contents = {})
{
    std::string path = std::string("/tmp/") + prefix + "-XXXXXX";
    const int fd = mkstemp(path.data());
    if (fd < 0) { return {}; }
    std::size_t written = 0;
    while (written < contents.size())
    {
        const ssize_t now = ::write(fd, contents.data() + written, contents.size() - written);
        if (now <= 0) { break; }
        written += static_cast<std::size_t>(now);
    }
    ::close(fd);
    return path;
}

// --- the other client ---------------------------------------------------------------------------------

/// What X11SelectionLive asks the peer to do.
struct PeerScript
{
    /// "own" or "read".
    std::string mode;
    /// "PRIMARY" or "CLIPBOARD".
    std::string selection;
    /// own: the targets offered, each with @ref data. read: the one target asked for.
    std::vector<std::string> types;
    /// own: the bytes served.
    std::string data;
    /// own: the largest property it writes in one piece; anything larger goes through INCR.
    std::size_t chunk = 65536;
    /// own: when not empty, each type's own bytes, in the order of @ref types.
    std::vector<std::string> perType;
};

class X11SelectionLive : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const char* privateServer = std::getenv("CNA_X11_PRIVATE_TEST_SERVER");
        if (privateServer == nullptr || std::string(privateServer) != "1")
        {
            GTEST_SKIP() << "takes the server's selections; needs tools/platform/x11_test_server.sh";
        }
        platform_ = PlatformFactory::Create("X11");
        platform_->AcquireSubsystem(PlatformSubsystem::Video);
        acquired_ = true;
        primary_ = platform_->GetPrimarySelection();
        clipboard_ = platform_->GetClipboard();
        ASSERT_NE(primary_, nullptr);
        ASSERT_NE(clipboard_, nullptr);
    }

    void TearDown() override
    {
        for (const pid_t pid : running_)
        {
            ::kill(pid, SIGKILL);
            int status = 0;
            ::waitpid(pid, &status, 0);
        }
        for (const std::string& path : files_) { std::remove(path.c_str()); }
        if (acquired_)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
        platform_.reset();
    }

    void Pump()
    {
        std::vector<PlatformEvent> batch;
        platform_->PollEvents(batch);
    }

    bool PumpUntil(const std::function<bool()>& done,
                   const std::chrono::milliseconds budget = std::chrono::milliseconds(5000))
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        while (std::chrono::steady_clock::now() < deadline)
        {
            Pump();
            if (done()) { return true; }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }

    struct Peer
    {
        pid_t pid = -1;
        std::string report;
        std::string output;
    };

    /// Starts the peer; an owner is waited for until it owns the selection.
    Peer Start(const PeerScript& script)
    {
        Peer peer;
        peer.report = TemporaryFile("cna-selection-report");
        peer.output = TemporaryFile("cna-selection-output");
        const std::string dataPath = TemporaryFile("cna-selection-data", script.data);
        files_.push_back(peer.report);
        files_.push_back(peer.output);
        files_.push_back(dataPath);

        std::vector<std::string> environment;
        for (char** entry = environ; *entry != nullptr; ++entry) { environment.emplace_back(*entry); }
        std::string types;
        for (const std::string& type : script.types) { types += (types.empty() ? "" : ",") + type; }
        environment.push_back("CNA_X11_PEER_MODE=" + script.mode);
        environment.push_back("CNA_X11_PEER_SELECTION=" + script.selection);
        environment.push_back("CNA_X11_PEER_TYPES=" + types);
        environment.push_back("CNA_X11_PEER_DATA=" + dataPath);
        environment.push_back("CNA_X11_PEER_CHUNK=" + std::to_string(script.chunk));
        std::string dataList;
        for (const std::string& bytes : script.perType)
        {
            const std::string path = TemporaryFile("cna-selection-data", bytes);
            files_.push_back(path);
            dataList += (dataList.empty() ? "" : ",") + path;
        }
        environment.push_back("CNA_X11_PEER_DATA_LIST=" + dataList);
        environment.push_back("CNA_X11_PEER_REPORT=" + peer.report);
        environment.push_back("CNA_X11_PEER_OUTPUT=" + peer.output);
        std::vector<char*> environmentPointers;
        for (std::string& entry : environment) { environmentPointers.push_back(entry.data()); }
        environmentPointers.push_back(nullptr);
        std::string executable = "/proc/self/exe";
        std::string filter = "--gtest_filter=X11SelectionPeer.DISABLED_Run";
        std::string disabled = "--gtest_also_run_disabled_tests";
        std::vector<char*> arguments = {executable.data(), filter.data(), disabled.data(), nullptr};

        peer.pid = ::fork();
        if (peer.pid == 0)
        {
            const int devNull = ::open("/dev/null", O_WRONLY);
            if (devNull >= 0)
            {
                ::dup2(devNull, 1);
                ::dup2(devNull, 2);
            }
            ::execve(executable.c_str(), arguments.data(), environmentPointers.data());
            ::_exit(127);
        }
        if (peer.pid > 0)
        {
            running_.push_back(peer.pid);
        }
        if (script.mode == "own")
        {
            const bool ready = PumpUntil([&peer]() {
                return ReadFile(peer.report).find("ready\n") != std::string::npos;
            });
            EXPECT_TRUE(ready) << "the peer never took the selection: " << ReadFile(peer.report);
        }
        return peer;
    }

    /// Runs the peer to its end, pumping CNA meanwhile -- a reader needs CNA to answer it.
    int Finish(Peer& peer)
    {
        int status = 0;
        const bool finished = PumpUntil([&peer, &status]() {
            return ::waitpid(peer.pid, &status, WNOHANG) == peer.pid;
        }, std::chrono::seconds(15));
        if (!finished)
        {
            ::kill(peer.pid, SIGKILL);
            ::waitpid(peer.pid, &status, 0);
        }
        std::erase(running_, peer.pid);
        EXPECT_TRUE(finished) << "the peer never finished: " << ReadFile(peer.report);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    /// Reads a selection the way another application pastes it.
    std::string ReadAsAnotherClient(const std::string& selection, const std::string& target,
                                    int* exitCode = nullptr, std::string* report = nullptr)
    {
        PeerScript script;
        script.mode = "read";
        script.selection = selection;
        script.types = {target};
        Peer peer = Start(script);
        const int code = Finish(peer);
        if (exitCode != nullptr) { *exitCode = code; }
        if (report != nullptr) { *report = ReadFile(peer.report); }
        return ReadFile(peer.output);
    }

    /// Stops an owning peer, which gives its selection up by destroying its window.
    void Stop(Peer& peer)
    {
        ::kill(peer.pid, SIGTERM);
        (void) Finish(peer);
    }

    std::unique_ptr<IPlatform> platform_;
    bool acquired_ = false;
    IPlatformClipboard* primary_ = nullptr;
    IPlatformClipboard* clipboard_ = nullptr;
    std::vector<pid_t> running_;
    std::vector<std::string> files_;
};

TEST_F(X11SelectionLive, ThePrimarySelectionIsASecondServiceBesideTheClipboard)
{
    EXPECT_TRUE(platform_->GetCapabilities().primarySelection);
    EXPECT_NE(static_cast<void*>(primary_), static_cast<void*>(clipboard_));
}

TEST_F(X11SelectionLive, CnaPastesWhatAnotherClientSelected)
{
    const std::string text = "selected elsewhere \xE2\x9C\x93";
    Peer owner = Start({"own", "PRIMARY", {"UTF8_STRING", "STRING"}, text});

    EXPECT_TRUE(primary_->HasText());
    EXPECT_EQ(primary_->GetText(), text);
    EXPECT_FALSE(clipboard_->HasText()) << "selecting is not copying";
    EXPECT_EQ(clipboard_->GetText(), "");
    Stop(owner);
}

TEST_F(X11SelectionLive, AnotherClientPastesWhatCnaSelected)
{
    const std::string text = "selected in CNA \xE2\x86\x92 pasted with the middle button";
    primary_->SetText(text);

    std::string report;
    EXPECT_EQ(ReadAsAnotherClient("PRIMARY", "UTF8_STRING", nullptr, &report), text);
    EXPECT_NE(report.find("type=UTF8_STRING"), std::string::npos) << report;

    // The clipboard stayed unowned: the server answers a conversion of it with no property.
    int code = 0;
    EXPECT_EQ(ReadAsAnotherClient("CLIPBOARD", "UTF8_STRING", &code), "");
    EXPECT_EQ(code, 3) << "the clipboard should have had no owner to answer";
}

TEST_F(X11SelectionLive, ItAdvertisesTheTargetsAReaderLooksFor)
{
    primary_->SetText("anything");
    const std::vector<std::string> targets =
        Split(ReadAsAnotherClient("PRIMARY", "TARGETS"), ',');
    for (const char* expected : {"TARGETS", "UTF8_STRING", "STRING", "TEXT"})
    {
        EXPECT_NE(std::find(targets.begin(), targets.end(), expected), targets.end()) << expected;
    }
}

TEST_F(X11SelectionLive, TheTwoSelectionsAreIndependent)
{
    clipboard_->SetText("copied");
    primary_->SetText("selected");
    EXPECT_EQ(ReadAsAnotherClient("CLIPBOARD", "UTF8_STRING"), "copied");
    EXPECT_EQ(ReadAsAnotherClient("PRIMARY", "UTF8_STRING"), "selected");

    // Another application selects something: CNA's selection is gone, its clipboard is not.
    Peer owner = Start({"own", "PRIMARY", {"UTF8_STRING"}, "selected elsewhere"});
    EXPECT_TRUE(PumpUntil([this]() { return primary_->GetText() == "selected elsewhere"; }));
    EXPECT_EQ(clipboard_->GetText(), "copied");
    EXPECT_EQ(ReadAsAnotherClient("CLIPBOARD", "UTF8_STRING"), "copied");
    Stop(owner);
}

TEST_F(X11SelectionLive, LosingThePrimarySelectionLeavesNothingToPaste)
{
    primary_->SetText("soon replaced");
    Peer owner = Start({"own", "PRIMARY", {"UTF8_STRING"}, "replacement"});
    // The peer's SelectionClear handling on CNA's side, then the peer leaving with it.
    Pump();
    Stop(owner);
    EXPECT_TRUE(PumpUntil([this]() { return !primary_->HasText(); }))
        << "CNA still answers from its own stale copy";
    EXPECT_EQ(primary_->GetText(), "");
}

TEST_F(X11SelectionLive, LatinOneFromAnOlderClientArrivesAsUtf8)
{
    // An application offering only STRING, which the ICCCM defines as Latin-1.
    Peer owner = Start({"own", "PRIMARY", {"STRING"}, "caf\xE9 cr\xE8me"});
    EXPECT_EQ(primary_->GetText(), "caf\xC3\xA9 cr\xC3\xA8me");
    Stop(owner);
}

TEST_F(X11SelectionLive, ALargeSelectionCrossesThroughIncrBothWays)
{
    std::string large;
    large.reserve(3u << 20);
    for (int line = 0; large.size() < (3u << 20); ++line)
    {
        large += "line " + std::to_string(line) + " of a selection too large for one property\n";
    }

    primary_->SetText(large);
    std::string report;
    const std::string pasted = ReadAsAnotherClient("PRIMARY", "UTF8_STRING", nullptr, &report);
    EXPECT_EQ(pasted.size(), large.size());
    EXPECT_TRUE(pasted == large) << "CNA's INCR send";
    EXPECT_NE(report.find("incr"), std::string::npos) << "the transfer was not incremental: " << report;

    const std::string other = large.substr(0, 1u << 20);
    Peer owner = Start({"own", "PRIMARY", {"UTF8_STRING"}, other, 65536});
    const std::string read = primary_->GetText();
    EXPECT_EQ(read.size(), other.size());
    EXPECT_TRUE(read == other) << "CNA's INCR receive";
    Stop(owner);
    EXPECT_NE(ReadFile(owner.report).find("incr"), std::string::npos) << ReadFile(owner.report);
}

// --- formats other than text (X11-0158) -----------------------------------------------------------

std::string EveryByteValue(const std::size_t size)
{
    std::string bytes(size, '\0');
    for (std::size_t index = 0; index < size; ++index)
    {
        bytes[index] = static_cast<char>((index * 131u + (index >> 8)) & 0xFFu);
    }
    return bytes;
}

std::vector<std::uint8_t> Bytes(const std::string& text)
{
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

TEST_F(X11SelectionLive, CnaOffersSeveralFormatsAtOnce)
{
    ASSERT_TRUE(platform_->GetCapabilities().clipboardData);
    const std::string png = EveryByteValue(4096);
    const std::string html = "<p>caf\xC3\xA9 <b>\xE2\x9C\x93</b></p>";
    const std::string plain = "caf\xC3\xA9 \xE2\x9C\x93";
    clipboard_->SetData({{"image/png", Bytes(png)},
                         {"text/html", Bytes(html)},
                         {"text/plain;charset=utf-8", Bytes(plain)}});

    const std::vector<std::string> targets = Split(ReadAsAnotherClient("CLIPBOARD", "TARGETS"), ',');
    for (const char* expected : {"TARGETS", "TIMESTAMP", "image/png", "text/html",
                                 "text/plain;charset=utf-8", "UTF8_STRING", "STRING", "TEXT",
                                 "text/plain"})
    {
        EXPECT_NE(std::find(targets.begin(), targets.end(), expected), targets.end()) << expected;
    }
    const auto position = [&targets](const char* name) {
        return std::find(targets.begin(), targets.end(), name) - targets.begin();
    };
    EXPECT_LT(position("image/png"), position("text/html")) << "the application's order is kept";

    std::string report;
    EXPECT_TRUE(ReadAsAnotherClient("CLIPBOARD", "image/png", nullptr, &report) == png);
    EXPECT_NE(report.find("type=image/png"), std::string::npos) << report;
    EXPECT_EQ(ReadAsAnotherClient("CLIPBOARD", "text/html"), html);
    EXPECT_EQ(ReadAsAnotherClient("CLIPBOARD", "UTF8_STRING"), plain);
    EXPECT_EQ(ReadAsAnotherClient("CLIPBOARD", "text/plain;charset=utf-8"), plain);

    // CNA's own view of what it offers, answered without asking itself.
    const std::vector<std::string> formats = clipboard_->GetMimeTypes();
    EXPECT_EQ(std::find(formats.begin(), formats.end(), "TARGETS"), formats.end());
    ASSERT_GE(formats.size(), 3u);
    EXPECT_EQ(formats[0], "image/png");
    EXPECT_TRUE(clipboard_->GetData("image/png") == Bytes(png));
    EXPECT_TRUE(clipboard_->HasData("text/html"));
    EXPECT_EQ(clipboard_->GetText(), plain);
}

TEST_F(X11SelectionLive, CnaReadsAnotherApplicationsFormatsExactly)
{
    const std::string png = EveryByteValue(100000);
    const std::string html = "<i>from elsewhere</i>";
    PeerScript script{"own", "CLIPBOARD", {"image/png", "text/html"}, "", 65536};
    script.perType = {png, html};
    Peer owner = Start(script);

    EXPECT_EQ(clipboard_->GetMimeTypes(), (std::vector<std::string>{"image/png", "text/html"}));
    EXPECT_TRUE(clipboard_->HasData("image/png"));
    EXPECT_FALSE(clipboard_->HasData("image/bmp"));
    EXPECT_TRUE(clipboard_->GetData("image/png") == Bytes(png)) << "every byte value, NULs included";
    EXPECT_TRUE(clipboard_->GetData("text/html") == Bytes(html));
    EXPECT_TRUE(clipboard_->GetData("image/bmp").empty());
    EXPECT_FALSE(clipboard_->HasText()) << "an image is not text";
    EXPECT_EQ(clipboard_->GetText(), "");
    Stop(owner);
}

TEST_F(X11SelectionLive, ALargeImageCrossesThroughIncrBothWays)
{
    const std::string out = EveryByteValue(5u << 19);
    clipboard_->SetData({{"image/png", Bytes(out)}});
    std::string report;
    const std::string pasted = ReadAsAnotherClient("CLIPBOARD", "image/png", nullptr, &report);
    EXPECT_EQ(pasted.size(), out.size());
    EXPECT_TRUE(pasted == out);
    EXPECT_NE(report.find("incr"), std::string::npos) << report;

    const std::string in = EveryByteValue(3u << 19).substr(7);
    PeerScript script{"own", "CLIPBOARD", {"image/png"}, in, 65536};
    Peer owner = Start(script);
    const std::vector<std::uint8_t> read = clipboard_->GetData("image/png");
    EXPECT_EQ(read.size(), in.size());
    EXPECT_TRUE(read == Bytes(in));
    Stop(owner);
    EXPECT_NE(ReadFile(owner.report).find("incr"), std::string::npos) << ReadFile(owner.report);
}

TEST_F(X11SelectionLive, StringIsLatinOneWhenCnaServesText)
{
    // D-17: STRING is Latin-1 by the ICCCM; UTF-8 bytes under it are two wrong characters each.
    clipboard_->SetText("caf\xC3\xA9 \xE2\x9C\x93");
    std::string report;
    EXPECT_EQ(ReadAsAnotherClient("CLIPBOARD", "STRING", nullptr, &report), "caf\xE9 ?");
    EXPECT_NE(report.find("type=STRING"), std::string::npos) << report;
    EXPECT_EQ(ReadAsAnotherClient("CLIPBOARD", "UTF8_STRING"), "caf\xC3\xA9 \xE2\x9C\x93");
}

TEST_F(X11SelectionLive, TextFromAnApplicationThatNamesItOnlyByMimeType)
{
    const std::string text = "\xC3\xBCn\xC3\xAF" "code \xE2\x9C\x93";
    Peer owner = Start({"own", "CLIPBOARD", {"text/plain;charset=utf-8"}, text});
    EXPECT_TRUE(clipboard_->HasText());
    EXPECT_EQ(clipboard_->GetText(), text);
    Stop(owner);
}

TEST_F(X11SelectionLive, CopyingAnImageReplacesTheText)
{
    clipboard_->SetText("text first");
    clipboard_->SetData({{"image/png", Bytes(EveryByteValue(64))}});
    EXPECT_FALSE(clipboard_->HasText());
    EXPECT_EQ(clipboard_->GetText(), "");
    int code = 0;
    EXPECT_EQ(ReadAsAnotherClient("CLIPBOARD", "UTF8_STRING", &code), "");
    EXPECT_EQ(code, 3) << "the text must no longer be served";
}

TEST(X11TextEncoding, StringIsLatinOneBothWays)
{
    using CNA::Platform::X11::Latin1ToUtf8;
    using CNA::Platform::X11::Utf8ToLatin1;
    EXPECT_EQ(Utf8ToLatin1("plain ASCII"), "plain ASCII");
    EXPECT_EQ(Utf8ToLatin1("caf\xC3\xA9 \xC3\xBF"), "caf\xE9 \xFF");
    EXPECT_EQ(Utf8ToLatin1("\xE2\x9C\x93 \xF0\x9F\x98\x80"), "? ?") << "outside Latin-1";
    EXPECT_EQ(Utf8ToLatin1("bad \xC3"), "bad ?") << "a truncated sequence";
    EXPECT_EQ(Utf8ToLatin1("bad \x80\x80"), "bad ??") << "a stray continuation, per byte";
    EXPECT_EQ(Latin1ToUtf8("caf\xE9 \xFF"), "caf\xC3\xA9 \xC3\xBF");
    for (int value = 0; value < 256; ++value)
    {
        const std::string one(1, static_cast<char>(value));
        EXPECT_EQ(Utf8ToLatin1(Latin1ToUtf8(one)), one) << value;
    }
}

TEST(X11TextEncoding, Utf8TextIsRecognisedUnderEachOfItsNames)
{
    using CNA::Platform::X11::IsUtf8TextFormat;
    EXPECT_TRUE(IsUtf8TextFormat("text/plain;charset=utf-8"));
    EXPECT_TRUE(IsUtf8TextFormat("text/plain; charset=UTF-8"));
    EXPECT_TRUE(IsUtf8TextFormat("text/plain"));
    EXPECT_TRUE(IsUtf8TextFormat("UTF8_STRING"));
    EXPECT_FALSE(IsUtf8TextFormat("text/html"));
    EXPECT_FALSE(IsUtf8TextFormat("STRING")) << "Latin-1";
    EXPECT_FALSE(IsUtf8TextFormat("text/plain;charset=iso-8859-1"));
}

// --- the peer: another X client, in a process of its own ---------------------------------------------

TEST(X11SelectionPeer, DISABLED_Run)
{
    // Started by X11SelectionLive::Start. "own": takes the selection and serves its targets --
    // TARGETS, and each type with the same bytes, through INCR above the chunk size -- until it
    // loses the selection or is stopped. "read": converts the selection to one target as any
    // paste does, INCR included, and writes what it got. Exit 3: the conversion was refused.
    const char* mode = std::getenv("CNA_X11_PEER_MODE");
    if (mode == nullptr)
    {
        GTEST_SKIP() << "run by X11SelectionLive";
    }
    std::ofstream report(std::getenv("CNA_X11_PEER_REPORT"));
    const std::string data = ReadFile(std::getenv("CNA_X11_PEER_DATA"));
    const std::vector<std::string> typeNames = Split(std::getenv("CNA_X11_PEER_TYPES"), ',');
    const std::size_t chunk = std::strtoul(std::getenv("CNA_X11_PEER_CHUNK"), nullptr, 10);

    ::Display* display = XOpenDisplay(nullptr);
    ASSERT_NE(display, nullptr);
    const auto atom = [display](const std::string& name) {
        return XInternAtom(display, name.c_str(), kXFalse);
    };
    const auto name = [display](const Atom value) {
        char* text = XGetAtomName(display, value);
        std::string result = text != nullptr ? text : "";
        if (text != nullptr) { XFree(text); }
        return result;
    };
    const Atom selection = atom(std::getenv("CNA_X11_PEER_SELECTION"));
    const Atom targets = atom("TARGETS");
    const Atom incr = atom("INCR");
    const ::Window window =
        XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(display, window, PropertyChangeMask);

    if (std::string(mode) == "read")
    {
        const Atom target = atom(typeNames.at(0));
        const Atom property = atom("CNA_PEER_PASTE");
        XConvertSelection(display, selection, target, property, window, kCurrentTime);
        XFlush(display);
        XEvent event{};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool answered = false;
        while (!answered && std::chrono::steady_clock::now() < deadline)
        {
            answered = XCheckTypedWindowEvent(display, window, SelectionNotify, &event) != 0;
            if (!answered) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        }
        if (!answered) { report << "timeout\n"; ::_exit(4); }
        if (event.xselection.property == kNone) { report << "refused\n"; ::_exit(3); }

        const auto take = [&](Atom& type, std::string& bytes) {
            int format = 0;
            unsigned long count = 0;
            unsigned long remaining = 0;
            unsigned char* value = nullptr;
            bytes.clear();
            if (XGetWindowProperty(display, window, property, 0, 0x7fffffff, True,
                                   AnyPropertyType, &type, &format, &count, &remaining, &value) != Success)
            {
                return false;
            }
            if (value != nullptr && format == 8)
            {
                bytes.assign(reinterpret_cast<char*>(value), count);
            }
            else if (value != nullptr && format == 32 && type == XA_ATOM)
            {
                const auto* atoms = reinterpret_cast<const Atom*>(value);
                for (unsigned long index = 0; index < count; ++index)
                {
                    bytes += (bytes.empty() ? "" : ",") + name(atoms[index]);
                }
            }
            if (value != nullptr) { XFree(value); }
            return true;
        };

        Atom type = kNone;
        std::string bytes;
        ASSERT_TRUE(take(type, bytes));
        std::string result;
        if (type == incr)
        {
            report << "incr\n";
            // Each chunk follows the deletion of the one before; a zero-length one ends it.
            while (true)
            {
                const auto chunkDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                bool arrived = false;
                while (!arrived && std::chrono::steady_clock::now() < chunkDeadline)
                {
                    if (XCheckTypedWindowEvent(display, window, PropertyNotify, &event) != 0 &&
                        event.xproperty.atom == property && event.xproperty.state == PropertyNewValue)
                    {
                        arrived = true;
                    }
                    else
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
                if (!arrived) { report << "stalled\n"; ::_exit(5); }
                ASSERT_TRUE(take(type, bytes));
                // A notification from before a chunk -- the INCR announcement's own -- whose
                // property is already gone: the chunk is still to come.
                if (type == kNone) { continue; }
                if (bytes.empty()) { break; }
                result += bytes;
            }
        }
        else
        {
            result = bytes;
        }
        report << "type=" << name(type) << "\n";
        std::ofstream(std::getenv("CNA_X11_PEER_OUTPUT"), std::ios::binary) << result;
        report.flush();
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        ::_exit(0);
    }

    // "own".
    std::vector<Atom> offered = {targets};
    for (const std::string& type : typeNames) { offered.push_back(atom(type)); }
    std::map<Atom, std::string> contents;
    const std::vector<std::string> dataList = Split(std::getenv("CNA_X11_PEER_DATA_LIST"), ',');
    for (std::size_t index = 0; index < typeNames.size(); ++index)
    {
        contents[offered[index + 1]] = index < dataList.size() ? ReadFile(dataList[index]) : data;
    }
    XSetSelectionOwner(display, selection, window, kCurrentTime);
    ASSERT_EQ(XGetSelectionOwner(display, selection), window);
    report << "ready\n";
    report.flush();

    struct Send
    {
        ::Window requestor;
        Atom property;
        Atom type;
        std::size_t offset;
        const std::string* bytes;
    };
    std::map<::Window, Send> sends;
    static volatile std::sig_atomic_t stop = 0;
    std::signal(SIGTERM, [](int) { stop = 1; });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (stop == 0 && std::chrono::steady_clock::now() < deadline)
    {
        if (XPending(display) == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        XEvent event{};
        XNextEvent(display, &event);
        if (event.type == SelectionClear)
        {
            report << "cleared\n";
            break;
        }
        if (event.type == PropertyNotify && event.xproperty.state == PropertyDelete)
        {
            const auto found = sends.find(event.xproperty.window);
            if (found == sends.end() || found->second.property != event.xproperty.atom) { continue; }
            Send& send = found->second;
            const std::string& bytes = *send.bytes;
            const std::size_t length = std::min(chunk, bytes.size() - send.offset);
            XChangeProperty(display, send.requestor, send.property, send.type, 8, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(bytes.data() + send.offset),
                            static_cast<int>(length));
            send.offset += length;
            if (length == 0)
            {
                XSelectInput(display, send.requestor, NoEventMask);
                sends.erase(found);
            }
            XFlush(display);
            continue;
        }
        if (event.type != SelectionRequest) { continue; }

        const XSelectionRequestEvent& request = event.xselectionrequest;
        report << "request " << name(request.target) << "\n";
        XEvent notify{};
        notify.xselection.type = SelectionNotify;
        notify.xselection.requestor = request.requestor;
        notify.xselection.selection = request.selection;
        notify.xselection.target = request.target;
        notify.xselection.time = request.time;
        notify.xselection.property = request.property;
        if (request.target == targets)
        {
            XChangeProperty(display, request.requestor, request.property, XA_ATOM, 32,
                            PropModeReplace, reinterpret_cast<const unsigned char*>(offered.data()),
                            static_cast<int>(offered.size()));
        }
        else if (const auto content = contents.find(request.target); content != contents.end())
        {
            const std::string& bytes = content->second;
            if (bytes.size() <= chunk)
            {
                XChangeProperty(display, request.requestor, request.property, request.target, 8,
                                PropModeReplace, reinterpret_cast<const unsigned char*>(bytes.data()),
                                static_cast<int>(bytes.size()));
            }
            else
            {
                report << "incr\n";
                XSelectInput(display, request.requestor, PropertyChangeMask);
                const long total = static_cast<long>(bytes.size());
                XChangeProperty(display, request.requestor, request.property, incr, 32,
                                PropModeReplace, reinterpret_cast<const unsigned char*>(&total), 1);
                sends[request.requestor] = {request.requestor, request.property, request.target, 0,
                                            &bytes};
            }
        }
        else
        {
            notify.xselection.property = kNone;
        }
        XSendEvent(display, request.requestor, kXFalse, NoEventMask, &notify);
        XFlush(display);
    }
    report.flush();
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    ::_exit(0);
}

} // namespace
