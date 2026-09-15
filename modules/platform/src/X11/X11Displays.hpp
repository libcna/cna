// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "X11Headers.hpp"

#include <map>
#include <vector>

namespace CNA::Platform::X11 {

    class X11Connection;
    class X11Window;

    /**
     * @brief Enumerates monitors through XRandR.
     *
     * ### One X screen is not one monitor
     *
     * It has not been since XRandR 1.2. A modern X session presents every physical monitor as one
     * screen whose root window spans them all, and the per-monitor geometry lives in RandR's CRTC
     * and monitor objects. Reporting `ScreenCount(display)` displays would report **one** on a
     * three-monitor desktop, and a game would then place its fullscreen window across all three.
     *
     * The enumeration therefore prefers `XRRGetMonitors` (RandR 1.5), which is the server's own
     * notion of a monitor and correctly merges a pair of CRTCs driving one physical panel. It
     * falls back to enumerating active CRTCs (RandR 1.2), and only when RandR is absent entirely
     * does it report the whole screen as a single display — which is then the truth, because a
     * server without RandR genuinely cannot subdivide it.
     *
     * ### Ids
     *
     * Display ids start at 1. Zero is reserved: `GraphicsAdapter` treats id 0 as "no display" and
     * falls back to a default mode, so a display numbered 0 would be invisible to it. The SDL2
     * backend offsets its dense index for the same reason.
     */
    class X11Displays final : public IPlatformDisplays
    {
    public:
        /**
         * @brief Builds the display service for one connection.
         *
         * @param connection The connection to enumerate on.
         */
        explicit X11Displays(X11Connection& connection);

        /** @brief Gets every connected display. @return The displays; never empty on a live server. */
        [[nodiscard]] std::vector<DisplayInfo> GetDisplays() const override;

        /**
         * @brief Gets the display a window is mostly on.
         * @param window The window to locate.
         * @param display Receives the display; untouched on false.
         * @return True when the window belongs to this platform and overlaps a known display.
         */
        [[nodiscard]] bool TryGetDisplayForWindow(const IPlatformWindow& window,
                                                  DisplayInfo& display) const override;

        /**
         * @brief Gets the unobscured interactive region of a window.
         *
         * Always false. X11 has no safe-area concept: panels and docks reserve space through
         * `_NET_WORKAREA`, which describes the desktop rather than a window's client area, and
         * a window's own client area is never obscured by a system chrome the way a phone's is.
         * Returning the full client bounds would be a fabricated answer.
         *
         * @param window The window whose client area is being described.
         * @param safeArea Left untouched.
         * @return False, always.
         */
        [[nodiscard]] bool TryGetSafeAreaForWindow(const IPlatformWindow& window,
                                                   WindowBounds& safeArea) const override;

        /**
         * @brief Gets the modes a display supports.
         * @param displayId Which display.
         * @return The supported modes; empty when the display is unknown or RandR is absent.
         */
        [[nodiscard]] std::vector<DisplayMode> GetDisplayModes(
            std::uint32_t displayId) const override;

        /**
         * @brief Gets the mode a display is currently in.
         * @param displayId Which display.
         * @param mode Receives the mode; untouched on false.
         * @return True when the display is known.
         */
        [[nodiscard]] bool TryGetCurrentDisplayMode(std::uint32_t displayId,
                                                    DisplayMode& mode) const override;

        /**
         * @brief Gets whether the host screen saver may activate.
         * @return True when the server's screen saver timeout is non-zero.
         */
        [[nodiscard]] bool IsScreenSaverEnabled() const override;

        /**
         * @brief Allows or prevents the host screen saver from activating.
         * @param enabled True to allow screen saving.
         */
        void SetScreenSaverEnabled(bool enabled) override;

        /** @brief Discards the cached enumeration after a RandR configuration change. */
        void InvalidateCache();

        /**
         * @brief Gets the content scale of the display showing most of an area.
         *
         * From the cached enumeration, with no server round trip: called as windows move, to
         * notice one crossing onto a monitor with a scale of its own (X11-0156).
         *
         * @param bounds The area, in root coordinates.
         * @return That display's content scale.
         */
        [[nodiscard]] float ContentScaleAt(const WindowBounds& bounds) const;

    private:
        struct CachedDisplay
        {
            DisplayInfo info;
            /// The RandR CRTC and output behind this display, as raw XIDs so the struct compiles
            /// in a build without the Xrandr headers. Zero when RandR is not in use.
            XID crtc = 0;
            XID output = 0;
            /// The mode the monitor is in now. Differs from info.desktopMode only while exclusive
            /// fullscreen holds a mode of its own on it (X11-0153).
            DisplayMode currentMode;
        };

        void EnsureCache() const;
        void BuildCache() const;

        X11Connection& connection_;
        mutable std::vector<CachedDisplay> cache_;
        mutable bool cacheValid_ = false;
        /// The mode switcher's generation the cache was built at.
        mutable std::uint64_t modeGeneration_ = 0;
        int savedScreenSaverTimeout_ = -1;
    };

} // namespace CNA::Platform::X11
