// SPDX-License-Identifier: MS-PL
#pragma once

#include "X11Headers.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Platform::X11 {

    class X11Connection;

    /**
     * @brief A content scale as a caller may use it: finite, within [0.5, 8].
     *
     * A value outside that is a broken setting rather than a monitor, and honouring it would
     * size an interface to nothing or to the whole screen.
     *
     * @param scale The candidate.
     * @return The scale, or nothing when it is not usable.
     */
    [[nodiscard]] std::optional<float> UsableScale(double scale) noexcept;

    /**
     * @brief The scale `Xft.dpi` states in a resource-manager string, `dpi / 96`.
     *
     * @param resources The `RESOURCE_MANAGER` text.
     * @return The scale, or nothing when there is no usable `Xft.dpi`.
     */
    [[nodiscard]] std::optional<float> ScaleFromXftDpi(std::string_view resources);

    /**
     * @brief The scale an XSETTINGS manager publishes.
     *
     * `Gdk/WindowScalingFactor` first, then `Xft/DPI` (stored in 1024ths of a DPI) over 96 --
     * SDL3's order. The property's own byte order is honoured, and a malformed one is read as far
     * as it is well-formed and no further.
     *
     * @param property The `_XSETTINGS_SETTINGS` property's bytes.
     * @return The scale, or nothing when neither setting is there.
     */
    [[nodiscard]] std::optional<float> ScaleFromXsettings(const std::vector<unsigned char>& property);

    /** @brief Per-screen scale factors, as KDE's X11 session publishes them. */
    struct X11ScreenScaleFactors
    {
        /** @brief Factors keyed by output name (`eDP-1=2;HDMI-1=1;`). */
        std::map<std::string, float> named;
        /** @brief Factors in screen order, when the list names no outputs (`2;1;`). */
        std::vector<float> positional;
    };

    /**
     * @brief Parses `QT_SCREEN_SCALE_FACTORS`.
     *
     * @param value The variable's value.
     * @return The factors; unusable entries are dropped, not guessed at.
     */
    [[nodiscard]] X11ScreenScaleFactors ParseScreenScaleFactors(std::string_view value);

    /**
     * @brief The content scale of the session, and of each monitor where the session sets one.
     *
     * X11 has no authoritative scale (plans/plan_x11.md D10); these are the settings a session
     * actually makes, read in SDL3's order so the two backends agree on one desktop: `Xft.dpi` from
     * the live `RESOURCE_MANAGER`, then the XSETTINGS manager's `Gdk/WindowScalingFactor` and
     * `Xft/DPI`, then `GDK_SCALE`, then 1. On top of that global scale, KDE's per-screen
     * `QT_SCREEN_SCALE_FACTORS` gives monitors their own (X11-0156) -- the one per-monitor scale an
     * X11 desktop sets.
     *
     * A content scale is a preference for sizing an interface, not a pixel density: X11 has one
     * coordinate space, so a window's pixels are its logical units and its display scale is 1.
     *
     * Changes are followed: a new `RESOURCE_MANAGER`, the XSETTINGS manager's property, a manager
     * that goes away or a new one taking over.
     */
    class X11ContentScale
    {
    public:
        /**
         * @brief Reads the scale for one connection, and starts watching it.
         *
         * @param connection The connection.
         */
        explicit X11ContentScale(X11Connection& connection);

        /** @brief Gets the session's scale. @return The scale; 1 when nothing states one. */
        [[nodiscard]] float Global() const { return global_; }

        /**
         * @brief Gets one monitor's scale.
         *
         * @param outputName The monitor's RandR name.
         * @param index The monitor's position in the display list.
         * @return The monitor's own factor where the session sets one, else the session's scale.
         */
        [[nodiscard]] float ForMonitor(const std::string& outputName, std::size_t index) const;

        /** @brief Gets whether any monitor has a scale of its own. @return True when some do. */
        [[nodiscard]] bool HasPerMonitorScales() const
        {
            return !screens_.named.empty() || !screens_.positional.empty();
        }

        /**
         * @brief Follows a change to one of the sources, if the event is one.
         *
         * @param event Any event.
         * @return True when the scale changed.
         */
        bool HandleEvent(const XEvent& event);

    private:
        void FindSettingsManager();
        [[nodiscard]] float Read() const;

        X11Connection& connection_;
        Atom resourceManager_ = kNone;
        Atom settingsSelection_ = kNone;
        Atom settingsProperty_ = kNone;
        Atom manager_ = kNone;
        ::Window settingsOwner_ = kNone;
        X11ScreenScaleFactors screens_;
        float global_ = 1.0f;
    };

} // namespace CNA::Platform::X11
