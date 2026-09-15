// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>

#if defined(CNA_PLATFORM_HAVE_DBUS)
#include "DBusLibrary.hpp"
#endif

namespace CNA::Platform::Freedesktop {

    /**
     * @brief The name a game gives the desktop when it asks to keep the screen on.
     * @return The executable's name, or "CNA" when it cannot be read.
     */
    [[nodiscard]] std::string ScreenSaverApplicationName();

    /**
     * @brief One request to the desktop's `org.freedesktop.ScreenSaver` to keep the screen on
     * (plans/plan_x11.md X11-0170; shared with Wayland by plans/plan_wayland.md WAYLAND-0012).
     *
     * The screen saver a user sees is the desktop's -- GNOME's, KDE's, Xfce's idle blanking and
     * locking -- and it takes requests on the session bus. The request is made on a connection of
     * this object's own that lasts exactly as long as the request, so the desktop forgets it the
     * moment the game goes away, however it goes. Nothing is started to make it: only a service
     * already on the bus is asked.
     */
    class ScreenSaverBusInhibition
    {
    public:
        ScreenSaverBusInhibition() = default;

        /** @brief Lifts a request still in force. */
        ~ScreenSaverBusInhibition();

        ScreenSaverBusInhibition(const ScreenSaverBusInhibition&) = delete;
        ScreenSaverBusInhibition& operator=(const ScreenSaverBusInhibition&) = delete;

        /**
         * @brief Asks the desktop to keep the screen on.
         * @return True when the desktop accepted (or had already accepted) the request.
         */
        [[nodiscard]] bool Inhibit();

        /** @brief Tells the desktop the screen may blank again; nothing if no request is held. */
        void Lift();

        /** @brief Gets whether a request is held. @return True while the desktop holds one. */
        [[nodiscard]] bool IsHeld() const;

    private:
#if defined(CNA_PLATFORM_HAVE_DBUS)
        DBusConnection* bus_ = nullptr;
        std::uint32_t cookie_ = 0;
#endif
    };

} // namespace CNA::Platform::Freedesktop
