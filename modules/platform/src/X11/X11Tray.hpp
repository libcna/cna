// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "X11CoreFont.hpp"
#include "X11Headers.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace CNA::Platform::X11 {

    class X11TrayIcon;

    /**
     * @brief Gets whether a screen has a system tray: a client owning `_NET_SYSTEM_TRAY_S<n>`.
     * @param display A connection to its server.
     * @param screen The screen.
     * @return True when one does.
     */
    [[nodiscard]] bool HasSystemTray(Display* display, int screen);

    /**
     * @brief Tray icons through X11's own system tray protocol (plans/plan_x11.md X11-0171).
     *
     * The freedesktop System Tray Protocol: an icon is a small window of the game's that the tray
     * -- whichever client owns `_NET_SYSTEM_TRAY_S<screen>` -- embeds with XEmbed when asked to
     * dock it. Xfce, MATE, LXDE, i3bar and the like show such icons themselves, and KDE Plasma
     * through its XEmbed bridge; GNOME has no tray, and where no client owns the selection when the
     * platform is made, `tray` is false. The menu is a popup window of the icon's own, drawn with
     * Xlib like the message box, opened by a click on the icon; an entry's callback runs from the
     * platform's `PollEvents`, a checkable entry toggled first, as the other backends' native menus
     * do. A tray that restarts announces itself, and every icon docks with it again.
     *
     * The service talks to the server on a connection of its own, which `PollEvents` reads; the
     * game's windows and their event queue are not involved.
     */
    class X11Tray final : public IPlatformTray
    {
    public:
        /**
         * @brief Opens the service's connection.
         * @param displayName The server the platform talks to.
         * @throws PlatformException When the connection cannot be opened.
         */
        explicit X11Tray(const std::string& displayName);

        /** @brief Closes the connection; an icon still alive is detached and does nothing more. */
        ~X11Tray() override;

        X11Tray(const X11Tray&) = delete;
        X11Tray& operator=(const X11Tray&) = delete;

        /**
         * @brief Creates an icon and docks it with the tray, or with the next one to appear.
         * @param tooltip The tooltip, or empty.
         * @return The icon.
         */
        [[nodiscard]] std::unique_ptr<IPlatformTrayIcon> CreateTray(const std::string& tooltip) override;

        /** @brief Reads the service's connection: clicks, the menus, a tray that restarts. */
        void Pump();

    private:
        friend class X11TrayIcon;

        void Unregister(X11TrayIcon* icon);
        [[nodiscard]] ::Window Manager() const;
        [[nodiscard]] unsigned long Colour(unsigned short red, unsigned short green, unsigned short blue) const;

        Display* display_ = nullptr;
        int screen_ = 0;
        ::Window root_ = kNone;
        Atom selection_ = kNone;
        Atom opcode_ = kNone;
        Atom manager_ = kNone;
        Atom xembedInfo_ = kNone;
        Atom netWmName_ = kNone;
        Atom utf8_ = kNone;
        Atom windowType_ = kNone;
        Atom typePopupMenu_ = kNone;
        Atom typeTooltip_ = kNone;
        std::unique_ptr<X11CoreFont> font_;
        /// Allocated once: a menu redraws on every pointer move.
        struct Palette
        {
            unsigned long background = 0;
            unsigned long text = 0;
            unsigned long disabled = 0;
            unsigned long accent = 0;
            unsigned long accentText = 0;
            unsigned long border = 0;
            unsigned long tooltip = 0;
        } palette_;
        std::vector<X11TrayIcon*> icons_;
        std::vector<TrayEntryClickCallback> ready_;
    };

    /** @brief One icon in the tray and its flat menu (X11-0171). */
    class X11TrayIcon final : public IPlatformTrayIcon
    {
    public:
        /**
         * @brief Creates the icon's window and asks the tray to dock it.
         * @param tray The service.
         * @param tooltip The tooltip, or empty.
         */
        X11TrayIcon(X11Tray& tray, std::string tooltip);

        /** @brief Removes the icon: its window, its menu, its tooltip. */
        ~X11TrayIcon() override;

        X11TrayIcon(const X11TrayIcon&) = delete;
        X11TrayIcon& operator=(const X11TrayIcon&) = delete;

        /** @brief Changes the tooltip. @param tooltip The text, or empty for none. */
        void SetTooltip(const std::string& tooltip) override;
        /**
         * @brief Appends a menu entry.
         * @param label The label.
         * @param checkable Whether it has a check mark.
         * @param initiallyChecked Its first state.
         * @param initiallyEnabled Whether it can be chosen.
         * @param onClick Called from PollEvents when it is chosen; may be empty.
         * @return Its index.
         */
        [[nodiscard]] std::size_t AddEntry(const std::string& label, bool checkable, bool initiallyChecked,
                                           bool initiallyEnabled, TrayEntryClickCallback onClick) override;
        /** @brief Changes a label. @param index The entry. @param label The label. */
        void SetEntryLabel(std::size_t index, const std::string& label) override;
        /** @brief Changes a check mark. @param index The entry. @param checked The state. */
        void SetEntryChecked(std::size_t index, bool checked) override;
        /** @brief Reads a check mark. @param index The entry. @return The state; false if unknown. */
        [[nodiscard]] bool GetEntryChecked(std::size_t index) const override;
        /** @brief Changes whether an entry can be chosen. @param index The entry. @param enabled The state. */
        void SetEntryEnabled(std::size_t index, bool enabled) override;
        /** @brief Reads whether an entry can be chosen. @param index The entry. @return The state. */
        [[nodiscard]] bool GetEntryEnabled(std::size_t index) const override;

        /** @brief Gets the icon's window, for tests. @return The XID, or none while it has none. */
        [[nodiscard]] ::Window GetWindow() const { return window_; }

    private:
        friend class X11Tray;

        struct Entry
        {
            std::string label;
            bool checkable = false;
            bool checked = false;
            bool enabled = true;
            TrayEntryClickCallback onClick;
        };

        [[nodiscard]] bool Owns(::Window window) const;
        void Handle(XEvent& event);
        void Tick(std::chrono::steady_clock::time_point now);
        void CreateWindow();
        void Dock();
        void DrawIcon();
        void OpenMenu(int rootX, int rootY);
        void CloseMenu();
        void DrawMenu();
        void ShowTooltip();
        void HideTooltip();
        [[nodiscard]] int RowHeight() const;
        [[nodiscard]] int EntryAt(int x, int y) const;
        void SetName(::Window window, const std::string& name) const;

        X11Tray* tray_;  // Null once the service is gone before the icon.
        std::string tooltip_;
        std::vector<Entry> entries_;
        ::Window window_ = kNone;
        ::Window menu_ = kNone;
        ::Window tip_ = kNone;
        GC gc_ = nullptr;
        int width_ = 22;
        int height_ = 22;
        int menuWidth_ = 0;
        int menuHeight_ = 0;
        int hover_ = -1;
        bool pressedInMenu_ = false;
        std::optional<std::chrono::steady_clock::time_point> hoverSince_;
    };

} // namespace CNA::Platform::X11
