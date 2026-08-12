// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"

#include <cstdint>

namespace CNA::Platform {

    /** @brief A mouse snapshot taken at one instant. */
    struct MouseSnapshot
    {
        /** @brief Window whose client coordinates x/y use; zero when there is no associated window. */
        WindowId window = 0;
        /** @brief Pointer x in client coordinates of the focused window. */
        int x = 0;
        /** @brief Pointer y in client coordinates of the focused window. */
        int y = 0;
        /** @brief Accumulated horizontal scroll in XNA units (120 per whole notch). */
        int scrollX = 0;
        /** @brief Accumulated vertical scroll in XNA units (120 per whole notch). */
        int scrollY = 0;
        /**
         * @brief Bitmask of held buttons.
         *
         * Bits 0..4 are left, middle, right, X1 and X2 respectively. No native button mask may
         * pass through this field.
         */
        std::uint8_t buttons = 0;
    };

    /** @brief Relative pointer motion accumulated since the previous public read. */
    struct MouseDelta
    {
        /** @brief Horizontal displacement. */
        int x = 0;
        /** @brief Vertical displacement. */
        int y = 0;
    };

    /** @brief A cursor shape the platform can display. */
    enum class SystemCursor
    {
        /** @brief The default arrow. */
        Arrow,
        /** @brief A text I-beam. */
        IBeam,
        /** @brief A busy indicator. */
        Wait,
        /** @brief A precision crosshair. */
        Crosshair,
        /** @brief A move/drag indicator. */
        Move,
        /** @brief A "not allowed" indicator. */
        NotAllowed,
        /** @brief A clickable-element pointer. */
        Pointer
    };

    /**
     * @brief Reads mouse state and controls the cursor.
     *
     * `PixelAccurateMouse` reports whether positions are true pixels. A platform whose pointer
     * resolution is coarser — a terminal reports in character cells — says so rather than
     * implying a precision it does not have.
     */
    class IPlatformMouse
    {
    public:
        /** @brief Destroys the service. */
        virtual ~IPlatformMouse() = default;

        /**
         * @brief Updates pollable state and relative displacement after the frame's event pump.
         *
         * Wheel totals may be event-fed by the implementation because native APIs do not expose
         * them as pollable state; Update must preserve those cumulative totals.
         */
        virtual void Update() = 0;

        /**
         * @brief Gets the most recent snapshot.
         *
         * @return The state as of the last Update().
         */
        [[nodiscard]] virtual const MouseSnapshot& GetSnapshot() const = 0;

        /**
         * @brief Returns and clears accumulated relative motion.
         *
         * Absolute position, buttons and wheel remain ordinary frame snapshots. Relative mode is
         * the deliberate exception: FNA/XNA-compatible callers consume displacement on each
         * `Mouse::GetState()` read, so a second read with no intervening motion returns (0, 0).
         *
         * @return Motion accumulated since the previous call, or zero while relative mode is off.
         */
        [[nodiscard]] virtual MouseDelta ConsumeRelativeDelta() = 0;

        /**
         * @brief Warps the pointer to a position within a window.
         *
         * @param window The window to position within; zero records the position without warping.
         * @param x Target x in client coordinates.
         * @param y Target y in client coordinates.
         */
        virtual void SetPosition(WindowId window, int x, int y) = 0;

        /**
         * @brief Sets whether the cursor is visible.
         *
         * @param visible True to show the cursor.
         */
        virtual void SetCursorVisible(bool visible) = 0;

        /**
         * @brief Sets the cursor shape.
         *
         * @param cursor The shape to display.
         * @throws PlatformNotSupportedException If the platform reports no `CursorShapes` capability.
         */
        virtual void SetCursor(SystemCursor cursor) = 0;

        /**
         * @brief Enables or disables relative (pointer-locked) mode.
         *
         * @param window The window to capture; zero when no window is available.
         * @param enabled True to capture the pointer and report motion deltas only.
         * @throws PlatformNotSupportedException If the platform reports no `RelativeMouse` capability.
         */
        virtual void SetRelativeMode(WindowId window, bool enabled) = 0;

        /**
         * @brief Gets whether relative mode is active.
         *
         * @return True if the pointer is captured.
         */
        [[nodiscard]] virtual bool IsRelativeMode() const = 0;

        // --- Desktop-space pointer ---------------------------------------------------------
        //
        // Everything above is scoped to a window. These three are not, and the difference is
        // what makes a drag survive the pointer leaving the window: a game tracking a slider
        // keeps receiving positions while the button is held, even out over the desktop.
        //
        // Kept separate rather than folded into the window-scoped calls because a platform can
        // genuinely have one and not the other. A terminal or a console has exactly one
        // full-screen surface and no desktop to have coordinates in.

        /**
         * @brief Captures the pointer so motion is reported while it is outside any window.
         *
         * @param enabled True to capture, false to release.
         * @return True if the platform accepted the change.
         * @throws PlatformNotSupportedException If the platform reports no `GlobalPointer`
         * capability.
         */
        virtual bool SetCapture(bool enabled) = 0;

        /**
         * @brief Reads the pointer position in desktop coordinates.
         *
         * A status rather than a throw when the position is unavailable: a pointer that has not
         * moved yet, or a platform that only reports position while a window has focus, is an
         * ordinary answer a caller branches on.
         *
         * @param x Receives the desktop x position; untouched when this returns false.
         * @param y Receives the desktop y position; untouched when this returns false.
         * @return True if a position was available.
         * @throws PlatformNotSupportedException If the platform reports no `GlobalPointer`
         * capability.
         */
        [[nodiscard]] virtual bool TryGetGlobalPosition(float& x, float& y) const = 0;

        /**
         * @brief Warps the pointer to a position in desktop coordinates.
         *
         * @param x Target desktop x.
         * @param y Target desktop y.
         * @return True if the platform accepted the warp.
         * @throws PlatformNotSupportedException If the platform reports no `GlobalPointer`
         * capability.
         */
        virtual bool SetGlobalPosition(float x, float y) = 0;
    };

} // namespace CNA::Platform
