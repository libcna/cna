// SPDX-License-Identifier: MS-PL

#include "X11Tray.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "X11Error.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace CNA::Platform::X11 {

    namespace {

        constexpr int kMenuPadding = 4;
        constexpr int kCheckColumn = 20;
        constexpr int kTextPadding = 10;
        constexpr int kMinimumMenuWidth = 120;
        constexpr auto kTooltipDelay = std::chrono::milliseconds(600);
        /// SYSTEM_TRAY_REQUEST_DOCK, the protocol's first opcode.
        constexpr long kRequestDock = 0;
        /// _XEMBED_INFO's XEMBED_MAPPED: the tray maps the icon once it has embedded it.
        constexpr unsigned long kXEmbedMapped = 1;

        /// The first character of a text, as the icon's letter: the tooltip's, upper-cased.
        std::string IconLetter(const std::string& tooltip)
        {
            if (tooltip.empty())
            {
                return "C";
            }
            const auto lead = static_cast<unsigned char>(tooltip[0]);
            std::size_t length = 1;
            if ((lead & 0xE0u) == 0xC0u) { length = 2; }
            else if ((lead & 0xF0u) == 0xE0u) { length = 3; }
            else if ((lead & 0xF8u) == 0xF0u) { length = 4; }
            std::string letter = tooltip.substr(0, std::min(length, tooltip.size()));
            if (letter.size() == 1)
            {
                letter[0] = static_cast<char>(std::toupper(lead));
            }
            return letter;
        }

    } // namespace

    bool HasSystemTray(Display* display, const int screen)
    {
        const std::string name = "_NET_SYSTEM_TRAY_S" + std::to_string(screen);
        const Atom selection = XInternAtom(display, name.c_str(), kXFalse);
        return XGetSelectionOwner(display, selection) != kNone;
    }

    // --- the service ------------------------------------------------------------------------------

    X11Tray::X11Tray(const std::string& displayName)
        : display_(XOpenDisplay(displayName.empty() ? nullptr : displayName.c_str()))
    {
        if (display_ == nullptr)
        {
            throw PlatformException("X11Tray", "cannot open the display '" + displayName + "'");
        }
        X11ErrorPolicy::Register(display_);
        screen_ = DefaultScreen(display_);
        root_ = RootWindow(display_, screen_);
        const std::string selection = "_NET_SYSTEM_TRAY_S" + std::to_string(screen_);
        selection_ = XInternAtom(display_, selection.c_str(), kXFalse);
        opcode_ = XInternAtom(display_, "_NET_SYSTEM_TRAY_OPCODE", kXFalse);
        manager_ = XInternAtom(display_, "MANAGER", kXFalse);
        xembedInfo_ = XInternAtom(display_, "_XEMBED_INFO", kXFalse);
        netWmName_ = XInternAtom(display_, "_NET_WM_NAME", kXFalse);
        utf8_ = XInternAtom(display_, "UTF8_STRING", kXFalse);
        windowType_ = XInternAtom(display_, "_NET_WM_WINDOW_TYPE", kXFalse);
        typePopupMenu_ = XInternAtom(display_, "_NET_WM_WINDOW_TYPE_POPUP_MENU", kXFalse);
        typeTooltip_ = XInternAtom(display_, "_NET_WM_WINDOW_TYPE_TOOLTIP", kXFalse);
        // A tray that starts, or starts again, announces itself to the root window.
        XSelectInput(display_, root_, StructureNotifyMask);
        font_ = std::make_unique<X11CoreFont>(display_);
        palette_.background = Colour(0xF2, 0xF2, 0xF2);
        palette_.text = Colour(0x20, 0x20, 0x20);
        palette_.disabled = Colour(0x90, 0x90, 0x90);
        palette_.accent = Colour(0x30, 0x70, 0xC0);
        palette_.accentText = WhitePixel(display_, screen_);
        palette_.border = Colour(0x80, 0x80, 0x80);
        palette_.tooltip = Colour(0xFF, 0xFF, 0xE1);
        XFlush(display_);
    }

    X11Tray::~X11Tray()
    {
        // The contract has every icon released first; one that was not is detached, not left to
        // call into a connection that is gone.
        for (X11TrayIcon* icon : icons_)
        {
            icon->CloseMenu();
            icon->HideTooltip();
            if (icon->gc_ != nullptr) { XFreeGC(display_, icon->gc_); }
            icon->gc_ = nullptr;
            icon->window_ = kNone;
            icon->tray_ = nullptr;
        }
        icons_.clear();
        font_.reset();
        X11ErrorPolicy::Unregister(display_);
        XCloseDisplay(display_);
    }

    std::unique_ptr<IPlatformTrayIcon> X11Tray::CreateTray(const std::string& tooltip)
    {
        auto icon = std::make_unique<X11TrayIcon>(*this, tooltip);
        icons_.push_back(icon.get());
        return icon;
    }

    void X11Tray::Unregister(X11TrayIcon* icon)
    {
        icons_.erase(std::remove(icons_.begin(), icons_.end(), icon), icons_.end());
    }

    ::Window X11Tray::Manager() const
    {
        return XGetSelectionOwner(display_, selection_);
    }

    unsigned long X11Tray::Colour(const unsigned short red, const unsigned short green, const unsigned short blue) const
    {
        XColor colour{};
        colour.red = static_cast<unsigned short>(red * 257);
        colour.green = static_cast<unsigned short>(green * 257);
        colour.blue = static_cast<unsigned short>(blue * 257);
        colour.flags = DoRed | DoGreen | DoBlue;
        return XAllocColor(display_, DefaultColormap(display_, screen_), &colour) != 0 ? colour.pixel
                                                                                         : BlackPixel(display_, screen_);
    }

    void X11Tray::Pump()
    {
        while (XPending(display_) > 0)
        {
            XEvent event{};
            XNextEvent(display_, &event);
            if (event.type == ClientMessage && event.xclient.window == root_ &&
                event.xclient.message_type == manager_ && static_cast<Atom>(event.xclient.data.l[1]) == selection_)
            {
                // A new tray: every icon docks with it, a window made again where the old tray
                // took the last one with it.
                for (X11TrayIcon* icon : icons_)
                {
                    if (icon->window_ == kNone)
                    {
                        icon->CreateWindow();
                    }
                    icon->Dock();
                }
                continue;
            }
            for (X11TrayIcon* icon : icons_)
            {
                if (icon->Owns(event.xany.window))
                {
                    icon->Handle(event);
                    break;
                }
            }
        }
        const auto now = std::chrono::steady_clock::now();
        for (X11TrayIcon* icon : icons_)
        {
            icon->Tick(now);
        }
        XFlush(display_);
        // Outside every loop over the icons: a callback may well destroy the icon it belongs to.
        std::vector<TrayEntryClickCallback> ready;
        ready.swap(ready_);
        for (TrayEntryClickCallback& callback : ready)
        {
            if (callback)
            {
                callback();
            }
        }
    }

    // --- one icon ---------------------------------------------------------------------------------

    X11TrayIcon::X11TrayIcon(X11Tray& tray, std::string tooltip) : tray_(&tray), tooltip_(std::move(tooltip))
    {
        CreateWindow();
        Dock();
    }

    X11TrayIcon::~X11TrayIcon()
    {
        if (tray_ == nullptr)
        {
            return;
        }
        CloseMenu();
        HideTooltip();
        Display* display = tray_->display_;
        if (gc_ != nullptr)
        {
            XFreeGC(display, gc_);
        }
        if (window_ != kNone)
        {
            X11ErrorTrap trap(display);
            XDestroyWindow(display, window_);
            trap.Sync();
        }
        tray_->Unregister(this);
        XFlush(display);
    }

    void X11TrayIcon::CreateWindow()
    {
        Display* display = tray_->display_;
        XSetWindowAttributes attributes{};
        attributes.background_pixel = tray_->palette_.accent;
        attributes.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | EnterWindowMask |
                                LeaveWindowMask | StructureNotifyMask;
        window_ = XCreateWindow(display, tray_->root_, 0, 0, static_cast<unsigned>(width_), static_cast<unsigned>(height_),
                                0, CopyFromParent, InputOutput, CopyFromParent, CWBackPixel | CWEventMask, &attributes);
        const unsigned long info[2] = {0, kXEmbedMapped};
        XChangeProperty(display, window_, tray_->xembedInfo_, tray_->xembedInfo_, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(info), 2);
        XClassHint* classHint = XAllocClassHint();
        std::string name = "cna-tray-icon";
        std::string className = "CNA";
        classHint->res_name = name.data();
        classHint->res_class = className.data();
        XSetClassHint(display, window_, classHint);
        XFree(classHint);
        SetName(window_, tooltip_);
        if (gc_ == nullptr)
        {
            gc_ = XCreateGC(display, window_, 0, nullptr);
        }
    }

    void X11TrayIcon::Dock()
    {
        const ::Window manager = tray_->Manager();
        if (manager == kNone || window_ == kNone)
        {
            return;  // Docked when a tray announces itself.
        }
        XEvent request{};
        request.xclient.type = ClientMessage;
        request.xclient.window = manager;
        request.xclient.message_type = tray_->opcode_;
        request.xclient.format = 32;
        request.xclient.data.l[0] = static_cast<long>(kCurrentTime);
        request.xclient.data.l[1] = kRequestDock;
        request.xclient.data.l[2] = static_cast<long>(window_);
        XSendEvent(tray_->display_, manager, kXFalse, NoEventMask, &request);
        XFlush(tray_->display_);
    }

    void X11TrayIcon::SetName(const ::Window window, const std::string& name) const
    {
        if (window == kNone)
        {
            return;
        }
        XChangeProperty(tray_->display_, window, tray_->netWmName_, tray_->utf8_, 8, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(name.data()), static_cast<int>(name.size()));
    }

    bool X11TrayIcon::Owns(const ::Window window) const
    {
        return window != kNone && (window == window_ || window == menu_ || window == tip_);
    }

    int X11TrayIcon::RowHeight() const
    {
        return tray_->font_->Ascent() + tray_->font_->Descent() + 8;
    }

    int X11TrayIcon::EntryAt(const int x, const int y) const
    {
        if (x < 0 || x >= menuWidth_ || y < kMenuPadding)
        {
            return -1;
        }
        const int index = (y - kMenuPadding) / RowHeight();
        return index < static_cast<int>(entries_.size()) ? index : -1;
    }

    void X11TrayIcon::DrawIcon()
    {
        if (window_ == kNone)
        {
            return;
        }
        Display* display = tray_->display_;
        const X11CoreFont& font = *tray_->font_;
        XSetForeground(display, gc_, tray_->palette_.accent);
        XFillRectangle(display, window_, gc_, 0, 0, static_cast<unsigned>(width_), static_cast<unsigned>(height_));
        const std::string letter = IconLetter(tooltip_);
        XSetForeground(display, gc_, tray_->palette_.accentText);
        font.Draw(window_, gc_, (width_ - font.Measure(letter)) / 2,
                  (height_ + font.Ascent() - font.Descent()) / 2, letter);
    }

    void X11TrayIcon::OpenMenu(const int rootX, const int rootY)
    {
        if (entries_.empty() || menu_ != kNone)
        {
            return;
        }
        HideTooltip();
        Display* display = tray_->display_;
        const X11CoreFont& font = *tray_->font_;
        int widest = 0;
        for (const Entry& entry : entries_)
        {
            widest = std::max(widest, font.Measure(entry.label));
        }
        menuWidth_ = std::max(kMinimumMenuWidth, kCheckColumn + widest + 2 * kTextPadding);
        menuHeight_ = static_cast<int>(entries_.size()) * RowHeight() + 2 * kMenuPadding;
        const int screenWidth = DisplayWidth(display, tray_->screen_);
        const int screenHeight = DisplayHeight(display, tray_->screen_);
        int x = std::min(rootX, screenWidth - menuWidth_ - 2);
        int y = rootY + menuHeight_ + 2 > screenHeight ? rootY - menuHeight_ - 2 : rootY;
        x = std::max(0, x);
        y = std::max(0, y);

        XSetWindowAttributes attributes{};
        attributes.override_redirect = kXTrue;
        attributes.save_under = kXTrue;
        attributes.background_pixel = tray_->palette_.background;
        attributes.border_pixel = tray_->palette_.border;
        attributes.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | LeaveWindowMask;
        menu_ = XCreateWindow(display, tray_->root_, x, y, static_cast<unsigned>(menuWidth_),
                              static_cast<unsigned>(menuHeight_), 1, CopyFromParent, InputOutput, CopyFromParent,
                              CWOverrideRedirect | CWSaveUnder | CWBackPixel | CWBorderPixel | CWEventMask, &attributes);
        XChangeProperty(display, menu_, tray_->windowType_, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(&tray_->typePopupMenu_), 1);
        SetName(menu_, tooltip_.empty() ? std::string("CNA tray menu") : tooltip_);
        XMapRaised(display, menu_);
        // Every click goes to the menu while it is open: one outside it closes it.
        XGrabPointer(display, menu_, kXTrue, ButtonPressMask | ButtonReleaseMask | PointerMotionMask | LeaveWindowMask,
                     GrabModeAsync, GrabModeAsync, kNone, kNone, kCurrentTime);
        hover_ = -1;
        pressedInMenu_ = false;
        XFlush(display);
    }

    void X11TrayIcon::CloseMenu()
    {
        if (menu_ == kNone || tray_ == nullptr)
        {
            return;
        }
        Display* display = tray_->display_;
        XUngrabPointer(display, kCurrentTime);
        XDestroyWindow(display, menu_);
        menu_ = kNone;
        hover_ = -1;
        pressedInMenu_ = false;
        XFlush(display);
    }

    void X11TrayIcon::DrawMenu()
    {
        if (menu_ == kNone || tray_ == nullptr)
        {
            return;
        }
        Display* display = tray_->display_;
        const X11CoreFont& font = *tray_->font_;
        const unsigned long background = tray_->palette_.background;
        const unsigned long text = tray_->palette_.text;
        const unsigned long disabled = tray_->palette_.disabled;
        const unsigned long highlight = tray_->palette_.accent;
        const unsigned long highlightText = tray_->palette_.accentText;
        XSetForeground(display, gc_, background);
        XFillRectangle(display, menu_, gc_, 0, 0, static_cast<unsigned>(menuWidth_), static_cast<unsigned>(menuHeight_));
        const int row = RowHeight();
        for (std::size_t index = 0; index < entries_.size(); ++index)
        {
            const Entry& entry = entries_[index];
            const int top = kMenuPadding + static_cast<int>(index) * row;
            const bool lit = static_cast<int>(index) == hover_ && entry.enabled;
            if (lit)
            {
                XSetForeground(display, gc_, highlight);
                XFillRectangle(display, menu_, gc_, 1, top, static_cast<unsigned>(menuWidth_ - 2),
                               static_cast<unsigned>(row));
            }
            const unsigned long ink = !entry.enabled ? disabled : lit ? highlightText : text;
            XSetForeground(display, gc_, ink);
            if (entry.checkable)
            {
                const int boxTop = top + (row - 10) / 2;
                XDrawRectangle(display, menu_, gc_, kTextPadding / 2, boxTop, 10, 10);
                if (entry.checked)
                {
                    XSetLineAttributes(display, gc_, 2, LineSolid, CapRound, JoinRound);
                    XDrawLine(display, menu_, gc_, kTextPadding / 2 + 2, boxTop + 5, kTextPadding / 2 + 4, boxTop + 8);
                    XDrawLine(display, menu_, gc_, kTextPadding / 2 + 4, boxTop + 8, kTextPadding / 2 + 9, boxTop + 2);
                    XSetLineAttributes(display, gc_, 0, LineSolid, CapButt, JoinMiter);
                }
            }
            font.Draw(menu_, gc_, kCheckColumn + kTextPadding / 2, top + 4 + font.Ascent(), entry.label);
        }
    }

    void X11TrayIcon::ShowTooltip()
    {
        if (tooltip_.empty() || tip_ != kNone || menu_ != kNone)
        {
            return;
        }
        Display* display = tray_->display_;
        const X11CoreFont& font = *tray_->font_;
        ::Window root = kNone;
        ::Window child = kNone;
        int pointerX = 0;
        int pointerY = 0;
        int windowX = 0;
        int windowY = 0;
        unsigned int mask = 0;
        XQueryPointer(display, tray_->root_, &root, &child, &pointerX, &pointerY, &windowX, &windowY, &mask);
        const int width = font.Measure(tooltip_) + 12;
        const int height = font.Ascent() + font.Descent() + 8;
        const int x = std::max(0, std::min(pointerX + 12, DisplayWidth(display, tray_->screen_) - width - 2));
        int y = pointerY + 18;
        if (y + height > DisplayHeight(display, tray_->screen_))
        {
            y = std::max(0, pointerY - height - 6);
        }
        XSetWindowAttributes attributes{};
        attributes.override_redirect = kXTrue;
        attributes.save_under = kXTrue;
        attributes.background_pixel = tray_->palette_.tooltip;
        attributes.border_pixel = tray_->palette_.border;
        attributes.event_mask = ExposureMask;
        tip_ = XCreateWindow(display, tray_->root_, x, y, static_cast<unsigned>(width), static_cast<unsigned>(height), 1,
                             CopyFromParent, InputOutput, CopyFromParent,
                             CWOverrideRedirect | CWSaveUnder | CWBackPixel | CWBorderPixel | CWEventMask, &attributes);
        XChangeProperty(display, tip_, tray_->windowType_, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(&tray_->typeTooltip_), 1);
        SetName(tip_, tooltip_);
        XMapRaised(display, tip_);
        XFlush(display);
    }

    void X11TrayIcon::HideTooltip()
    {
        if (tip_ != kNone && tray_ != nullptr)
        {
            XDestroyWindow(tray_->display_, tip_);
            tip_ = kNone;
            XFlush(tray_->display_);
        }
    }

    void X11TrayIcon::Tick(const std::chrono::steady_clock::time_point now)
    {
        if (hoverSince_ && tip_ == kNone && menu_ == kNone && now - *hoverSince_ >= kTooltipDelay)
        {
            ShowTooltip();
            hoverSince_.reset();
        }
    }

    void X11TrayIcon::Handle(XEvent& event)
    {
        Display* display = tray_->display_;
        const ::Window window = event.xany.window;
        if (window == tip_)
        {
            if (event.type == Expose && event.xexpose.count == 0)
            {
                XSetForeground(display, gc_, tray_->palette_.text);
                tray_->font_->Draw(tip_, gc_, 6, 4 + tray_->font_->Ascent(), tooltip_);
            }
            return;
        }
        if (window == menu_)
        {
            switch (event.type)
            {
                case Expose:
                    if (event.xexpose.count == 0) { DrawMenu(); }
                    break;
                case MotionNotify:
                {
                    const int now = EntryAt(event.xmotion.x, event.xmotion.y);
                    if (now != hover_)
                    {
                        hover_ = now;
                        DrawMenu();
                    }
                    break;
                }
                case LeaveNotify:
                    if (hover_ != -1)
                    {
                        hover_ = -1;
                        DrawMenu();
                    }
                    break;
                case ButtonPress:
                    if (event.xbutton.x < 0 || event.xbutton.y < 0 || event.xbutton.x >= menuWidth_ ||
                        event.xbutton.y >= menuHeight_)
                    {
                        CloseMenu();  // A click anywhere else.
                    }
                    else
                    {
                        pressedInMenu_ = true;
                    }
                    break;
                case ButtonRelease:
                {
                    if (!pressedInMenu_)
                    {
                        break;  // The release of the click that opened the menu, say.
                    }
                    pressedInMenu_ = false;
                    const int index = EntryAt(event.xbutton.x, event.xbutton.y);
                    if (index < 0 || !entries_[static_cast<std::size_t>(index)].enabled)
                    {
                        break;
                    }
                    Entry& entry = entries_[static_cast<std::size_t>(index)];
                    if (entry.checkable)
                    {
                        entry.checked = !entry.checked;
                    }
                    tray_->ready_.push_back(entry.onClick);
                    CloseMenu();
                    break;
                }
                default:
                    break;
            }
            return;
        }
        if (window != window_)
        {
            return;
        }
        switch (event.type)
        {
            case Expose:
                if (event.xexpose.count == 0) { DrawIcon(); }
                break;
            case ConfigureNotify:
                // The tray sizes its icons.
                width_ = std::max(1, event.xconfigure.width);
                height_ = std::max(1, event.xconfigure.height);
                break;
            case EnterNotify:
                hoverSince_ = std::chrono::steady_clock::now();
                break;
            case LeaveNotify:
                hoverSince_.reset();
                HideTooltip();
                break;
            case ButtonPress:
                hoverSince_.reset();
                HideTooltip();
                break;
            case ButtonRelease:
                if (event.xbutton.button == Button1 || event.xbutton.button == Button3)
                {
                    if (menu_ != kNone)
                    {
                        CloseMenu();
                    }
                    else
                    {
                        OpenMenu(event.xbutton.x_root, event.xbutton.y_root);
                    }
                }
                break;
            case DestroyNotify:
                if (event.xdestroywindow.window == window_)
                {
                    // Taken down with a tray that did not hand it back: made again for the next.
                    window_ = kNone;
                    CloseMenu();
                    HideTooltip();
                }
                break;
            default:
                break;
        }
    }

    void X11TrayIcon::SetTooltip(const std::string& tooltip)
    {
        tooltip_ = tooltip;
        if (tray_ == nullptr)
        {
            return;
        }
        SetName(window_, tooltip_);
        if (tip_ != kNone)
        {
            HideTooltip();
            ShowTooltip();
        }
        DrawIcon();
        XFlush(tray_->display_);
    }

    std::size_t X11TrayIcon::AddEntry(const std::string& label, const bool checkable, const bool initiallyChecked,
                                      const bool initiallyEnabled, TrayEntryClickCallback onClick)
    {
        Entry entry;
        entry.label = label;
        entry.checkable = checkable;
        entry.checked = checkable && initiallyChecked;
        entry.enabled = initiallyEnabled;
        entry.onClick = std::move(onClick);
        entries_.push_back(std::move(entry));
        // An open menu is sized for what it had; it takes the new entry when it opens again.
        CloseMenu();
        return entries_.size() - 1;
    }

    void X11TrayIcon::SetEntryLabel(const std::size_t index, const std::string& label)
    {
        if (index < entries_.size())
        {
            entries_[index].label = label;
            DrawMenu();
        }
    }

    void X11TrayIcon::SetEntryChecked(const std::size_t index, const bool checked)
    {
        if (index < entries_.size() && entries_[index].checkable)
        {
            entries_[index].checked = checked;
            DrawMenu();
        }
    }

    bool X11TrayIcon::GetEntryChecked(const std::size_t index) const
    {
        return index < entries_.size() && entries_[index].checked;
    }

    void X11TrayIcon::SetEntryEnabled(const std::size_t index, const bool enabled)
    {
        if (index < entries_.size())
        {
            entries_[index].enabled = enabled;
            DrawMenu();
        }
    }

    bool X11TrayIcon::GetEntryEnabled(const std::size_t index) const
    {
        return index < entries_.size() && entries_[index].enabled;
    }

} // namespace CNA::Platform::X11
