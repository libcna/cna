// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0154: files and text dropped on a CNA window by another client, through
// XDND.
//
// The parsing -- which type a drop is read as, what a text/uri-list names -- is shared with the
// Wayland drop target and tested in FreedesktopDropParsingTests.cpp. The protocol runs against the launcher's private Xvfb with a real drag source: this
// binary started again as a separate process (X11DragSource below), a different X client
// speaking XDND from the outside, exactly as a file manager would. No window manager is needed:
// XDND is between the two clients.

#include <gtest/gtest.h>

#include "../../../src/X11/X11DragAndDrop.hpp"
#include "../../../src/X11/X11Headers.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <variant>
#include <vector>

extern char** environ;

namespace {

using namespace CNA::Platform;
using CNA::Platform::X11::kCurrentTime;
using CNA::Platform::X11::kNone;
using CNA::Platform::X11::kXFalse;


// --- the protocol, against a real drag source ---------------------------------------------------------

/// What the drag source is told to do, passed through the environment of its own process.
struct DragScript
{
    std::vector<std::string> types;
    std::string data;
    /// "drop", "leave", or "stall" -- drop, then never answer the selection request.
    std::string ending = "drop";
    /// Where the drag moves, in the target window's coordinates.
    std::vector<std::pair<int, int>> positions = {{40, 30}, {60, 45}};
};

std::string Joined(const std::vector<std::string>& parts, const char separator)
{
    std::string joined;
    for (std::size_t index = 0; index < parts.size(); ++index)
    {
        if (index > 0) { joined += separator; }
        joined += parts[index];
    }
    return joined;
}

class X11DragAndDropLive : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const char* display = std::getenv("DISPLAY");
        const char* privateServer = std::getenv("CNA_X11_PRIVATE_TEST_SERVER");
        if (display == nullptr || display[0] == '\0' || privateServer == nullptr ||
            std::string(privateServer) != "1")
        {
            GTEST_SKIP() << "needs tools/platform/x11_test_server.sh";
        }
        platform_ = PlatformFactory::Create("X11");
        platform_->AcquireSubsystem(PlatformSubsystem::Video);
        acquired_ = true;
        WindowDescription description;
        description.title = "CNA drop target";
        description.x = 100;
        description.y = 80;
        description.width = 320;
        description.height = 240;
        window_ = platform_->CreateWindow(description);
        const WindowId id = window_->GetId();
        ASSERT_TRUE(PumpUntil([id](const std::vector<PlatformEvent>& events) {
            return std::any_of(events.begin(), events.end(), [id](const PlatformEvent& event) {
                const auto* windowEvent = std::get_if<WindowEvent>(&event);
                return windowEvent != nullptr && windowEvent->window == id &&
                       (windowEvent->kind == WindowEventKind::Restored ||
                        windowEvent->kind == WindowEventKind::Exposed);
            });
        })) << "the window never mapped";
        seen_.clear();
    }

    void TearDown() override
    {
        window_.reset();
        if (acquired_)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
        platform_.reset();
        std::remove(reportPath_.c_str());
        std::remove(dataPath_.c_str());
    }

    bool PumpUntil(const std::function<bool(const std::vector<PlatformEvent>&)>& done,
                   const std::chrono::milliseconds budget = std::chrono::milliseconds(3000))
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        std::vector<PlatformEvent> batch;
        while (std::chrono::steady_clock::now() < deadline)
        {
            platform_->PollEvents(batch);
            seen_.insert(seen_.end(), batch.begin(), batch.end());
            if (done(seen_))
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    /// Runs the drag source to the end, pumping CNA's events all the while -- the target has to
    /// answer the source's messages for the source to get anywhere.
    void Drag(const DragScript& script)
    {
        char reportTemplate[] = "/tmp/cna-dnd-report-XXXXXX";
        const int reportFd = mkstemp(reportTemplate);
        ASSERT_GE(reportFd, 0);
        ::close(reportFd);
        reportPath_ = reportTemplate;
        char dataTemplate[] = "/tmp/cna-dnd-data-XXXXXX";
        const int dataFd = mkstemp(dataTemplate);
        ASSERT_GE(dataFd, 0);
        ASSERT_EQ(::write(dataFd, script.data.data(), script.data.size()),
                  static_cast<ssize_t>(script.data.size()));
        ::close(dataFd);
        dataPath_ = dataTemplate;

        std::vector<std::string> positions;
        for (const auto& [x, y] : script.positions)
        {
            positions.push_back(std::to_string(x) + ":" + std::to_string(y));
        }
        std::vector<std::string> environment;
        for (char** entry = environ; *entry != nullptr; ++entry)
        {
            environment.emplace_back(*entry);
        }
        environment.push_back("CNA_X11_DND_TARGET=" + std::to_string(window_->GetWindowHandle()));
        environment.push_back("CNA_X11_DND_TYPES=" + Joined(script.types, ','));
        environment.push_back("CNA_X11_DND_DATA=" + dataPath_);
        environment.push_back("CNA_X11_DND_ENDING=" + script.ending);
        environment.push_back("CNA_X11_DND_POSITIONS=" + Joined(positions, ','));
        environment.push_back("CNA_X11_DND_REPORT=" + reportPath_);
        std::vector<char*> environmentPointers;
        for (std::string& entry : environment) { environmentPointers.push_back(entry.data()); }
        environmentPointers.push_back(nullptr);
        std::string executable = "/proc/self/exe";
        std::string filter = "--gtest_filter=X11DragSource.DISABLED_Drag";
        std::string disabled = "--gtest_also_run_disabled_tests";
        std::vector<char*> arguments = {executable.data(), filter.data(), disabled.data(), nullptr};

        const pid_t source = ::fork();
        ASSERT_GE(source, 0);
        if (source == 0)
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

        int status = 0;
        const bool finished = PumpUntil([source, &status](const std::vector<PlatformEvent>&) {
            return ::waitpid(source, &status, WNOHANG) == source;
        }, std::chrono::seconds(15));
        if (!finished)
        {
            ::kill(source, SIGKILL);
            ::waitpid(source, &status, 0);
        }
        ASSERT_TRUE(finished) << "the drag source never finished";
        ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0)
            << "the drag source failed; its report: " << Report();
        // What CNA delivered after the source was done -- the Complete of a stalled drop, say.
        (void) PumpUntil([](const std::vector<PlatformEvent>&) { return false; },
                         std::chrono::milliseconds(200));
    }

    [[nodiscard]] std::string Report() const
    {
        std::ifstream in(reportPath_);
        std::stringstream contents;
        contents << in.rdbuf();
        return contents.str();
    }

    [[nodiscard]] std::vector<DropEvent> Drops() const
    {
        std::vector<DropEvent> drops;
        for (const PlatformEvent& event : seen_)
        {
            if (const auto* drop = std::get_if<DropEvent>(&event))
            {
                drops.push_back(*drop);
            }
        }
        return drops;
    }

    [[nodiscard]] std::string Kinds() const
    {
        std::string kinds;
        for (const DropEvent& drop : Drops())
        {
            if (!kinds.empty()) { kinds += ' '; }
            kinds += ToString(drop.kind);
        }
        return kinds;
    }

    std::unique_ptr<IPlatform> platform_;
    std::unique_ptr<IPlatformWindow> window_;
    std::vector<PlatformEvent> seen_;
    std::string reportPath_;
    std::string dataPath_;
    bool acquired_ = false;
};

TEST_F(X11DragAndDropLive, EveryWindowAnnouncesXdndVersionFive)
{
    EXPECT_TRUE(platform_->GetCapabilities().dragAndDrop);
    ::Display* display = XOpenDisplay(nullptr);
    ASSERT_NE(display, nullptr);
    Atom type = kNone;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char* data = nullptr;
    ASSERT_EQ(XGetWindowProperty(display, static_cast<::Window>(window_->GetWindowHandle()),
                                 XInternAtom(display, "XdndAware", kXFalse), 0, 1, kXFalse, XA_ATOM,
                                 &type, &format, &count, &remaining, &data),
              Success);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(format, 32);
    EXPECT_EQ(*reinterpret_cast<long*>(data), 5);
    XFree(data);
    XCloseDisplay(display);
}

TEST_F(X11DragAndDropLive, FilesDroppedArriveAsPathsBetweenBeginAndComplete)
{
    DragScript script;
    script.types = {"text/uri-list", "text/plain"};
    script.data = "file:///tmp/cna%20level.xnb\r\nfile://localhost/etc/hostname\r\n";
    Drag(script);

    EXPECT_EQ(Kinds(), "Begin Position Position File File Complete");
    const std::vector<DropEvent> drops = Drops();
    ASSERT_EQ(drops.size(), 6u);
    for (const DropEvent& drop : drops)
    {
        EXPECT_EQ(drop.window, window_->GetId());
    }
    EXPECT_FLOAT_EQ(drops[1].x, 40.0f);
    EXPECT_FLOAT_EQ(drops[1].y, 30.0f);
    EXPECT_FLOAT_EQ(drops[2].x, 60.0f);
    EXPECT_FLOAT_EQ(drops[2].y, 45.0f);
    EXPECT_EQ(drops[3].data, "/tmp/cna level.xnb");
    EXPECT_EQ(drops[4].data, "/etc/hostname");
    EXPECT_FLOAT_EQ(drops[3].x, 60.0f) << "a file lands where the drag was dropped";
    // And the source heard what it needed: accepted as a copy at every position, and finished.
    const std::string report = Report();
    EXPECT_NE(report.find("status 3 XdndActionCopy"), std::string::npos) << report;
    EXPECT_NE(report.find("finished 1 XdndActionCopy"), std::string::npos) << report;
}

TEST_F(X11DragAndDropLive, TextArrivesWholeInOneEvent)
{
    // SDL3's X11 backend splits a text drop into one event per line; the contract's Text is the
    // text that was dropped.
    DragScript script;
    script.types = {"UTF8_STRING"};
    script.data = "first line\nsecond line \xE2\x80\x94 \xC5\xBE";
    Drag(script);

    EXPECT_EQ(Kinds(), "Begin Position Position Text Complete");
    const std::vector<DropEvent> drops = Drops();
    ASSERT_EQ(drops.size(), 5u);
    EXPECT_EQ(drops[3].data, script.data);
    EXPECT_NE(Report().find("finished 1"), std::string::npos) << Report();
}

TEST_F(X11DragAndDropLive, ALinkDroppedFromABrowserArrivesAsText)
{
    DragScript script;
    script.types = {"text/uri-list"};
    script.data = "https://example.org/levels/7\r\n";
    Drag(script);

    const std::vector<DropEvent> drops = Drops();
    ASSERT_EQ(Kinds(), "Begin Position Position Text Complete");
    EXPECT_EQ(drops[3].data, "https://example.org/levels/7");
}

TEST_F(X11DragAndDropLive, Latin1TextIsDeliveredAsUtf8)
{
    DragScript script;
    script.types = {"STRING"};
    script.data = "caf\xE9";
    Drag(script);

    const std::vector<DropEvent> drops = Drops();
    ASSERT_EQ(Kinds(), "Begin Position Position Text Complete");
    EXPECT_EQ(drops[3].data, "caf\xC3\xA9");
}

TEST_F(X11DragAndDropLive, MoreThanThreeTypesAreReadFromTheSourcesTypeList)
{
    DragScript script;
    script.types = {"image/png", "text/html", "application/x-cna-test", "text/uri-list"};
    script.data = "file:///tmp/from-the-list\r\n";
    Drag(script);

    const std::vector<DropEvent> drops = Drops();
    ASSERT_EQ(Kinds(), "Begin Position Position File Complete");
    EXPECT_EQ(drops[3].data, "/tmp/from-the-list");
}

TEST_F(X11DragAndDropLive, ADragThatLeavesEndsWithCompleteAlone)
{
    DragScript script;
    script.types = {"text/uri-list"};
    script.data = "file:///tmp/never";
    script.ending = "leave";
    Drag(script);

    EXPECT_EQ(Kinds(), "Begin Position Position Complete");
    EXPECT_EQ(Report().find("finished"), std::string::npos) << "a leave is not a drop";
}

TEST_F(X11DragAndDropLive, ADragOfNothingTheWindowCanTakeIsRefusedAndSaysNothing)
{
    DragScript script;
    script.types = {"image/png"};
    script.data = "\x89PNG";
    Drag(script);

    EXPECT_EQ(Kinds(), "") << "a drag the game cannot receive must not look like one it can";
    const std::string report = Report();
    EXPECT_NE(report.find("status 0 None"), std::string::npos) << report;
    EXPECT_NE(report.find("finished 0 None"), std::string::npos) << report;
}

TEST_F(X11DragAndDropLive, ASourceThatNeverDeliversCostsABoundedWaitAndStillEnds)
{
    // The source drops and then never answers the conversion -- it hangs, or dies. The event pump
    // waits its selection timeout, not forever, and the game's sequence still ends.
    DragScript script;
    script.types = {"text/uri-list"};
    script.data = "file:///tmp/never";
    script.ending = "stall";
    const auto started = std::chrono::steady_clock::now();
    Drag(script);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(8));

    EXPECT_EQ(Kinds(), "Begin Position Position Complete");
    EXPECT_NE(Report().find("finished 0 None"), std::string::npos) << Report();
}

// --- the drag source ---------------------------------------------------------------------------------

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

TEST(X11DragSource, DISABLED_Drag)
{
    // Started by X11DragAndDropLive::Drag in a process of its own. A minimal XDND 5 source:
    // enter, positions (each answered before the next), then a drop, a leave, or a drop it
    // never delivers; it serves the conversion and records what the target told it.
    const char* targetText = std::getenv("CNA_X11_DND_TARGET");
    if (targetText == nullptr)
    {
        GTEST_SKIP() << "run by X11DragAndDropLive";
    }
    const auto target = static_cast<::Window>(std::strtoul(targetText, nullptr, 10));
    const std::vector<std::string> typeNames = Split(std::getenv("CNA_X11_DND_TYPES"), ',');
    const std::string ending = std::getenv("CNA_X11_DND_ENDING");
    std::ifstream dataFile(std::getenv("CNA_X11_DND_DATA"), std::ios::binary);
    const std::string data((std::istreambuf_iterator<char>(dataFile)), std::istreambuf_iterator<char>());
    std::ofstream report(std::getenv("CNA_X11_DND_REPORT"));

    ::Display* display = XOpenDisplay(nullptr);
    ASSERT_NE(display, nullptr);
    const ::Window root = DefaultRootWindow(display);
    const ::Window source = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(display, source, PropertyChangeMask);
    const auto atom = [display](const char* name) { return XInternAtom(display, name, kXFalse); };
    const Atom xdndSelection = atom("XdndSelection");
    const Atom actionCopy = atom("XdndActionCopy");
    std::vector<Atom> types;
    for (const std::string& name : typeNames) { types.push_back(atom(name.c_str())); }
    XSetSelectionOwner(display, xdndSelection, source, kCurrentTime);

    const auto nameOf = [display](const Atom value) -> std::string {
        if (value == kNone) { return "None"; }
        char* name = XGetAtomName(display, value);
        std::string result = name != nullptr ? name : "?";
        if (name != nullptr) { XFree(name); }
        return result;
    };
    const auto send = [&](const char* type, const long l1, const long l2, const long l3,
                          const long l4) {
        XEvent message{};
        message.xclient.type = ClientMessage;
        message.xclient.window = target;
        message.xclient.message_type = atom(type);
        message.xclient.format = 32;
        message.xclient.data.l[0] = static_cast<long>(source);
        message.xclient.data.l[1] = l1;
        message.xclient.data.l[2] = l2;
        message.xclient.data.l[3] = l3;
        message.xclient.data.l[4] = l4;
        XSendEvent(display, target, kXFalse, NoEventMask, &message);
        XFlush(display);
    };
    // Waits for one client message to the source, serving conversions meanwhile.
    const auto await = [&](const Atom wanted, XClientMessageEvent& answer,
                           const bool serve) -> bool {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline)
        {
            while (XPending(display) > 0)
            {
                XEvent event;
                XNextEvent(display, &event);
                if (event.type == ClientMessage && event.xclient.message_type == wanted)
                {
                    answer = event.xclient;
                    return true;
                }
                if (event.type == SelectionRequest && serve)
                {
                    const XSelectionRequestEvent& request = event.xselectionrequest;
                    XEvent notify{};
                    notify.xselection.type = SelectionNotify;
                    notify.xselection.requestor = request.requestor;
                    notify.xselection.selection = request.selection;
                    notify.xselection.target = request.target;
                    notify.xselection.time = request.time;
                    notify.xselection.property = kNone;
                    if (std::find(types.begin(), types.end(), request.target) != types.end())
                    {
                        XChangeProperty(display, request.requestor, request.property,
                                        request.target, 8, PropModeReplace,
                                        reinterpret_cast<const unsigned char*>(data.data()),
                                        static_cast<int>(data.size()));
                        notify.xselection.property = request.property;
                        report << "served " << nameOf(request.target) << "\n";
                    }
                    XSendEvent(display, request.requestor, kXFalse, NoEventMask, &notify);
                    XFlush(display);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    };

    // Enter: up to three types in the message, more through XdndTypeList.
    long flags = 5L << 24;
    long inline0 = 0;
    long inline1 = 0;
    long inline2 = 0;
    if (types.size() > 3)
    {
        flags |= 1;
        XChangeProperty(display, source, atom("XdndTypeList"), XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(types.data()),
                        static_cast<int>(types.size()));
    }
    else
    {
        if (types.size() > 0) { inline0 = static_cast<long>(types[0]); }
        if (types.size() > 1) { inline1 = static_cast<long>(types[1]); }
        if (types.size() > 2) { inline2 = static_cast<long>(types[2]); }
    }
    send("XdndEnter", flags, inline0, inline1, inline2);

    for (const std::string& position : Split(std::getenv("CNA_X11_DND_POSITIONS"), ','))
    {
        const std::size_t colon = position.find(':');
        int rootX = 0;
        int rootY = 0;
        ::Window child = kNone;
        XTranslateCoordinates(display, target, root, std::stoi(position.substr(0, colon)),
                              std::stoi(position.substr(colon + 1)), &rootX, &rootY, &child);
        send("XdndPosition", 0, (static_cast<long>(rootX) << 16) | rootY, 0,
             static_cast<long>(actionCopy));
        XClientMessageEvent status{};
        ASSERT_TRUE(await(atom("XdndStatus"), status, true)) << "no XdndStatus";
        report << "status " << status.data.l[1] << " " << nameOf(static_cast<Atom>(status.data.l[4]))
               << "\n";
    }

    if (ending == "leave")
    {
        send("XdndLeave", 0, 0, 0, 0);
        report << "left\n";
    }
    else
    {
        send("XdndDrop", 0, 0, 0, 0);
        XClientMessageEvent finished{};
        ASSERT_TRUE(await(atom("XdndFinished"), finished, ending != "stall")) << "no XdndFinished";
        report << "finished " << finished.data.l[1] << " "
               << nameOf(static_cast<Atom>(finished.data.l[2])) << "\n";
    }
    report.flush();
    XDestroyWindow(display, source);
    XCloseDisplay(display);
}

} // namespace
