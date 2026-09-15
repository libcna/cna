// SPDX-License-Identifier: MS-PL
#pragma once

#include <cmath>
#include <cstdint>
#include <span>

namespace CNA::Platform::Wayland {

    /** @brief How a window's buffer relates to its surface (plans/plan_wayland.md D-13). */
    enum class ScaleMethod
    {
        /** @brief One buffer pixel per logical unit; the compositor scales if it must. */
        Unscaled,
        /** @brief A buffer of round(logical × scale) pixels shown at the logical size through `wp_viewport`. */
        Viewport,
        /** @brief `wl_surface.set_buffer_scale(n)` with a buffer of logical × n pixels. */
        BufferScale
    };

    /** @brief What is known about a window's scale, from the compositor and the application. */
    struct ScaleInputs
    {
        /** @brief Whether the application opted into high-DPI drawables (`WindowDescription::highDpi`). */
        bool highDpi = false;
        /** @brief Whether `wp_viewporter` is bound. */
        bool viewporter = false;
        /** @brief `wp_fractional_scale_v1.preferred_scale` in 120ths, or 0 until one arrives. */
        std::uint32_t fractional120 = 0;
        /** @brief The integer scale: `wl_surface.preferred_buffer_scale`, else the largest of the outputs the surface is on. */
        int integer = 1;
    };

    /** @brief The decision derived from ScaleInputs. */
    struct ScaleDecision
    {
        /** @brief How the buffer is presented. */
        ScaleMethod method = ScaleMethod::Unscaled;
        /** @brief Pixels per logical unit; exactly 1 when Unscaled. */
        double scale = 1.0;
        /** @brief The value for `wl_surface.set_buffer_scale` (1 unless BufferScale). */
        int bufferScale = 1;
    };

    /**
     * @brief Decides how a window is scaled.
     *
     * The viewport carries every scale where it exists, integer ones included: an integer
     * `set_buffer_scale` makes a buffer whose size is not a multiple of the scale a fatal protocol
     * error (`wl_surface.invalid_size`), and an EGL buffer one frame behind a scale change is
     * exactly such a buffer. `set_buffer_scale` is the fallback only for a compositor without a
     * viewporter.
     *
     * @param inputs What is known.
     * @return The decision.
     */
    [[nodiscard]] inline ScaleDecision DecideScale(const ScaleInputs& inputs)
    {
        ScaleDecision decision;
        if (!inputs.highDpi)
        {
            return decision;
        }
        const int integer = inputs.integer < 1 ? 1 : inputs.integer;
        if (inputs.viewporter)
        {
            decision.scale = inputs.fractional120 > 0 ? static_cast<double>(inputs.fractional120) / 120.0
                                                      : static_cast<double>(integer);
            decision.method = decision.scale == 1.0 ? ScaleMethod::Unscaled : ScaleMethod::Viewport;
            return decision;
        }
        decision.scale = static_cast<double>(integer);
        decision.bufferScale = integer;
        decision.method = integer == 1 ? ScaleMethod::Unscaled : ScaleMethod::BufferScale;
        return decision;
    }

    /**
     * @brief The buffer size for a logical size at a scale.
     *
     * fractional-scale-v1: the buffer is the surface size times the scale, rounded to the nearest
     * integer, halfway cases away from zero -- `std::lround`.
     *
     * @param logical The logical extent.
     * @param scale Pixels per logical unit.
     * @return The pixel extent; at least 1 for a positive logical extent.
     */
    [[nodiscard]] inline int ScaledExtent(const int logical, const double scale)
    {
        if (logical <= 0)
        {
            return 0;
        }
        const long pixels = std::lround(static_cast<double>(logical) * scale);
        return pixels < 1 ? 1 : static_cast<int>(pixels);
    }

    /** @brief The states an `xdg_toplevel.configure` carries, as flags. */
    struct ToplevelStates
    {
        /** @brief `maximized`. */
        bool maximized = false;
        /** @brief `fullscreen`. */
        bool fullscreen = false;
        /** @brief `resizing`: an interactive resize is in progress. */
        bool resizing = false;
        /** @brief `activated`: the compositor's active window. */
        bool activated = false;
        /** @brief Any of `tiled_left/right/top/bottom` (xdg_wm_base v2). */
        bool tiled = false;
        /** @brief `suspended` (xdg_wm_base v6): not visible, e.g. minimized. */
        bool suspended = false;
    };

    /**
     * @brief Reads the state array of an `xdg_toplevel.configure`.
     *
     * Values this backend does not know (a later protocol version's) are ignored, as the protocol
     * requires of a client.
     *
     * @param states The `xdg_toplevel_state` values.
     * @return The flags.
     */
    [[nodiscard]] inline ToplevelStates ParseToplevelStates(const std::span<const std::uint32_t> states)
    {
        ToplevelStates parsed;
        for (const std::uint32_t state : states)
        {
            switch (state)
            {
                case 1: parsed.maximized = true; break;   // XDG_TOPLEVEL_STATE_MAXIMIZED
                case 2: parsed.fullscreen = true; break;  // XDG_TOPLEVEL_STATE_FULLSCREEN
                case 3: parsed.resizing = true; break;    // XDG_TOPLEVEL_STATE_RESIZING
                case 4: parsed.activated = true; break;   // XDG_TOPLEVEL_STATE_ACTIVATED
                case 5: case 6: case 7: case 8: parsed.tiled = true; break;  // TILED_LEFT..BOTTOM
                case 9: parsed.suspended = true; break;   // XDG_TOPLEVEL_STATE_SUSPENDED
                default: break;
            }
        }
        return parsed;
    }

    /** @brief A logical content size. */
    struct LogicalSize
    {
        /** @brief Width. */
        int width = 0;
        /** @brief Height. */
        int height = 0;
    };

    /**
     * @brief The content size a window takes after a configure (plans/plan_wayland.md D-9).
     *
     * A zero dimension leaves the choice to the client, which keeps what it has. A floating
     * window's non-zero size is a suggestion bounded by the window's own limits; a constrained
     * window (maximized, fullscreen, tiled) is told its size and takes it. The frame a built-in
     * decoration adds is outside the content and is taken off first.
     *
     * @param configuredWidth The configure's width (window geometry), or 0.
     * @param configuredHeight The configure's height (window geometry), or 0.
     * @param states The configure's states.
     * @param current The content size now.
     * @param frameHeight The height a client-side title bar takes from the geometry, or 0.
     * @param minimum The smallest content size allowed, 0 per axis for none.
     * @param maximum The largest content size allowed, 0 per axis for none.
     * @return The content size to take.
     */
    [[nodiscard]] inline LogicalSize ResolveConfigureSize(const int configuredWidth, const int configuredHeight,
                                                          const ToplevelStates& states, const LogicalSize current,
                                                          const int frameHeight, const LogicalSize minimum,
                                                          const LogicalSize maximum)
    {
        LogicalSize size = current;
        if (configuredWidth > 0)
        {
            size.width = configuredWidth;
        }
        if (configuredHeight > 0)
        {
            size.height = configuredHeight - frameHeight;
        }
        const bool constrained = states.maximized || states.fullscreen || states.tiled;
        if (!constrained)
        {
            if (minimum.width > 0 && size.width < minimum.width) { size.width = minimum.width; }
            if (minimum.height > 0 && size.height < minimum.height) { size.height = minimum.height; }
            if (maximum.width > 0 && size.width > maximum.width) { size.width = maximum.width; }
            if (maximum.height > 0 && size.height > maximum.height) { size.height = maximum.height; }
        }
        if (size.width < 1) { size.width = 1; }
        if (size.height < 1) { size.height = 1; }
        return size;
    }

} // namespace CNA::Platform::Wayland
