// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0167: message boxes drawn with Xlib -- a dialog window of its own,
// transient for the game's, answered by a click, the keyboard or the window manager's close.
//
// The text wrapping and the layout need no server. The rest shows real boxes on the launcher's
// own server (CNA_X11_PRIVATE_TEST_SERVER) -- never on a desktop someone is using -- reads what
// they draw back with XGetImage, and answers them with events sent from a second client.

#include <gtest/gtest.h>

#include "../../../src/X11/X11Headers.hpp"
#include "../../../src/X11/X11MessageBox.hpp"

#include "CNA/Platform/PlatformEvent.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

namespace {

using namespace CNA::Platform;
using CNA::Platform::X11::DecodeMessageBoxText;
using CNA::Platform::X11::LayoutMessageBox;
using CNA::Platform::X11::MessageBoxButtonAt;
using CNA::Platform::X11::WrapMessageBoxText;
using CNA::Platform::X11::X11MessageBoxRect;
using CNA::Platform::X11::X11TextMeasure;
using CNA::Platform::X11::kCurrentTime;
using CNA::Platform::X11::kNone;
using CNA::Platform::X11::kXFalse;

int CodePoints(const std::string_view text)
{
    int count = 0;
    for (const char byte : text)
    {
        count += (static_cast<unsigned char>(byte) & 0xC0u) != 0x80u ? 1 : 0;
    }
    return count;
}

/// Ten pixels a character: a font whose arithmetic a test can do in its head.
const X11TextMeasure kTenPixels = [](const std::string_view text) { return 10 * CodePoints(text); };

// --- wrapping and layout -------------------------------------------------------------------------

TEST(X11MessageBoxGeometry, TextIsDecodedToTheCharactersAUnicodeCoreFontIndexes)
{
    EXPECT_EQ(DecodeMessageBoxText("Hi"), u"Hi");
    EXPECT_EQ(DecodeMessageBoxText("P\xC5\x99\xC3\xAD"), u"P\u0159\u00ED");
    EXPECT_EQ(DecodeMessageBoxText("\xE2\x82\xAC"), u"\u20AC");
    EXPECT_EQ(DecodeMessageBoxText("\xEF\xBF\xBF"), u"\uFFFF") << "the last BMP character";
}

TEST(X11MessageBoxGeometry, WhatACoreFontCannotShowBecomesTheReplacementCharacter)
{
    // Beyond the Basic Multilingual Plane: one replacement, the whole sequence consumed.
    EXPECT_EQ(DecodeMessageBoxText("a\xF0\x9F\x8E\xAE" "b"), u"a\uFFFD" u"b");
    // A lone continuation byte, an overlong form, an encoded surrogate, a byte never in UTF-8.
    EXPECT_EQ(DecodeMessageBoxText("\x80"), u"\uFFFD");
    EXPECT_EQ(DecodeMessageBoxText("\xC0\xAF"), u"\uFFFD\uFFFD");
    EXPECT_EQ(DecodeMessageBoxText("\xED\xA0\x80"), u"\uFFFD\uFFFD\uFFFD");
    EXPECT_EQ(DecodeMessageBoxText("\xFF" "a"), u"\uFFFD" u"a");
    // A truncated sequence: one replacement for its valid prefix, and the next character kept.
    EXPECT_EQ(DecodeMessageBoxText("\xE2\x82" "A"), u"\uFFFD" u"A");
    EXPECT_EQ(DecodeMessageBoxText("\xE2"), u"\uFFFD");
    // Past U+10FFFF.
    EXPECT_EQ(DecodeMessageBoxText("\xF4\x90\x80\x80"), u"\uFFFD\uFFFD\uFFFD\uFFFD");
}

TEST(X11MessageBoxGeometry, AShortMessageIsOneLine)
{
    EXPECT_EQ(WrapMessageBoxText("Hello", 100, kTenPixels), std::vector<std::string>{"Hello"});
}

TEST(X11MessageBoxGeometry, TheMessagesOwnLineBreaksAreKeptEmptyLinesToo)
{
    EXPECT_EQ(WrapMessageBoxText("a\n\nb\r\nc", 100, kTenPixels), (std::vector<std::string>{"a", "", "b", "c"}));
    EXPECT_EQ(WrapMessageBoxText("a\n", 100, kTenPixels), (std::vector<std::string>{"a", ""}));
}

TEST(X11MessageBoxGeometry, AnEmptyMessageIsOneEmptyLine)
{
    EXPECT_EQ(WrapMessageBoxText("", 100, kTenPixels), std::vector<std::string>{""});
}

TEST(X11MessageBoxGeometry, ALongLineBreaksAtTheLastSpaceThatFits)
{
    EXPECT_EQ(WrapMessageBoxText("the quick brown fox", 100, kTenPixels),
              (std::vector<std::string>{"the quick", "brown fox"}));
    // A space exactly at the edge: the whole fitting prefix is the line.
    EXPECT_EQ(WrapMessageBoxText("abcdefghij klm", 100, kTenPixels),
              (std::vector<std::string>{"abcdefghij", "klm"}));
}

TEST(X11MessageBoxGeometry, AWordWiderThanTheBoxBreaksAtTheLastCharacterThatFits)
{
    EXPECT_EQ(WrapMessageBoxText("abcdefghijklmnop", 50, kTenPixels),
              (std::vector<std::string>{"abcde", "fghij", "klmno", "p"}));
    // Narrower than one character: one character a line, never none.
    EXPECT_EQ(WrapMessageBoxText("ab", 5, kTenPixels), (std::vector<std::string>{"a", "b"}));
}

TEST(X11MessageBoxGeometry, ALineNeverBreaksInsideAUtf8Sequence)
{
    const std::vector<std::string> lines = WrapMessageBoxText("\xC5\xBE\xC5\xBE\xC5\xBE\xC5\xBE\xC5\xBE\xC5\xBE\xC5\xBE",
                                                              30, kTenPixels);
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "\xC5\xBE\xC5\xBE\xC5\xBE");
    EXPECT_EQ(lines[1], "\xC5\xBE\xC5\xBE\xC5\xBE");
    EXPECT_EQ(lines[2], "\xC5\xBE");
    // Four-byte sequences as well.
    const std::vector<std::string> emoji = WrapMessageBoxText("\xF0\x9F\x8E\xAE\xF0\x9F\x8E\xAE", 10, kTenPixels);
    EXPECT_EQ(emoji, (std::vector<std::string>{"\xF0\x9F\x8E\xAE", "\xF0\x9F\x8E\xAE"}));
}

TEST(X11MessageBoxGeometry, ButtonsGoRightAlignedAlongTheBottomInTheOrderGiven)
{
    const auto layout = LayoutMessageBox({"Hello", "World!"}, {"Yes", "No", "Cancel"}, kTenPixels, 10, 3);
    ASSERT_EQ(layout.buttons.size(), 3u);
    ASSERT_EQ(layout.lines.size(), 2u);
    for (std::size_t index = 0; index + 1 < layout.buttons.size(); ++index)
    {
        const X11MessageBoxRect& left = layout.buttons[index];
        const X11MessageBoxRect& right = layout.buttons[index + 1];
        EXPECT_LT(left.x + left.width, right.x) << "buttons overlap or touch";
        EXPECT_EQ(left.y, right.y);
    }
    const X11MessageBoxRect& last = layout.buttons.back();
    EXPECT_EQ(layout.width - (last.x + last.width), layout.height - (last.y + last.height))
        << "the right margin and the bottom margin should be the same padding";
    EXPECT_GT(layout.buttons.front().x, layout.stripe.width);
    // The text sits beside the band, one line under another, above the buttons.
    EXPECT_GT(layout.lines[0].first, layout.stripe.x + layout.stripe.width);
    EXPECT_EQ(layout.lines[0].first, layout.lines[1].first);
    EXPECT_GE(layout.lines[1].second - layout.lines[0].second, 13);
    EXPECT_LT(layout.lines[1].second + 3, layout.buttons.front().y);
    // The band runs the whole height of the left edge.
    EXPECT_EQ(layout.stripe.x, 0);
    EXPECT_EQ(layout.stripe.y, 0);
    EXPECT_EQ(layout.stripe.height, layout.height);
}

TEST(X11MessageBoxGeometry, AButtonIsAsWideAsItsLabelNeedsAndNoNarrowerThanTheOthersMinimum)
{
    const auto layout = LayoutMessageBox({"x"}, {"OK", "A much longer label"}, kTenPixels, 10, 3);
    ASSERT_EQ(layout.buttons.size(), 2u);
    EXPECT_GT(layout.buttons[1].width, 190);
    EXPECT_GT(layout.buttons[1].width, layout.buttons[0].width);
    const auto tiny = LayoutMessageBox({"x"}, {"A", "B"}, kTenPixels, 10, 3);
    EXPECT_EQ(tiny.buttons[0].width, tiny.buttons[1].width) << "short labels share the minimum";
    EXPECT_GE(tiny.buttons[0].width, 60);
}

TEST(X11MessageBoxGeometry, TheWindowGrowsToTheWidestLineAndTheButtonRow)
{
    const auto wide = LayoutMessageBox({std::string(50, 'w')}, {"OK"}, kTenPixels, 10, 3);
    EXPECT_GE(wide.width, wide.lines[0].first + 500);
    const auto buttons = LayoutMessageBox({"x"}, {"One", "Two", "Three", "Four", "Five", "Six"}, kTenPixels, 10, 3);
    EXPECT_GE(buttons.buttons.front().x, buttons.stripe.width);
    EXPECT_LE(buttons.buttons.back().x + buttons.buttons.back().width, buttons.width);
    const auto taller = LayoutMessageBox({"a", "b", "c", "d"}, {"OK"}, kTenPixels, 10, 3);
    const auto shorter = LayoutMessageBox({"a"}, {"OK"}, kTenPixels, 10, 3);
    EXPECT_GT(taller.height, shorter.height);
}

TEST(X11MessageBoxGeometry, APointFindsTheButtonUnderItAndNothingElse)
{
    const auto layout = LayoutMessageBox({"Hello"}, {"Yes", "No"}, kTenPixels, 10, 3);
    for (int index = 0; index < 2; ++index)
    {
        const X11MessageBoxRect& button = layout.buttons[static_cast<std::size_t>(index)];
        EXPECT_EQ(MessageBoxButtonAt(layout, button.x + button.width / 2, button.y + button.height / 2), index);
        EXPECT_EQ(MessageBoxButtonAt(layout, button.x, button.y), index);
        EXPECT_EQ(MessageBoxButtonAt(layout, button.x + button.width, button.y), std::nullopt) << "right edge";
        EXPECT_EQ(MessageBoxButtonAt(layout, button.x, button.y + button.height), std::nullopt) << "bottom edge";
    }
    const int gap = layout.buttons[0].x + layout.buttons[0].width + 1;
    EXPECT_EQ(MessageBoxButtonAt(layout, gap, layout.buttons[0].y + 2), std::nullopt);
    EXPECT_EQ(MessageBoxButtonAt(layout, 2, 2), std::nullopt);
}

// --- real boxes, on the launcher's server --------------------------------------------------------

struct Rgb
{
    int red = 0;
    int green = 0;
    int blue = 0;
};

bool Near(const Rgb& colour, const int red, const int green, const int blue)
{
    return std::abs(colour.red - red) <= 8 && std::abs(colour.green - green) <= 8 && std::abs(colour.blue - blue) <= 8;
}

class X11MessageBoxLive : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const char* privateServer = std::getenv("CNA_X11_PRIVATE_TEST_SERVER");
        if (privateServer == nullptr || std::string(privateServer) != "1")
        {
            GTEST_SKIP() << "shows windows and answers them; needs tools/platform/x11_test_server.sh";
        }
        observer_ = XOpenDisplay(nullptr);
        ASSERT_NE(observer_, nullptr);
        root_ = DefaultRootWindow(observer_);
        platform_ = PlatformFactory::Create("X11");
        platform_->AcquireSubsystem(PlatformSubsystem::Video);
        dialogs_ = platform_->GetDialogs();
    }

    void TearDown() override
    {
        DismissAndJoin();
        window_.reset();
        if (platform_ != nullptr)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
        platform_.reset();
        if (observer_ != nullptr)
        {
            XCloseDisplay(observer_);
        }
    }

    void MakeParent(const int x, const int y, const int width, const int height)
    {
        WindowDescription description;
        description.title = "CNA message box parent";
        description.width = width;
        description.height = height;
        description.centered = false;
        description.x = x;
        description.y = y;
        window_ = platform_->CreateWindow(description);
        window_->Sync();
    }

    /// A box a failed test left up is closed, so that its thread can end.
    void DismissAndJoin()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (runner_.joinable() && !done_ && std::chrono::steady_clock::now() < deadline)
        {
            if (const ::Window box = FindBox(title_, std::chrono::milliseconds(50)); box != kNone)
            {
                Close(box);
            }
        }
        if (runner_.joinable())
        {
            runner_.join();
        }
    }

    /// Runs a box on a thread of its own, as a game's main thread would block on it.
    void Run(std::string title, std::function<int()> call)
    {
        DismissAndJoin();
        title_ = std::move(title);
        done_ = false;
        error_ = nullptr;
        result_ = -2;
        runner_ = std::thread([this, call = std::move(call)]() {
            try
            {
                result_ = call();
            }
            catch (...)
            {
                error_ = std::current_exception();
            }
            done_ = true;
        });
    }

    void Show(const MessageBoxSeverity severity, const std::string& title, std::string message,
              std::vector<std::string> buttons, const bool withParent = true)
    {
        IPlatformWindow* parent = withParent ? window_.get() : nullptr;
        Run(title, [this, severity, title, message = std::move(message), buttons = std::move(buttons), parent]() {
            return dialogs_->ShowMessageBoxWithButtons(severity, title, message, buttons, parent);
        });
    }

    [[nodiscard]] std::optional<std::string> NetWmName(const ::Window window) const
    {
        const Atom property = XInternAtom(observer_, "_NET_WM_NAME", kXFalse);
        const Atom utf8 = XInternAtom(observer_, "UTF8_STRING", kXFalse);
        Atom type = kNone;
        int format = 0;
        unsigned long count = 0;
        unsigned long remaining = 0;
        unsigned char* data = nullptr;
        std::optional<std::string> name;
        if (XGetWindowProperty(observer_, window, property, 0, 1024, kXFalse, utf8, &type, &format, &count,
                               &remaining, &data) == Success &&
            data != nullptr && type == utf8)
        {
            name = std::string(reinterpret_cast<char*>(data), count);
        }
        if (data != nullptr) { XFree(data); }
        return name;
    }

    [[nodiscard]] ::Window Search(const ::Window window, const std::string& title) const
    {
        if (NetWmName(window) == title)
        {
            XWindowAttributes attributes{};
            if (XGetWindowAttributes(observer_, window, &attributes) != 0 && attributes.map_state == IsViewable)
            {
                return window;
            }
        }
        ::Window rootReturn = kNone;
        ::Window parentReturn = kNone;
        ::Window* children = nullptr;
        unsigned int count = 0;
        ::Window found = kNone;
        if (XQueryTree(observer_, window, &rootReturn, &parentReturn, &children, &count) != 0)
        {
            for (unsigned int index = 0; index < count && found == kNone; ++index)
            {
                found = Search(children[index], title);
            }
            if (children != nullptr) { XFree(children); }
        }
        return found;
    }

    /// The box with a title, once it is on the screen.
    [[nodiscard]] ::Window FindBox(const std::string& title,
                                   const std::chrono::milliseconds budget = std::chrono::milliseconds(5000)) const
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        do
        {
            if (const ::Window box = Search(root_, title); box != kNone)
            {
                return box;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);
        return kNone;
    }

    [[nodiscard]] Rgb Colour(const unsigned long pixel) const
    {
        XColor colour{};
        colour.pixel = pixel;
        XQueryColor(observer_, DefaultColormap(observer_, DefaultScreen(observer_)), &colour);
        return {colour.red >> 8, colour.green >> 8, colour.blue >> 8};
    }

    /// What the box shows, as colours, row by row.
    [[nodiscard]] std::vector<std::vector<Rgb>> Capture(const ::Window box, int& width, int& height) const
    {
        XWindowAttributes attributes{};
        XGetWindowAttributes(observer_, box, &attributes);
        width = attributes.width;
        height = attributes.height;
        std::vector<std::vector<Rgb>> rows;
        XImage* image = XGetImage(observer_, box, 0, 0, static_cast<unsigned>(width), static_cast<unsigned>(height),
                                  AllPlanes, ZPixmap);
        if (image == nullptr)
        {
            return rows;
        }
        const Visual* visual = attributes.visual;
        const auto channel = [](const unsigned long pixel, unsigned long mask) {
            if (mask == 0) { return 0; }
            int shift = 0;
            while ((mask & 1u) == 0) { mask >>= 1; ++shift; }
            return static_cast<int>(((pixel >> shift) & mask) * 255 / mask);
        };
        const bool trueColour = visual->c_class == TrueColor;
        for (int y = 0; y < height; ++y)
        {
            std::vector<Rgb> row;
            row.reserve(static_cast<std::size_t>(width));
            for (int x = 0; x < width; ++x)
            {
                const unsigned long pixel = XGetPixel(image, x, y);
                row.push_back(trueColour ? Rgb{channel(pixel, visual->red_mask), channel(pixel, visual->green_mask),
                                               channel(pixel, visual->blue_mask)}
                                         : Colour(pixel));
            }
            rows.push_back(std::move(row));
        }
        XDestroyImage(image);
        return rows;
    }

    /// Waits for the box to be drawn: its band's colour down the left edge.
    bool WaitForBand(const ::Window box, const int red, const int green, const int blue)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline)
        {
            int width = 0;
            int height = 0;
            const auto rows = Capture(box, width, height);
            if (!rows.empty() && Near(rows[static_cast<std::size_t>(height / 2)][2], red, green, blue))
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    void Key(const ::Window box, const KeySym key, const unsigned int state = 0) const
    {
        XEvent event{};
        event.xkey.type = KeyPress;
        event.xkey.display = observer_;
        event.xkey.window = box;
        event.xkey.root = root_;
        event.xkey.subwindow = kNone;
        event.xkey.time = kCurrentTime;
        event.xkey.same_screen = 1;
        event.xkey.state = state;
        event.xkey.keycode = XKeysymToKeycode(observer_, key);
        XSendEvent(observer_, box, kXFalse, KeyPressMask, &event);
        XSync(observer_, kXFalse);
    }

    void Button(const ::Window box, const int type, const int x, const int y) const
    {
        XEvent event{};
        event.xbutton.type = type;
        event.xbutton.display = observer_;
        event.xbutton.window = box;
        event.xbutton.root = root_;
        event.xbutton.subwindow = kNone;
        event.xbutton.time = kCurrentTime;
        event.xbutton.x = x;
        event.xbutton.y = y;
        event.xbutton.same_screen = 1;
        event.xbutton.button = Button1;
        XSendEvent(observer_, box, kXFalse, type == ButtonPress ? ButtonPressMask : ButtonReleaseMask, &event);
        XSync(observer_, kXFalse);
    }

    /// The window manager's close button: WM_PROTOCOLS / WM_DELETE_WINDOW.
    void Close(const ::Window box) const
    {
        XEvent event{};
        event.xclient.type = ClientMessage;
        event.xclient.window = box;
        event.xclient.message_type = XInternAtom(observer_, "WM_PROTOCOLS", kXFalse);
        event.xclient.format = 32;
        event.xclient.data.l[0] = static_cast<long>(XInternAtom(observer_, "WM_DELETE_WINDOW", kXFalse));
        event.xclient.data.l[1] = static_cast<long>(kCurrentTime);
        XSendEvent(observer_, box, kXFalse, NoEventMask, &event);
        XSync(observer_, kXFalse);
    }

    /// The box's answer, once its thread has one.
    std::optional<int> Answer(const std::chrono::milliseconds budget = std::chrono::milliseconds(5000))
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        while (!done_ && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!done_)
        {
            return std::nullopt;
        }
        runner_.join();
        if (error_)
        {
            std::rethrow_exception(error_);
        }
        return result_;
    }

    /// Each button's left and right edge, read from a row through the button faces.
    [[nodiscard]] std::vector<std::pair<int, int>> ButtonColumns(const ::Window box) const
    {
        int width = 0;
        int height = 0;
        const auto rows = Capture(box, width, height);
        // The focused button's border is the accent; its first face row is three rows under it.
        int top = -1;
        for (int y = height - 1; y >= 0 && top < 0; --y)
        {
            for (int x = 10; x < width; ++x)
            {
                const Rgb& pixel = rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
                if (Near(pixel, 0x30, 0x70, 0xC0))
                {
                    // Keep climbing to the border's top row.
                    int up = y;
                    while (up > 0 && Near(rows[static_cast<std::size_t>(up - 1)][static_cast<std::size_t>(x)], 0x30, 0x70, 0xC0))
                    {
                        --up;
                    }
                    top = up;
                    break;
                }
            }
        }
        std::vector<std::pair<int, int>> columns;
        if (top < 0)
        {
            return columns;
        }
        const std::vector<Rgb>& row = rows[static_cast<std::size_t>(top + 3)];
        int start = -1;
        for (int x = 0; x <= width; ++x)
        {
            const bool face = x < width && Near(row[static_cast<std::size_t>(x)], 0xE2, 0xE2, 0xE2);
            if (face && start < 0) { start = x; }
            if (!face && start >= 0) { columns.emplace_back(start, x - 1); start = -1; }
        }
        buttonRowY_ = top + 6;
        return columns;
    }

    Display* observer_ = nullptr;
    ::Window root_ = kNone;
    std::unique_ptr<IPlatform> platform_;
    IPlatformDialogs* dialogs_ = nullptr;
    std::unique_ptr<IPlatformWindow> window_;
    std::thread runner_;
    std::string title_;
    std::atomic<bool> done_{false};
    int result_ = -2;
    std::exception_ptr error_;
    mutable int buttonRowY_ = 0;
};

TEST_F(X11MessageBoxLive, TheCapabilityAndTheServiceComeTogether)
{
    EXPECT_TRUE(platform_->GetCapabilities().messageBox);
    EXPECT_NE(dialogs_, nullptr);
}

TEST_F(X11MessageBoxLive, ABoxIsATransientDialogCentredOnItsParent)
{
    MakeParent(200, 150, 600, 400);
    const std::string title = "Příliš žluťoučký kůň";
    Show(MessageBoxSeverity::Information, title, "A message.", {"OK"});
    const ::Window box = FindBox(title);
    ASSERT_NE(box, kNone) << "no box titled in UTF-8 appeared";

    ::Window transientFor = kNone;
    ASSERT_NE(XGetTransientForHint(observer_, box, &transientFor), 0);
    EXPECT_EQ(transientFor, static_cast<::Window>(window_->GetNativeHandle().windowId));

    Atom type = kNone;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char* data = nullptr;
    ASSERT_EQ(XGetWindowProperty(observer_, box, XInternAtom(observer_, "_NET_WM_WINDOW_TYPE", kXFalse), 0, 16,
                                 kXFalse, XA_ATOM, &type, &format, &count, &remaining, &data),
              Success);
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(count, 1u);
    EXPECT_EQ(reinterpret_cast<Atom*>(data)[0], XInternAtom(observer_, "_NET_WM_WINDOW_TYPE_DIALOG", kXFalse));
    XFree(data);

    Atom* protocols = nullptr;
    int protocolCount = 0;
    ASSERT_NE(XGetWMProtocols(observer_, box, &protocols, &protocolCount), 0);
    EXPECT_TRUE(std::find(protocols, protocols + protocolCount, XInternAtom(observer_, "WM_DELETE_WINDOW", kXFalse)) !=
                protocols + protocolCount);
    XFree(protocols);

    // Modal to the parent, whose game is blocked on it.
    data = nullptr;
    ASSERT_EQ(XGetWindowProperty(observer_, box, XInternAtom(observer_, "_NET_WM_STATE", kXFalse), 0, 16, kXFalse,
                                 XA_ATOM, &type, &format, &count, &remaining, &data),
              Success);
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(count, 1u);
    EXPECT_EQ(reinterpret_cast<Atom*>(data)[0], XInternAtom(observer_, "_NET_WM_STATE_MODAL", kXFalse));
    XFree(data);

    // It wants the keyboard.
    XWMHints* wmHints = XGetWMHints(observer_, box);
    ASSERT_NE(wmHints, nullptr);
    EXPECT_NE(wmHints->flags & InputHint, 0);
    EXPECT_NE(wmHints->input, 0);
    XFree(wmHints);

    XClassHint classHint{};
    ASSERT_NE(XGetClassHint(observer_, box, &classHint), 0);
    EXPECT_STREQ(classHint.res_class, "CNA");
    XFree(classHint.res_name);
    XFree(classHint.res_class);

    // Fixed size: the minimum and the maximum are the size.
    XSizeHints hints{};
    long supplied = 0;
    ASSERT_NE(XGetWMNormalHints(observer_, box, &hints, &supplied), 0);
    XWindowAttributes attributes{};
    ASSERT_NE(XGetWindowAttributes(observer_, box, &attributes), 0);
    EXPECT_EQ(hints.min_width, attributes.width);
    EXPECT_EQ(hints.max_width, attributes.width);
    EXPECT_EQ(hints.min_height, attributes.height);
    EXPECT_EQ(hints.max_height, attributes.height);

    int x = 0;
    int y = 0;
    ::Window child = kNone;
    ASSERT_NE(XTranslateCoordinates(observer_, box, root_, 0, 0, &x, &y, &child), 0);
    const WindowBounds parent = window_->GetClientBounds();
    EXPECT_NEAR(x + attributes.width / 2.0, parent.x + parent.width / 2.0, 1.0);
    EXPECT_NEAR(y + attributes.height / 2.0, parent.y + parent.height / 2.0, 1.0);

    Key(box, XK_Return);
    EXPECT_EQ(Answer(), 0);
}

TEST_F(X11MessageBoxLive, WithoutAParentItIsCentredAcrossTheScreenAndTransientForNothing)
{
    Show(MessageBoxSeverity::Information, "CNA box without parent", "Nobody's.", {"OK"}, false);
    const ::Window box = FindBox("CNA box without parent");
    ASSERT_NE(box, kNone);
    ::Window transientFor = kNone;
    EXPECT_EQ(XGetTransientForHint(observer_, box, &transientFor), 0);
    // Nothing to be modal to.
    Atom type = kNone;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char* data = nullptr;
    XGetWindowProperty(observer_, box, XInternAtom(observer_, "_NET_WM_STATE", kXFalse), 0, 16, kXFalse, XA_ATOM,
                       &type, &format, &count, &remaining, &data);
    EXPECT_EQ(count, 0u);
    if (data != nullptr) { XFree(data); }
    XWindowAttributes attributes{};
    ASSERT_NE(XGetWindowAttributes(observer_, box, &attributes), 0);
    int x = 0;
    int y = 0;
    ::Window child = kNone;
    ASSERT_NE(XTranslateCoordinates(observer_, box, root_, 0, 0, &x, &y, &child), 0);
    const int screen = DefaultScreen(observer_);
    EXPECT_NEAR(x + attributes.width / 2.0, DisplayWidth(observer_, screen) / 2.0, 1.0);
    EXPECT_LT(y, (DisplayHeight(observer_, screen) - attributes.height) / 2) << "a third of the way down";
    Key(box, XK_Escape);
    EXPECT_EQ(Answer(), -1);
}

TEST_F(X11MessageBoxLive, AParentThatHasGoneIsNoParent)
{
    MakeParent(50, 50, 200, 150);
    IPlatformWindow* parent = window_.get();
    const auto gone = static_cast<::Window>(window_->GetNativeHandle().windowId);
    // The window is destroyed behind the platform's back, as a crashed toolkit or a killed
    // client would leave it; the box must not be transient for an XID that is free for reuse.
    XDestroyWindow(observer_, gone);
    XSync(observer_, kXFalse);
    Run("CNA box orphan", [this, parent]() {
        return dialogs_->ShowMessageBoxWithButtons(MessageBoxSeverity::Warning, "CNA box orphan", "Its parent is gone.",
                                                   {"OK"}, parent);
    });
    const ::Window box = FindBox("CNA box orphan");
    ASSERT_NE(box, kNone);
    ::Window transientFor = kNone;
    EXPECT_EQ(XGetTransientForHint(observer_, box, &transientFor), 0);
    Key(box, XK_Return);
    EXPECT_EQ(Answer(), 0);
}

TEST_F(X11MessageBoxLive, AParentAtTheScreensCornerDoesNotPushTheBoxOffIt)
{
    MakeParent(0, 0, 120, 90);
    Show(MessageBoxSeverity::Information, "CNA box in the corner", "Stays on the screen however small its parent is.",
         {"OK"});
    const ::Window box = FindBox("CNA box in the corner");
    ASSERT_NE(box, kNone);
    int x = 0;
    int y = 0;
    ::Window child = kNone;
    ASSERT_NE(XTranslateCoordinates(observer_, box, root_, 0, 0, &x, &y, &child), 0);
    EXPECT_GE(x, 0);
    EXPECT_GE(y, 0);
    Key(box, XK_Return);
    EXPECT_EQ(Answer(), 0);
}

TEST_F(X11MessageBoxLive, EachSeverityHasItsBandAndTheTextIsDrawn)
{
    struct Case
    {
        MessageBoxSeverity severity;
        int red;
        int green;
        int blue;
    };
    for (const Case& item : {Case{MessageBoxSeverity::Information, 0x30, 0x70, 0xC0},
                             Case{MessageBoxSeverity::Warning, 0xD0, 0x90, 0x00},
                             Case{MessageBoxSeverity::Error, 0xC0, 0x30, 0x30}})
    {
        const std::string title = "CNA box severity " + std::to_string(static_cast<int>(item.severity));
        Show(item.severity, title, "Something happened.\nP\u0159\u00EDli\u0161 \u017Elu\u0165ou\u010Dk\u00FD k\u016F\u0148.", {"OK"},
             false);
        const ::Window box = FindBox(title);
        ASSERT_NE(box, kNone) << title;
        ASSERT_TRUE(WaitForBand(box, item.red, item.green, item.blue)) << title;

        int width = 0;
        int height = 0;
        const auto rows = Capture(box, width, height);
        // The text: dark pixels on the background, right of the band and above the buttons.
        int textPixels = 0;
        int background = 0;
        for (int y = 0; y < height / 2; ++y)
        {
            for (int x = 10; x < width; ++x)
            {
                const Rgb& pixel = rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
                textPixels += Near(pixel, 0x20, 0x20, 0x20) ? 1 : 0;
                background += Near(pixel, 0xF2, 0xF2, 0xF2) ? 1 : 0;
            }
        }
        EXPECT_GT(textPixels, 40) << title << ": no text was drawn";
        EXPECT_GT(background, (width - 10) * (height / 2) / 2) << title;
        EXPECT_FALSE(ButtonColumns(box).empty()) << title << ": no focused button was drawn";
        Key(box, XK_Return);
        EXPECT_EQ(Answer(), 0) << title;
    }
}

TEST_F(X11MessageBoxLive, TabAndTheArrowsMoveTheFocusAndReturnChoosesTheFocusedButton)
{
    Show(MessageBoxSeverity::Warning, "CNA box focus", "Choose.", {"Yes", "No", "Cancel"}, false);
    ::Window box = FindBox("CNA box focus");
    ASSERT_NE(box, kNone);
    ASSERT_TRUE(WaitForBand(box, 0xD0, 0x90, 0x00));
    const std::vector<std::pair<int, int>> columns = ButtonColumns(box);
    ASSERT_EQ(columns.size(), 3u);

    // The focus ring follows Tab: it is drawn round the second button, not the first.
    Key(box, XK_Tab);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool moved = false;
    while (!moved && std::chrono::steady_clock::now() < deadline)
    {
        int width = 0;
        int height = 0;
        const auto rows = Capture(box, width, height);
        const auto& row = rows[static_cast<std::size_t>(buttonRowY_)];
        moved = Near(row[static_cast<std::size_t>(columns[1].first - 1)], 0x30, 0x70, 0xC0) &&
                !Near(row[static_cast<std::size_t>(columns[0].first - 1)], 0x30, 0x70, 0xC0);
    }
    EXPECT_TRUE(moved) << "the focus ring did not move to the second button";
    Key(box, XK_Return);
    EXPECT_EQ(Answer(), 1);

    // Shift+Tab from the first button wraps to the last.
    Show(MessageBoxSeverity::Warning, "CNA box back", "Choose.", {"Yes", "No", "Cancel"}, false);
    box = FindBox("CNA box back");
    ASSERT_NE(box, kNone);
    Key(box, XK_Tab, ShiftMask);
    Key(box, XK_space);
    EXPECT_EQ(Answer(), 2);

    // Right, Right, Left: the second.
    Show(MessageBoxSeverity::Warning, "CNA box arrows", "Choose.", {"Yes", "No", "Cancel"}, false);
    box = FindBox("CNA box arrows");
    ASSERT_NE(box, kNone);
    Key(box, XK_Right);
    Key(box, XK_Right);
    Key(box, XK_Left);
    Key(box, XK_KP_Enter);
    EXPECT_EQ(Answer(), 1);
}

TEST_F(X11MessageBoxLive, EscapeAndTheWindowManagersCloseAreNoChoice)
{
    Show(MessageBoxSeverity::Error, "CNA box escape", "Dismiss me.", {"Yes", "No"}, false);
    ::Window box = FindBox("CNA box escape");
    ASSERT_NE(box, kNone);
    Key(box, XK_Escape);
    EXPECT_EQ(Answer(), -1);
    EXPECT_EQ(FindBox("CNA box escape", std::chrono::milliseconds(0)), kNone) << "the box is gone";

    Show(MessageBoxSeverity::Error, "CNA box close", "Close me.", {"Yes", "No"}, false);
    box = FindBox("CNA box close");
    ASSERT_NE(box, kNone);
    // A client message that merely carries WM_DELETE_WINDOW's atom is not the protocol.
    XEvent other{};
    other.xclient.type = ClientMessage;
    other.xclient.window = box;
    other.xclient.message_type = XInternAtom(observer_, "CNA_NOT_A_PROTOCOL", kXFalse);
    other.xclient.format = 32;
    other.xclient.data.l[0] = static_cast<long>(XInternAtom(observer_, "WM_DELETE_WINDOW", kXFalse));
    XSendEvent(observer_, box, kXFalse, NoEventMask, &other);
    XSync(observer_, kXFalse);
    EXPECT_EQ(Answer(std::chrono::milliseconds(300)), std::nullopt);
    Close(box);
    EXPECT_EQ(Answer(), -1);
}

TEST_F(X11MessageBoxLive, AClickChoosesTheButtonItLandsOn)
{
    Show(MessageBoxSeverity::Warning, "CNA box click", "Click one.", {"Yes", "No", "Cancel"}, false);
    const ::Window box = FindBox("CNA box click");
    ASSERT_NE(box, kNone);
    ASSERT_TRUE(WaitForBand(box, 0xD0, 0x90, 0x00));
    const std::vector<std::pair<int, int>> columns = ButtonColumns(box);
    ASSERT_EQ(columns.size(), 3u);
    const int x = (columns[2].first + columns[2].second) / 2;
    Button(box, ButtonPress, x, buttonRowY_);
    Button(box, ButtonRelease, x, buttonRowY_);
    EXPECT_EQ(Answer(), 2);
}

TEST_F(X11MessageBoxLive, APressOnOneButtonReleasedOnAnotherChoosesNothing)
{
    Show(MessageBoxSeverity::Warning, "CNA box drag", "Changed my mind.", {"Yes", "No"}, false);
    const ::Window box = FindBox("CNA box drag");
    ASSERT_NE(box, kNone);
    ASSERT_TRUE(WaitForBand(box, 0xD0, 0x90, 0x00));
    const std::vector<std::pair<int, int>> columns = ButtonColumns(box);
    ASSERT_EQ(columns.size(), 2u);
    Button(box, ButtonPress, (columns[0].first + columns[0].second) / 2, buttonRowY_);
    Button(box, ButtonRelease, (columns[1].first + columns[1].second) / 2, buttonRowY_);
    EXPECT_EQ(Answer(std::chrono::milliseconds(300)), std::nullopt) << "the box answered a drag";
    // Nor does a click on the background.
    Button(box, ButtonPress, 30, 12);
    Button(box, ButtonRelease, 30, 12);
    EXPECT_EQ(Answer(std::chrono::milliseconds(300)), std::nullopt);
    Key(box, XK_Escape);
    EXPECT_EQ(Answer(), -1);
}

TEST_F(X11MessageBoxLive, TheOkBoxReturnsWhenItIsClosed)
{
    Run("CNA box ok", [this]() {
        dialogs_->ShowMessageBox(MessageBoxSeverity::Information, "CNA box ok", "Just so you know.", nullptr);
        return 7;
    });
    const ::Window box = FindBox("CNA box ok");
    ASSERT_NE(box, kNone);
    ASSERT_TRUE(WaitForBand(box, 0x30, 0x70, 0xC0));
    ASSERT_EQ(ButtonColumns(box).size(), 1u) << "one OK button";
    Key(box, XK_Return);
    EXPECT_EQ(Answer(), 7);
}

TEST_F(X11MessageBoxLive, TheGamesEventQueueIsLeftAloneWhileABoxIsUp)
{
    MakeParent(100, 100, 320, 240);
    std::vector<PlatformEvent> batch;
    platform_->PollEvents(batch);
    Show(MessageBoxSeverity::Information, "CNA box own queue", "Mine.", {"OK"});
    const ::Window box = FindBox("CNA box own queue");
    ASSERT_NE(box, kNone);
    // The box uses a connection of its own; the platform's is polled while it is up.
    Key(box, XK_Tab);
    Key(box, XK_a);
    std::vector<PlatformEvent> seen;
    for (int pass = 0; pass < 10; ++pass)
    {
        platform_->PollEvents(batch);
        seen.insert(seen.end(), batch.begin(), batch.end());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(std::none_of(seen.begin(), seen.end(), [](const PlatformEvent& event) {
        return std::holds_alternative<KeyEvent>(event) || std::holds_alternative<TextInputEvent>(event);
    })) << "the box's keys reached the game";
    Key(box, XK_Return);
    EXPECT_EQ(Answer(), 0);
}

TEST_F(X11MessageBoxLive, ABoxWithoutButtonsIsRefused)
{
    EXPECT_THROW((void) dialogs_->ShowMessageBoxWithButtons(MessageBoxSeverity::Information, "CNA box none", "No way out.",
                                                           {}, nullptr),
                 PlatformException);
}

TEST_F(X11MessageBoxLive, WithoutAPortalFileDialogsRefuseNamingTheirCapability)
{
    // The binary and the launcher give this test no session bus, so no portal (X11-0169). Were
    // one found anyway, nothing below may run: it would open a real file chooser.
    ASSERT_FALSE(platform_->GetCapabilities().nativeFileDialog) << "a desktop portal was reached from a test";
    const auto names = [](const std::function<void()>& call) {
        try
        {
            call();
        }
        catch (const PlatformNotSupportedException& refusal)
        {
            return refusal.GetCapability() == PlatformCapability::NativeFileDialog;
        }
        return false;
    };
    EXPECT_TRUE(names([&]() { dialogs_->ShowOpenFileDialog([](const std::vector<std::string>&) {}, {}, "", false, nullptr); }));
    EXPECT_TRUE(names([&]() { dialogs_->ShowSaveFileDialog([](const std::vector<std::string>&) {}, {}, "", nullptr); }));
    EXPECT_TRUE(names([&]() { dialogs_->ShowOpenFolderDialog([](const std::vector<std::string>&) {}, "", false, nullptr); }));
}

} // namespace
