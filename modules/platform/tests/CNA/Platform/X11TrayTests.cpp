// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0171: tray icons through X11's own system tray protocol.
//
// On the launcher's server only. The test plays the tray -- it owns _NET_SYSTEM_TRAY_S0, announces
// itself, and embeds what asks to be docked, as Xfce's or MATE's panel does -- and clicks the icon
// and its menu with events it sends.

#include <gtest/gtest.h>

#include "../../../src/X11/X11Headers.hpp"
#include "../../../src/X11/X11Tray.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::X11;

/// A system tray, as a panel runs one: on a connection of its own.
class FakeTray
{
public:
    FakeTray()
    {
        display_ = XOpenDisplay(nullptr);
        if (display_ == nullptr)
        {
            return;
        }
        root_ = DefaultRootWindow(display_);
        selection_ = XInternAtom(display_, "_NET_SYSTEM_TRAY_S0", kXFalse);
        opcode_ = XInternAtom(display_, "_NET_SYSTEM_TRAY_OPCODE", kXFalse);
        panel_ = XCreateSimpleWindow(display_, root_, 0, 0, 300, 30, 0, 0, 0x808080);
        XSelectInput(display_, panel_, SubstructureNotifyMask);
        XMapWindow(display_, panel_);
        XSetSelectionOwner(display_, selection_, panel_, kCurrentTime);
        XEvent announce{};
        announce.xclient.type = ClientMessage;
        announce.xclient.window = root_;
        announce.xclient.message_type = XInternAtom(display_, "MANAGER", kXFalse);
        announce.xclient.format = 32;
        announce.xclient.data.l[0] = static_cast<long>(kCurrentTime);
        announce.xclient.data.l[1] = static_cast<long>(selection_);
        announce.xclient.data.l[2] = static_cast<long>(panel_);
        XSendEvent(display_, root_, kXFalse, StructureNotifyMask, &announce);
        XSync(display_, kXFalse);
    }

    ~FakeTray()
    {
        if (display_ != nullptr)
        {
            XCloseDisplay(display_);
        }
    }

    FakeTray(const FakeTray&) = delete;
    FakeTray& operator=(const FakeTray&) = delete;

    [[nodiscard]] bool Ready() const { return display_ != nullptr; }

    /// Reads the tray's queue: docks what asks, notes what it lost.
    void Pump()
    {
        while (XPending(display_) > 0)
        {
            XEvent event{};
            XNextEvent(display_, &event);
            if (event.type == ClientMessage && event.xclient.message_type == opcode_ && event.xclient.data.l[1] == 0)
            {
                const auto icon = static_cast<::Window>(event.xclient.data.l[2]);
                requests_.push_back(icon);
                // Embed it as a panel does: reparented, sized, kept should the panel go first.
                XAddToSaveSet(display_, icon);
                XReparentWindow(display_, icon, panel_, 4 + 26 * static_cast<int>(docked_.size()), 4);
                XResizeWindow(display_, icon, 22, 22);
                XMapWindow(display_, icon);
                docked_.push_back(icon);
                XSync(display_, kXFalse);
            }
            else if (event.type == DestroyNotify)
            {
                destroyed_.push_back(event.xdestroywindow.window);
            }
        }
    }

    [[nodiscard]] const std::vector<::Window>& Requests() const { return requests_; }
    [[nodiscard]] const std::vector<::Window>& Destroyed() const { return destroyed_; }
    [[nodiscard]] Display* GetDisplay() const { return display_; }

private:
    Display* display_ = nullptr;
    ::Window root_ = kNone;
    ::Window panel_ = kNone;
    Atom selection_ = kNone;
    Atom opcode_ = kNone;
    std::vector<::Window> requests_;
    std::vector<::Window> docked_;
    std::vector<::Window> destroyed_;
};

class X11TrayLive : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const char* privateServer = std::getenv("CNA_X11_PRIVATE_TEST_SERVER");
        if (privateServer == nullptr || std::string(privateServer) != "1")
        {
            GTEST_SKIP() << "plays the system tray; needs tools/platform/x11_test_server.sh";
        }
        observer_ = XOpenDisplay(nullptr);
        ASSERT_NE(observer_, nullptr);
    }

    void TearDown() override
    {
        icon_.reset();
        platform_.reset();
        tray_.reset();
        if (observer_ != nullptr)
        {
            XCloseDisplay(observer_);
        }
    }

    void StartTrayAndPlatform()
    {
        tray_ = std::make_unique<FakeTray>();
        ASSERT_TRUE(tray_->Ready());
        platform_ = PlatformFactory::Create("X11");
        platform_->AcquireSubsystem(PlatformSubsystem::Video);
    }

    /// Pumps the platform and the tray until a condition holds.
    bool PumpUntil(const std::function<bool()>& done, const std::chrono::milliseconds budget = std::chrono::milliseconds(3000))
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        std::vector<PlatformEvent> events;
        while (std::chrono::steady_clock::now() < deadline)
        {
            platform_->PollEvents(events);
            if (tray_ != nullptr) { tray_->Pump(); }
            if (done()) { return true; }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return done();
    }

    void PumpFor(const std::chrono::milliseconds duration)
    {
        (void) PumpUntil([] { return false; }, duration);
    }

    [[nodiscard]] ::Window IconWindow() const
    {
        return static_cast<const X11TrayIcon*>(icon_.get())->GetWindow();
    }

    /// An override-redirect window of a given type that is on the screen, or none.
    [[nodiscard]] ::Window FindMapped(const char* type) const
    {
        const Atom windowType = XInternAtom(observer_, "_NET_WM_WINDOW_TYPE", kXFalse);
        const Atom wanted = XInternAtom(observer_, type, kXFalse);
        ::Window rootReturn = kNone;
        ::Window parent = kNone;
        ::Window* children = nullptr;
        unsigned int count = 0;
        ::Window found = kNone;
        XQueryTree(observer_, DefaultRootWindow(observer_), &rootReturn, &parent, &children, &count);
        for (unsigned int index = 0; index < count && found == kNone; ++index)
        {
            XWindowAttributes attributes{};
            if (XGetWindowAttributes(observer_, children[index], &attributes) == 0 || attributes.map_state != IsViewable ||
                attributes.override_redirect == 0)
            {
                continue;
            }
            Atom type_ = kNone;
            int format = 0;
            unsigned long items = 0;
            unsigned long remaining = 0;
            unsigned char* data = nullptr;
            if (XGetWindowProperty(observer_, children[index], windowType, 0, 1, kXFalse, XA_ATOM, &type_, &format,
                                   &items, &remaining, &data) == Success &&
                data != nullptr && items == 1 && reinterpret_cast<Atom*>(data)[0] == wanted)
            {
                found = children[index];
            }
            if (data != nullptr) { XFree(data); }
        }
        if (children != nullptr) { XFree(children); }
        return found;
    }

    void Click(const ::Window window, const int x, const int y, const unsigned int button = Button1) const
    {
        for (const int type : {ButtonPress, ButtonRelease})
        {
            XEvent event{};
            event.xbutton.type = type;
            event.xbutton.window = window;
            event.xbutton.root = DefaultRootWindow(observer_);
            event.xbutton.x = x;
            event.xbutton.y = y;
            event.xbutton.x_root = 100 + x;
            event.xbutton.y_root = 100 + y;
            event.xbutton.button = button;
            event.xbutton.same_screen = 1;
            XSendEvent(observer_, window, kXFalse, type == ButtonPress ? ButtonPressMask : ButtonReleaseMask, &event);
        }
        XSync(observer_, kXFalse);
    }

    /// The row a menu entry is drawn in: the menu's height split evenly, less its padding.
    [[nodiscard]] int RowCentre(const ::Window menu, const int index, const int entries) const
    {
        XWindowAttributes attributes{};
        XGetWindowAttributes(observer_, menu, &attributes);
        const int row = (attributes.height - 8) / entries;
        return 4 + row * index + row / 2;
    }

    [[nodiscard]] unsigned long PixelAt(const ::Window window, const int x, const int y) const
    {
        XImage* image = XGetImage(observer_, window, x, y, 1, 1, AllPlanes, ZPixmap);
        if (image == nullptr) { return 0; }
        const unsigned long pixel = XGetPixel(image, 0, 0);
        XDestroyImage(image);
        return pixel;
    }

    Display* observer_ = nullptr;
    std::unique_ptr<FakeTray> tray_;
    std::unique_ptr<IPlatform> platform_;
    std::unique_ptr<IPlatformTrayIcon> icon_;
};

TEST_F(X11TrayLive, WithoutATrayThereIsNoTrayService)
{
    platform_ = PlatformFactory::Create("X11");
    platform_->AcquireSubsystem(PlatformSubsystem::Video);
    EXPECT_FALSE(platform_->GetCapabilities().tray) << "nothing owns _NET_SYSTEM_TRAY_S0";
    EXPECT_EQ(platform_->GetTray(), nullptr);
}

TEST_F(X11TrayLive, AnIconAsksTheTrayToDockItAndIsDrawnThere)
{
    StartTrayAndPlatform();
    ASSERT_TRUE(platform_->GetCapabilities().tray);
    ASSERT_NE(platform_->GetTray(), nullptr);
    icon_ = platform_->GetTray()->CreateTray("Game");
    ASSERT_TRUE(PumpUntil([this] { return !tray_->Requests().empty(); })) << "no dock request came";
    EXPECT_EQ(tray_->Requests().front(), IconWindow());

    // XEmbed: version 0, and the tray maps it.
    const Atom info = XInternAtom(observer_, "_XEMBED_INFO", kXFalse);
    Atom type = kNone;
    int format = 0;
    unsigned long items = 0;
    unsigned long remaining = 0;
    unsigned char* data = nullptr;
    ASSERT_EQ(XGetWindowProperty(observer_, IconWindow(), info, 0, 2, kXFalse, info, &type, &format, &items,
                                 &remaining, &data),
              Success);
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(items, 2u);
    EXPECT_EQ(reinterpret_cast<unsigned long*>(data)[0], 0u);
    EXPECT_EQ(reinterpret_cast<unsigned long*>(data)[1], 1u);
    XFree(data);

    // Drawn: the badge's colour at a corner, and the letter's white somewhere in the middle.
    PumpFor(std::chrono::milliseconds(100));
    XWindowAttributes attributes{};
    ASSERT_NE(XGetWindowAttributes(observer_, IconWindow(), &attributes), 0);
    EXPECT_EQ(attributes.map_state, IsViewable);
    EXPECT_EQ(attributes.width, 22) << "the tray's size";
    XImage* image = XGetImage(observer_, IconWindow(), 0, 0, 22, 22, AllPlanes, ZPixmap);
    ASSERT_NE(image, nullptr);
    const unsigned long corner = XGetPixel(image, 1, 1);
    int white = 0;
    for (int y = 0; y < 22; ++y)
    {
        for (int x = 0; x < 22; ++x)
        {
            white += XGetPixel(image, x, y) == WhitePixel(observer_, DefaultScreen(observer_)) ? 1 : 0;
        }
    }
    XDestroyImage(image);
    XColor colour{};
    colour.pixel = corner;
    XQueryColor(observer_, DefaultColormap(observer_, DefaultScreen(observer_)), &colour);
    EXPECT_EQ(colour.red >> 8, 0x30);
    EXPECT_EQ(colour.blue >> 8, 0xC0);
    EXPECT_GT(white, 5) << "no letter was drawn";
}

TEST_F(X11TrayLive, AClickOpensTheMenuAndAChosenEntryRunsFromPollEvents)
{
    StartTrayAndPlatform();
    icon_ = platform_->GetTray()->CreateTray("Game");
    int paused = 0;
    int disabled = 0;
    int quit = 0;
    const std::size_t pause = icon_->AddEntry("Pause", true, false, true, [&paused] { ++paused; });
    const std::size_t grey = icon_->AddEntry("Unavailable", false, false, false, [&disabled] { ++disabled; });
    // Quit's callback destroys the very icon it belongs to, as a game's Quit would.
    const std::size_t exit = icon_->AddEntry("Quit", false, false, true, [this, &quit] {
        ++quit;
        icon_.reset();
    });
    EXPECT_EQ(pause, 0u);
    EXPECT_EQ(grey, 1u);
    EXPECT_EQ(exit, 2u);
    ASSERT_TRUE(PumpUntil([this] { return !tray_->Requests().empty(); }));
    PumpFor(std::chrono::milliseconds(50));

    Click(IconWindow(), 10, 10);
    ::Window menu = kNone;
    ASSERT_TRUE(PumpUntil([&] { return (menu = FindMapped("_NET_WM_WINDOW_TYPE_POPUP_MENU")) != kNone; }))
        << "no menu opened";
    PumpFor(std::chrono::milliseconds(50));
    XWindowAttributes attributes{};
    XGetWindowAttributes(observer_, menu, &attributes);
    EXPECT_GE(attributes.width, 120);

    // The greyed entry: nothing happens, and the menu stays.
    Click(menu, 40, RowCentre(menu, 1, 3));
    PumpFor(std::chrono::milliseconds(100));
    EXPECT_EQ(disabled, 0);
    EXPECT_EQ(FindMapped("_NET_WM_WINDOW_TYPE_POPUP_MENU"), menu);

    // The checkable one: toggled, then its callback -- from PollEvents -- and the menu gone.
    Click(menu, 40, RowCentre(menu, 0, 3));
    ASSERT_TRUE(PumpUntil([&] { return paused == 1; }));
    EXPECT_TRUE(icon_->GetEntryChecked(pause));
    EXPECT_TRUE(PumpUntil([this] { return FindMapped("_NET_WM_WINDOW_TYPE_POPUP_MENU") == kNone; }));

    // Again, and a click outside closes it without choosing anything.
    Click(IconWindow(), 10, 10);
    ASSERT_TRUE(PumpUntil([&] { return (menu = FindMapped("_NET_WM_WINDOW_TYPE_POPUP_MENU")) != kNone; }));
    Click(menu, -50, -50);
    EXPECT_TRUE(PumpUntil([this] { return FindMapped("_NET_WM_WINDOW_TYPE_POPUP_MENU") == kNone; }));
    EXPECT_EQ(paused + disabled + quit, 1);

    // And Quit, whose callback destroys the icon from inside the tray's own pump.
    Click(IconWindow(), 10, 10);
    ASSERT_TRUE(PumpUntil([&] { return (menu = FindMapped("_NET_WM_WINDOW_TYPE_POPUP_MENU")) != kNone; }));
    PumpFor(std::chrono::milliseconds(50));
    Click(menu, 40, RowCentre(menu, 2, 3));
    ASSERT_TRUE(PumpUntil([&] { return quit == 1; }));
    EXPECT_EQ(icon_, nullptr);
    PumpFor(std::chrono::milliseconds(50));  // Nothing of the icon is touched afterwards.
    EXPECT_EQ(paused, 1);
}

TEST_F(X11TrayLive, EntryChangesAreReadBackAndUnknownIndicesAreIgnored)
{
    StartTrayAndPlatform();
    icon_ = platform_->GetTray()->CreateTray("");
    const std::size_t sound = icon_->AddEntry("Sound", true, true, true, {});
    const std::size_t plain = icon_->AddEntry("About", false, true, true, {});
    EXPECT_TRUE(icon_->GetEntryChecked(sound));
    EXPECT_FALSE(icon_->GetEntryChecked(plain)) << "an entry without a check mark is never checked";
    icon_->SetEntryChecked(sound, false);
    EXPECT_FALSE(icon_->GetEntryChecked(sound));
    icon_->SetEntryEnabled(plain, false);
    EXPECT_FALSE(icon_->GetEntryEnabled(plain));
    icon_->SetEntryLabel(plain, "About this game");
    icon_->SetEntryChecked(99, true);
    icon_->SetEntryEnabled(99, true);
    icon_->SetEntryLabel(99, "nothing");
    EXPECT_FALSE(icon_->GetEntryChecked(99));
    EXPECT_FALSE(icon_->GetEntryEnabled(99));
    icon_->SetTooltip("Renamed");
    const Atom name = XInternAtom(observer_, "_NET_WM_NAME", kXFalse);
    Atom type = kNone;
    int format = 0;
    unsigned long items = 0;
    unsigned long remaining = 0;
    unsigned char* data = nullptr;
    PumpFor(std::chrono::milliseconds(50));
    ASSERT_EQ(XGetWindowProperty(observer_, IconWindow(), name, 0, 64, kXFalse, AnyPropertyType, &type, &format,
                                 &items, &remaining, &data),
              Success);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(std::string(reinterpret_cast<char*>(data), items), "Renamed") << "the tray may show the icon's name";
    XFree(data);
}

TEST_F(X11TrayLive, TheTooltipAppearsWhileThePointerRestsOnTheIcon)
{
    StartTrayAndPlatform();
    icon_ = platform_->GetTray()->CreateTray("Game tooltip");
    ASSERT_TRUE(PumpUntil([this] { return !tray_->Requests().empty(); }));
    XEvent enter{};
    enter.xcrossing.type = EnterNotify;
    enter.xcrossing.window = IconWindow();
    enter.xcrossing.root = DefaultRootWindow(observer_);
    enter.xcrossing.same_screen = 1;
    XSendEvent(observer_, IconWindow(), kXFalse, EnterWindowMask, &enter);
    XSync(observer_, kXFalse);
    PumpFor(std::chrono::milliseconds(300));
    EXPECT_EQ(FindMapped("_NET_WM_WINDOW_TYPE_TOOLTIP"), kNone) << "not at once";
    ::Window tip = kNone;
    ASSERT_TRUE(PumpUntil([&] { return (tip = FindMapped("_NET_WM_WINDOW_TYPE_TOOLTIP")) != kNone; }))
        << "no tooltip after resting on the icon";
    XWindowAttributes attributes{};
    XGetWindowAttributes(observer_, tip, &attributes);
    EXPECT_GT(attributes.width, 40);

    XEvent leave = enter;
    leave.xcrossing.type = LeaveNotify;
    XSendEvent(observer_, IconWindow(), kXFalse, LeaveWindowMask, &leave);
    XSync(observer_, kXFalse);
    EXPECT_TRUE(PumpUntil([this] { return FindMapped("_NET_WM_WINDOW_TYPE_TOOLTIP") == kNone; }));
}

TEST_F(X11TrayLive, ATrayThatStartsAgainGetsEveryIconAgain)
{
    StartTrayAndPlatform();
    icon_ = platform_->GetTray()->CreateTray("Game");
    ASSERT_TRUE(PumpUntil([this] { return tray_->Requests().size() == 1; }));
    // The panel restarts: its connection goes, and with it its selection; the icon, in its
    // save-set, is handed back to the root window. Then a new one announces itself.
    tray_.reset();
    PumpFor(std::chrono::milliseconds(100));
    tray_ = std::make_unique<FakeTray>();
    ASSERT_TRUE(tray_->Ready());
    ASSERT_TRUE(PumpUntil([this] { return tray_->Requests().size() == 1; })) << "the icon never docked again";
    EXPECT_EQ(tray_->Requests().front(), IconWindow());
}

TEST_F(X11TrayLive, AnIconThatIsReleasedLeavesTheTray)
{
    StartTrayAndPlatform();
    icon_ = platform_->GetTray()->CreateTray("Game");
    ASSERT_TRUE(PumpUntil([this] { return tray_->Requests().size() == 1; }));
    const ::Window window = IconWindow();
    icon_.reset();
    ASSERT_TRUE(PumpUntil([&] {
        const auto& gone = tray_->Destroyed();
        return std::find(gone.begin(), gone.end(), window) != gone.end();
    })) << "the tray still holds the icon's window";
}

} // namespace
