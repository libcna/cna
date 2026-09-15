// SPDX-License-Identifier: MS-PL
#pragma once

#include "X11Headers.hpp"

#include "../Freedesktop/ScreenSaverBus.hpp"

namespace CNA::Platform::X11 {

    class X11Connection;

    /**
     * @brief Keeps the screen from blanking while a game asks it to (plans/plan_x11.md X11-0170).
     *
     * The screen saver a user sees is the desktop's -- GNOME's, KDE's, Xfce's idle blanking and
     * locking -- not the X server's own, and the desktop takes requests on the session bus:
     * `org.freedesktop.ScreenSaver.Inhibit`. That is asked first, through
     * `Freedesktop::ScreenSaverBusInhibition` (shared with the Wayland backend), on a connection
     * that lasts exactly as long as the request, so the desktop forgets the request the moment
     * the game goes away, however it goes. Without such a service the X server's saver
     * is suspended for this client alone (MIT-SCREEN-SAVER 1.1's `XScreenSaverSuspend`), which the
     * server also gives back if the client dies. Only a server without that extension has its
     * timeout set to zero and restored, the one way that outlives a crashed game.
     */
    class X11ScreenSaverInhibitor
    {
    public:
        /** @brief How the screen is being kept on. */
        enum class Method
        {
            /** @brief It is not. */
            None,
            /** @brief The desktop's `org.freedesktop.ScreenSaver` holds an inhibition. */
            DesktopService,
            /** @brief The X server's saver is suspended for this client. */
            ServerSuspend,
            /** @brief The X server's saver timeout is zero until lifted. */
            ServerTimeout
        };

        /**
         * @brief Inhibits on one connection's server when asked to.
         * @param connection The platform's connection.
         */
        explicit X11ScreenSaverInhibitor(X11Connection& connection);

        /** @brief Lifts an inhibition still in force. */
        ~X11ScreenSaverInhibitor();

        X11ScreenSaverInhibitor(const X11ScreenSaverInhibitor&) = delete;
        X11ScreenSaverInhibitor& operator=(const X11ScreenSaverInhibitor&) = delete;

        /** @brief Keeps the screen on, by the first method that works; nothing if already. */
        void Inhibit();

        /** @brief Lets the screen blank again; nothing if it already may. */
        void Lift();

        /** @brief Gets how the screen is kept on. @return The method, or None. */
        [[nodiscard]] Method GetMethod() const { return method_; }

    private:
        bool SuspendServerSaver(bool suspend);

        X11Connection& connection_;
        Method method_ = Method::None;
        Freedesktop::ScreenSaverBusInhibition desktop_;
        int savedTimeout_ = -1;
    };

} // namespace CNA::Platform::X11
