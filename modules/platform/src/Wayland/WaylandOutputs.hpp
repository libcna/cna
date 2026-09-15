// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSystemServices.hpp"

#include "WaylandProtocols.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Platform::Wayland {

    class WaylandConnection;

    /** @brief What one output says about itself, applied atomically on `wl_output.done`. */
    struct WaylandOutputState
    {
        /** @brief The connector name (`DP-1`), wl_output v4; empty before. */
        std::string name;
        /** @brief The human-readable description, wl_output v4 or xdg-output. */
        std::string description;
        /** @brief Manufacturer, from `geometry`. */
        std::string make;
        /** @brief Model, from `geometry`. */
        std::string model;
        /** @brief Position in the compositor's space, from `geometry`. */
        int x = 0;
        /** @brief Position in the compositor's space, from `geometry`. */
        int y = 0;
        /** @brief Physical width in millimetres; often 0 or fictional. */
        int physicalWidth = 0;
        /** @brief Physical height in millimetres; often 0 or fictional. */
        int physicalHeight = 0;
        /** @brief `wl_output.transform`. */
        int transform = 0;
        /** @brief Current mode width in pixels. */
        int modeWidth = 0;
        /** @brief Current mode height in pixels. */
        int modeHeight = 0;
        /** @brief Current refresh rate in mHz. */
        int refreshMilliHz = 0;
        /** @brief Every mode advertised (v4 compositors usually send only the current one). */
        std::vector<DisplayMode> modes;
        /** @brief `wl_output.scale`: the integer scale, the ceiling of a fractional one. */
        int scale = 1;
        /** @brief Logical position, from xdg-output; valid when hasLogical. */
        int logicalX = 0;
        /** @brief Logical position, from xdg-output. */
        int logicalY = 0;
        /** @brief Logical size, from xdg-output. */
        int logicalWidth = 0;
        /** @brief Logical size, from xdg-output. */
        int logicalHeight = 0;
        /** @brief Whether xdg-output has reported a logical geometry. */
        bool hasLogical = false;
    };

    /**
     * @brief The scale an output's user setting asks for: the mode's pixels per logical unit
     * where xdg-output reports a logical size (1.25 on a 125 % desktop, where `wl_output.scale`
     * says 2), else the integer scale.
     *
     * @param state The output.
     * @return The scale, at least a small positive number.
     */
    [[nodiscard]] float OutputContentScale(const WaylandOutputState& state);

    /** @brief One `wl_output` global and, where the compositor offers it, its `zxdg_output_v1`. */
    class WaylandOutput
    {
    public:
        /**
         * @brief Binds the output.
         * @param registry The registry.
         * @param name The global's name, which is also this display's id.
         * @param version The negotiated version.
         * @param onChanged Called after each `done` that changed something.
         */
        WaylandOutput(wl_registry* registry, std::uint32_t name, std::uint32_t version, std::function<void()> onChanged);

        /** @brief Releases the output (and its xdg-output). */
        ~WaylandOutput();

        WaylandOutput(const WaylandOutput&) = delete;
        WaylandOutput& operator=(const WaylandOutput&) = delete;

        /**
         * @brief Creates the xdg-output once the manager is bound (it may be announced after the
         * output).
         * @param manager The `zxdg_output_manager_v1`, or null.
         */
        void AttachXdgOutput(void* manager);

        /** @brief Gets the global name. @return The name. */
        [[nodiscard]] std::uint32_t GetGlobalName() const { return globalName_; }

        /** @brief Gets the proxy. @return The `wl_output`. */
        [[nodiscard]] wl_output* GetProxy() const { return output_; }

        /** @brief Gets the applied state. @return The state. */
        [[nodiscard]] const WaylandOutputState& GetState() const { return state_; }

        /** @brief Gets whether a first `done` has arrived. @return True once described. */
        [[nodiscard]] bool IsDescribed() const { return described_; }

        /** @brief Describes this output in the contract's terms. @return The display. */
        [[nodiscard]] DisplayInfo ToDisplayInfo() const;

    private:
        static const wl_output_listener kListener;
#if defined(CNA_WAYLAND_HAVE_XDG_OUTPUT)
        static const zxdg_output_v1_listener kXdgListener;
#endif

        void Done();

        std::uint32_t globalName_ = 0;
        std::uint32_t version_ = 0;
        wl_output* output_ = nullptr;
        void* xdgOutput_ = nullptr;
        WaylandOutputState pending_;
        WaylandOutputState state_;
        bool described_ = false;
        bool restartModes_ = false;
        std::function<void()> onChanged_;
    };

    /**
     * @brief The contract's display service over the compositor's outputs (WAYLAND-0040).
     *
     * Wayland has no display-mode switching for ordinary clients: the modes are the ones the
     * compositor advertises (a v4 compositor usually sends only the current one) and the current
     * mode is always the desktop's.
     */
    class WaylandDisplays final : public IPlatformDisplays
    {
    public:
        /** @brief How the display service reaches the outputs and a window's current output. */
        struct Source
        {
            /** @brief Every described output. */
            std::function<std::vector<const WaylandOutput*>()> outputs;
            /** @brief The output a window is on, or null. */
            std::function<const WaylandOutput*(const IPlatformWindow&)> outputForWindow;
            /** @brief Gets whether the screen saver is allowed. */
            std::function<bool()> screenSaverEnabled;
            /** @brief Allows or inhibits the screen saver. */
            std::function<void(bool)> setScreenSaverEnabled;
        };

        /**
         * @brief Creates the service.
         * @param source Where it reads from.
         */
        explicit WaylandDisplays(Source source) : source_(std::move(source)) {}

        /** @brief Gets every output. @return The displays, in announcement order. */
        [[nodiscard]] std::vector<DisplayInfo> GetDisplays() const override;
        /**
         * @brief Gets the display a window is on.
         * @param window The window.
         * @param display Receives it.
         * @return True when the window is on a known output.
         */
        [[nodiscard]] bool TryGetDisplayForWindow(const IPlatformWindow& window, DisplayInfo& display) const override;
        /**
         * @brief Gets a window's safe area: its whole client area (Wayland has no insets protocol).
         * @param window The window.
         * @param safeArea Receives the area.
         * @return True.
         */
        [[nodiscard]] bool TryGetSafeAreaForWindow(const IPlatformWindow& window, WindowBounds& safeArea) const override;
        /**
         * @brief Gets an output's advertised modes.
         * @param displayId The output's global name.
         * @return The modes; empty for an unknown id.
         */
        [[nodiscard]] std::vector<DisplayMode> GetDisplayModes(std::uint32_t displayId) const override;
        /**
         * @brief Gets an output's current mode.
         * @param displayId The output's global name.
         * @param mode Receives it.
         * @return True for a known, described output.
         */
        [[nodiscard]] bool TryGetCurrentDisplayMode(std::uint32_t displayId, DisplayMode& mode) const override;
        /** @brief Gets whether the screen saver may run. @return True unless inhibited. */
        [[nodiscard]] bool IsScreenSaverEnabled() const override;
        /** @brief Allows or inhibits the screen saver (D-26). @param enabled True to allow. */
        void SetScreenSaverEnabled(bool enabled) override;

    private:
        Source source_;
    };

} // namespace CNA::Platform::Wayland
