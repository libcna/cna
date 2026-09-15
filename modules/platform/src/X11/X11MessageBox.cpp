// SPDX-License-Identifier: MS-PL

#include "X11MessageBox.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "X11Clipboard.hpp"
#include "X11CoreFont.hpp"
#include "../Freedesktop/DesktopPortal.hpp"

#include <cstdio>
#include "X11Display.hpp"
#include "X11Error.hpp"
#include "X11Window.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

namespace CNA::Platform::X11 {

    namespace {

        constexpr int kPadding = 16;
        constexpr int kStripeWidth = 6;
        constexpr int kLineGap = 3;
        constexpr int kButtonGap = 8;
        constexpr int kButtonPadX = 16;
        constexpr int kButtonPadY = 6;
        constexpr int kMinimumButtonWidth = 80;
        constexpr int kMaximumTextWidth = 520;
        constexpr int kMinimumWidth = 280;

        /// The length of the UTF-8 sequence a lead byte starts; 1 for anything else.
        std::size_t SequenceLength(const unsigned char lead)
        {
            if ((lead & 0xE0u) == 0xC0u) { return 2; }
            if ((lead & 0xF0u) == 0xE0u) { return 3; }
            if ((lead & 0xF8u) == 0xF0u) { return 4; }
            return 1;
        }

        /// A connection of the box's own, registered with the error policy so a stray error on it
        /// is reported, never fatal, and closed however the box ends.
        struct BoxDisplay
        {
            explicit BoxDisplay(const std::string& name) : display(XOpenDisplay(name.empty() ? nullptr : name.c_str()))
            {
                if (display == nullptr)
                {
                    throw PlatformException("X11Dialogs", "cannot open the display '" + name + "'");
                }
                X11ErrorPolicy::Register(display);
            }
            ~BoxDisplay()
            {
                X11ErrorPolicy::Unregister(display);
                XCloseDisplay(display);
            }
            BoxDisplay(const BoxDisplay&) = delete;
            BoxDisplay& operator=(const BoxDisplay&) = delete;

            Display* display;
        };

        unsigned long Colour(Display* display, const int screen, const unsigned short red,
                             const unsigned short green, const unsigned short blue, const unsigned long fallback)
        {
            XColor colour{};
            colour.red = static_cast<unsigned short>(red * 257);
            colour.green = static_cast<unsigned short>(green * 257);
            colour.blue = static_cast<unsigned short>(blue * 257);
            colour.flags = DoRed | DoGreen | DoBlue;
            return XAllocColor(display, DefaultColormap(display, screen), &colour) != 0 ? colour.pixel : fallback;
        }

        int RunMessageBox(const std::string& displayName, ::Window parent, const MessageBoxSeverity severity,
                          const std::string& title, const std::string& message,
                          const std::vector<std::string>& buttons)
        {
            BoxDisplay connection(displayName);
            Display* display = connection.display;
            const int screen = DefaultScreen(display);
            const ::Window root = RootWindow(display, screen);

            // Where the parent is, asked on this connection: nothing of the platform's own is
            // used. A parent that has gone is no parent.
            std::optional<WindowBounds> parentBounds;
            if (parent != kNone)
            {
                X11ErrorTrap trap(display);
                XWindowAttributes attributes{};
                int parentX = 0;
                int parentY = 0;
                ::Window child = kNone;
                const bool found = XGetWindowAttributes(display, parent, &attributes) != 0 &&
                                   XTranslateCoordinates(display, parent, root, 0, 0, &parentX, &parentY, &child) != 0;
                if (trap.Sync() && found)
                {
                    parentBounds = WindowBounds{parentX, parentY, attributes.width, attributes.height};
                }
                else
                {
                    parent = kNone;
                }
            }
            const X11CoreFont font(display);
            const X11TextMeasure measure = [&font](const std::string_view text) { return font.Measure(text); };
            const std::vector<std::string> lines = WrapMessageBoxText(message, kMaximumTextWidth, measure);
            const X11MessageBoxLayout layout = LayoutMessageBox(lines, buttons, measure, font.Ascent(), font.Descent());

            int x = (DisplayWidth(display, screen) - layout.width) / 2;
            int y = (DisplayHeight(display, screen) - layout.height) / 3;
            if (parentBounds)
            {
                x = parentBounds->x + (parentBounds->width - layout.width) / 2;
                y = parentBounds->y + (parentBounds->height - layout.height) / 2;
            }
            // Never partly off the screen, whatever edge the parent sits at.
            x = std::clamp(x, 0, std::max(0, DisplayWidth(display, screen) - layout.width));
            y = std::clamp(y, 0, std::max(0, DisplayHeight(display, screen) - layout.height));

            const unsigned long black = BlackPixel(display, screen);
            const unsigned long background = Colour(display, screen, 0xF2, 0xF2, 0xF2, WhitePixel(display, screen));
            const unsigned long text = Colour(display, screen, 0x20, 0x20, 0x20, black);
            const unsigned long face = Colour(display, screen, 0xE2, 0xE2, 0xE2, WhitePixel(display, screen));
            const unsigned long border = Colour(display, screen, 0x80, 0x80, 0x80, black);
            const unsigned long accent = Colour(display, screen, 0x30, 0x70, 0xC0, black);
            const unsigned long stripe =
                severity == MessageBoxSeverity::Error     ? Colour(display, screen, 0xC0, 0x30, 0x30, black)
                : severity == MessageBoxSeverity::Warning ? Colour(display, screen, 0xD0, 0x90, 0x00, black)
                                                          : accent;

            const ::Window window = XCreateSimpleWindow(display, root, x, y, static_cast<unsigned>(layout.width),
                                                        static_cast<unsigned>(layout.height), 0, black, background);
            XSelectInput(display, window,
                         ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | StructureNotifyMask);

            // What the window manager needs to treat it as a dialog of the game's.
            // WM_NAME is Latin-1 by the ICCCM; _NET_WM_NAME carries the title as it is.
            const std::string legacyTitle = Utf8ToLatin1(title);
            XStoreName(display, window, legacyTitle.c_str());
            const Atom utf8 = XInternAtom(display, "UTF8_STRING", kXFalse);
            XChangeProperty(display, window, XInternAtom(display, "_NET_WM_NAME", kXFalse), utf8, 8,
                            PropModeReplace, reinterpret_cast<const unsigned char*>(title.data()),
                            static_cast<int>(title.size()));
            const Atom protocols = XInternAtom(display, "WM_PROTOCOLS", kXFalse);
            Atom deleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", kXFalse);
            XSetWMProtocols(display, window, &deleteWindow, 1);
            const Atom dialogType = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", kXFalse);
            XChangeProperty(display, window, XInternAtom(display, "_NET_WM_WINDOW_TYPE", kXFalse), XA_ATOM, 32,
                            PropModeReplace, reinterpret_cast<const unsigned char*>(&dialogType), 1);
            if (parent != kNone)
            {
                // Transient for the game's window, and modal to it: the game is blocked until the
                // box is answered, so its window should not look as if it could be used.
                XSetTransientForHint(display, window, parent);
                const Atom modal = XInternAtom(display, "_NET_WM_STATE_MODAL", kXFalse);
                XChangeProperty(display, window, XInternAtom(display, "_NET_WM_STATE", kXFalse), XA_ATOM, 32,
                                PropModeReplace, reinterpret_cast<const unsigned char*>(&modal), 1);
            }
            // The keyboard is wanted: a window manager gives the box the focus when it maps it.
            XWMHints* wmHints = XAllocWMHints();
            wmHints->flags = InputHint | StateHint;
            wmHints->input = 1;
            wmHints->initial_state = NormalState;
            XSetWMHints(display, window, wmHints);
            XFree(wmHints);
            XSizeHints* hints = XAllocSizeHints();
            hints->flags = PPosition | PMinSize | PMaxSize;
            hints->x = x;
            hints->y = y;
            hints->min_width = hints->max_width = layout.width;
            hints->min_height = hints->max_height = layout.height;
            XSetWMNormalHints(display, window, hints);
            XFree(hints);
            XClassHint* classHint = XAllocClassHint();
            std::string resourceName = "cna-message-box";
            std::string resourceClass = "CNA";
            classHint->res_name = resourceName.data();
            classHint->res_class = resourceClass.data();
            XSetClassHint(display, window, classHint);
            XFree(classHint);

            GC gc = XCreateGC(display, window, 0, nullptr);
            int focus = 0;
            std::optional<int> pressed;
            const int count = static_cast<int>(buttons.size());

            const auto draw = [&]() {
                XSetForeground(display, gc, background);
                XFillRectangle(display, window, gc, 0, 0, static_cast<unsigned>(layout.width),
                               static_cast<unsigned>(layout.height));
                XSetForeground(display, gc, stripe);
                XFillRectangle(display, window, gc, layout.stripe.x, layout.stripe.y,
                               static_cast<unsigned>(layout.stripe.width), static_cast<unsigned>(layout.stripe.height));
                XSetForeground(display, gc, text);
                for (std::size_t line = 0; line < lines.size(); ++line)
                {
                    font.Draw(window, gc, layout.lines[line].first, layout.lines[line].second, lines[line]);
                }
                for (int index = 0; index < count; ++index)
                {
                    const X11MessageBoxRect& button = layout.buttons[static_cast<std::size_t>(index)];
                    XSetForeground(display, gc, face);
                    XFillRectangle(display, window, gc, button.x, button.y, static_cast<unsigned>(button.width),
                                   static_cast<unsigned>(button.height));
                    XSetForeground(display, gc, index == focus ? accent : border);
                    XDrawRectangle(display, window, gc, button.x, button.y, static_cast<unsigned>(button.width - 1),
                                   static_cast<unsigned>(button.height - 1));
                    if (index == focus)
                    {
                        XDrawRectangle(display, window, gc, button.x + 1, button.y + 1,
                                       static_cast<unsigned>(button.width - 3), static_cast<unsigned>(button.height - 3));
                    }
                    const std::string& label = buttons[static_cast<std::size_t>(index)];
                    XSetForeground(display, gc, text);
                    font.Draw(window, gc, button.x + (button.width - font.Measure(label)) / 2,
                              button.y + kButtonPadY + font.Ascent(), label);
                }
                XFlush(display);
            };

            XMapRaised(display, window);
            XFlush(display);

            std::optional<int> result;
            while (!result)
            {
                XEvent event{};
                XNextEvent(display, &event);
                switch (event.type)
                {
                    case Expose:
                        if (event.xexpose.count == 0) { draw(); }
                        break;
                    case KeyPress:
                    {
                        const KeySym key = XLookupKeysym(&event.xkey, 0);
                        const bool back = (event.xkey.state & ShiftMask) != 0;
                        if (key == XK_Tab || key == XK_ISO_Left_Tab)
                        {
                            focus = (focus + (back || key == XK_ISO_Left_Tab ? count - 1 : 1)) % count;
                            draw();
                        }
                        else if (key == XK_Left || key == XK_Up)
                        {
                            focus = (focus + count - 1) % count;
                            draw();
                        }
                        else if (key == XK_Right || key == XK_Down)
                        {
                            focus = (focus + 1) % count;
                            draw();
                        }
                        else if (key == XK_Return || key == XK_KP_Enter || key == XK_space)
                        {
                            result = focus;
                        }
                        else if (key == XK_Escape)
                        {
                            result = -1;
                        }
                        break;
                    }
                    case ButtonPress:
                        if (event.xbutton.button == Button1)
                        {
                            pressed = MessageBoxButtonAt(layout, event.xbutton.x, event.xbutton.y);
                        }
                        break;
                    case ButtonRelease:
                        if (event.xbutton.button == Button1)
                        {
                            const std::optional<int> released = MessageBoxButtonAt(layout, event.xbutton.x, event.xbutton.y);
                            if (pressed && released && *pressed == *released)
                            {
                                result = *released;
                            }
                            pressed.reset();
                        }
                        break;
                    case MappingNotify:
                        XRefreshKeyboardMapping(&event.xmapping);
                        break;
                    case ClientMessage:
                        if (event.xclient.message_type == protocols &&
                            static_cast<Atom>(event.xclient.data.l[0]) == deleteWindow)
                        {
                            result = -1;
                        }
                        break;
                    case DestroyNotify:
                        if (event.xdestroywindow.window == window)
                        {
                            XFreeGC(display, gc);
                            return -1;
                        }
                        break;
                    default:
                        break;
                }
            }
            XFreeGC(display, gc);
            XDestroyWindow(display, window);
            XSync(display, kXFalse);
            return *result;
        }

    } // namespace

    std::u16string DecodeMessageBoxText(const std::string_view utf8)
    {
        std::u16string out;
        std::size_t at = 0;
        while (at < utf8.size())
        {
            const auto lead = static_cast<unsigned char>(utf8[at]);
            if (lead < 0x80u)
            {
                out.push_back(static_cast<char16_t>(lead));
                ++at;
                continue;
            }
            std::size_t length = 0;
            char32_t code = 0;
            // The second byte's range, narrowed where the lead alone would allow an overlong
            // form, a surrogate or a code point beyond U+10FFFF.
            unsigned char low = 0x80u;
            unsigned char high = 0xBFu;
            if (lead >= 0xC2u && lead <= 0xDFu)
            {
                length = 2;
                code = lead & 0x1Fu;
            }
            else if (lead >= 0xE0u && lead <= 0xEFu)
            {
                length = 3;
                code = lead & 0x0Fu;
                low = lead == 0xE0u ? 0xA0u : low;
                high = lead == 0xEDu ? 0x9Fu : high;
            }
            else if (lead >= 0xF0u && lead <= 0xF4u)
            {
                length = 4;
                code = lead & 0x07u;
                low = lead == 0xF0u ? 0x90u : low;
                high = lead == 0xF4u ? 0x8Fu : high;
            }
            else
            {
                out.push_back(u'\uFFFD');
                ++at;
                continue;
            }
            std::size_t used = 1;
            for (; used < length && at + used < utf8.size(); ++used)
            {
                const auto next = static_cast<unsigned char>(utf8[at + used]);
                if (next < low || next > high)
                {
                    break;
                }
                low = 0x80u;
                high = 0xBFu;
                code = (code << 6) | (next & 0x3Fu);
            }
            if (used < length)
            {
                // One replacement for the whole malformed prefix, as the Unicode standard advises.
                out.push_back(u'\uFFFD');
                at += used;
                continue;
            }
            out.push_back(code > 0xFFFFu ? u'\uFFFD' : static_cast<char16_t>(code));
            at += length;
        }
        return out;
    }

    std::vector<std::string> WrapMessageBoxText(const std::string_view text, const int maxWidth,
                                                const X11TextMeasure& measure)
    {
        std::vector<std::string> lines;
        std::size_t start = 0;
        while (start <= text.size())
        {
            std::size_t end = text.find('\n', start);
            if (end == std::string_view::npos) { end = text.size(); }
            std::string_view paragraph = text.substr(start, end - start);
            if (!paragraph.empty() && paragraph.back() == '\r') { paragraph.remove_suffix(1); }
            start = end + 1;
            if (paragraph.empty())
            {
                lines.emplace_back();
                continue;
            }
            while (!paragraph.empty())
            {
                if (measure(paragraph) <= maxWidth)
                {
                    lines.emplace_back(paragraph);
                    break;
                }
                // The longest prefix that fits, whole UTF-8 sequences only.
                std::size_t fits = 0;
                for (std::size_t at = 0; at < paragraph.size();)
                {
                    const std::size_t next = std::min(paragraph.size(),
                                                      at + SequenceLength(static_cast<unsigned char>(paragraph[at])));
                    if (measure(paragraph.substr(0, next)) > maxWidth) { break; }
                    fits = next;
                    at = next;
                }
                if (fits == 0)
                {
                    fits = std::min(paragraph.size(), SequenceLength(static_cast<unsigned char>(paragraph[0])));
                }
                // Break at the last space within it, when there is one.
                std::size_t cut = fits;
                if (fits < paragraph.size())
                {
                    const std::size_t space = paragraph.substr(0, fits + 1).rfind(' ');
                    if (space != std::string_view::npos && space > 0)
                    {
                        cut = space;
                    }
                }
                lines.emplace_back(paragraph.substr(0, cut));
                paragraph.remove_prefix(cut);
                while (!paragraph.empty() && paragraph.front() == ' ')
                {
                    paragraph.remove_prefix(1);
                }
            }
        }
        if (lines.empty())
        {
            lines.emplace_back();
        }
        return lines;
    }

    X11MessageBoxLayout LayoutMessageBox(const std::vector<std::string>& lines, const std::vector<std::string>& buttons,
                                         const X11TextMeasure& measure, const int ascent, const int descent)
    {
        X11MessageBoxLayout layout;
        const int lineHeight = ascent + descent + kLineGap;
        int textWidth = 0;
        for (const std::string& line : lines)
        {
            textWidth = std::max(textWidth, measure(line));
        }
        const int buttonHeight = ascent + descent + 2 * kButtonPadY;
        std::vector<int> buttonWidths;
        int buttonsWidth = 0;
        for (const std::string& label : buttons)
        {
            const int width = std::max(kMinimumButtonWidth, measure(label) + 2 * kButtonPadX);
            buttonWidths.push_back(width);
            buttonsWidth += width;
        }
        if (!buttons.empty())
        {
            buttonsWidth += kButtonGap * static_cast<int>(buttons.size() - 1);
        }
        const int textLeft = kStripeWidth + kPadding;
        layout.width = std::max({kMinimumWidth, textLeft + textWidth + kPadding, buttonsWidth + 2 * kPadding + kStripeWidth});
        const int textHeight = lineHeight * static_cast<int>(lines.size());
        layout.height = kPadding + textHeight + kPadding + buttonHeight + kPadding;
        layout.stripe = {0, 0, kStripeWidth, layout.height};
        for (std::size_t line = 0; line < lines.size(); ++line)
        {
            layout.lines.emplace_back(textLeft, kPadding + ascent + lineHeight * static_cast<int>(line));
        }
        int x = layout.width - kPadding - buttonsWidth;
        const int y = layout.height - kPadding - buttonHeight;
        for (const int width : buttonWidths)
        {
            layout.buttons.push_back({x, y, width, buttonHeight});
            x += width + kButtonGap;
        }
        return layout;
    }

    std::optional<int> MessageBoxButtonAt(const X11MessageBoxLayout& layout, const int x, const int y)
    {
        for (std::size_t index = 0; index < layout.buttons.size(); ++index)
        {
            if (layout.buttons[index].Contains(x, y))
            {
                return static_cast<int>(index);
            }
        }
        return std::nullopt;
    }

    X11Dialogs::X11Dialogs(X11Connection& connection, Freedesktop::DesktopPortal* portal)
        : connection_(connection), portal_(portal)
    {
    }

    void X11Dialogs::ShowMessageBox(const MessageBoxSeverity severity, const std::string& title,
                                    const std::string& message, IPlatformWindow* parent)
    {
        (void) ShowMessageBoxWithButtons(severity, title, message, {"OK"}, parent);
    }

    int X11Dialogs::ShowMessageBoxWithButtons(const MessageBoxSeverity severity, const std::string& title,
                                              const std::string& message, const std::vector<std::string>& buttons,
                                              IPlatformWindow* parent)
    {
        if (buttons.empty())
        {
            throw PlatformException("X11Dialogs::ShowMessageBoxWithButtons", "a message box needs a button");
        }
        ::Window parentWindow = kNone;
        if (const auto* x11 = dynamic_cast<const X11Window*>(parent); x11 != nullptr)
        {
            parentWindow = x11->GetXWindow();
        }
        return RunMessageBox(connection_.GetDisplayName(), parentWindow, severity, title, message, buttons);
    }

    namespace {

        unsigned long ParentXid(IPlatformWindow* parent)
        {
            const auto* x11 = dynamic_cast<const X11Window*>(parent);
            return x11 != nullptr ? static_cast<unsigned long>(x11->GetXWindow()) : 0ul;
        }

    } // namespace

    std::string PortalParentWindow(const unsigned long xid)
    {
        if (xid == 0)
        {
            return {};
        }
        char text[32] = {};
        std::snprintf(text, sizeof(text), "x11:%lx", xid);
        return text;
    }

    void X11Dialogs::ShowOpenFileDialog(FileDialogCallback onResult, const std::vector<FileDialogFilter>& filters,
                                        const std::string& defaultLocation, const bool allowMultiple,
                                        IPlatformWindow* parent)
    {
#if defined(CNA_PLATFORM_HAVE_DBUS)
        if (portal_ != nullptr)
        {
            portal_->ShowFileDialog(Freedesktop::DesktopPortal::FileRequest::Open, std::move(onResult), filters,
                                    defaultLocation, allowMultiple, PortalParentWindow(ParentXid(parent)));
            return;
        }
#endif
        (void) onResult; (void) filters; (void) defaultLocation; (void) allowMultiple; (void) ParentXid(parent);
        throw PlatformNotSupportedException(PlatformCapability::NativeFileDialog, "X11 (no desktop portal)");
    }

    void X11Dialogs::ShowSaveFileDialog(FileDialogCallback onResult, const std::vector<FileDialogFilter>& filters,
                                        const std::string& defaultLocation, IPlatformWindow* parent)
    {
#if defined(CNA_PLATFORM_HAVE_DBUS)
        if (portal_ != nullptr)
        {
            portal_->ShowFileDialog(Freedesktop::DesktopPortal::FileRequest::Save, std::move(onResult), filters,
                                    defaultLocation, false, PortalParentWindow(ParentXid(parent)));
            return;
        }
#endif
        (void) onResult; (void) filters; (void) defaultLocation;
        throw PlatformNotSupportedException(PlatformCapability::NativeFileDialog, "X11 (no desktop portal)");
    }

    void X11Dialogs::ShowOpenFolderDialog(FileDialogCallback onResult, const std::string& defaultLocation,
                                          const bool allowMultiple, IPlatformWindow* parent)
    {
#if defined(CNA_PLATFORM_HAVE_DBUS)
        if (portal_ != nullptr)
        {
            portal_->ShowFileDialog(Freedesktop::DesktopPortal::FileRequest::OpenFolder, std::move(onResult), {},
                                    defaultLocation, allowMultiple, PortalParentWindow(ParentXid(parent)));
            return;
        }
#endif
        (void) onResult; (void) defaultLocation; (void) allowMultiple;
        throw PlatformNotSupportedException(PlatformCapability::NativeFileDialog, "X11 (no desktop portal)");
    }

} // namespace CNA::Platform::X11
