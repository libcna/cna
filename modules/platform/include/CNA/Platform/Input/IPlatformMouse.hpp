// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>

namespace CNA::Platform {

    class IPlatformWindow;

    /** @brief A mouse snapshot taken at one instant. */
    struct MouseSnapshot
    {
        /** @brief Pointer x in client coordinates of the focused window. */
        int x = 0;
        /** @brief Pointer y in client coordinates of the focused window. */
        int y = 0;
        /** @brief Accumulated horizontal scroll. */
        int scrollX = 0;
        /** @brief Accumulated vertical scroll. */
        int scrollY = 0;
        /** @brief Bitmask of held buttons; bit 0 is left, bit 1 middle, bit 2 right. */
        std::uint8_t buttons = 0;
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

        /** @brief Updates the snapshot from the platform's current state. */
        virtual void Update() = 0;

        /**
         * @brief Gets the most recent snapshot.
         *
         * @return The state as of the last Update().
         */
        [[nodiscard]] virtual const MouseSnapshot& GetSnapshot() const = 0;

        /**
         * @brief Warps the pointer to a position within a window.
         *
         * @param window The window to position within.
         * @param x Target x in client coordinates.
         * @param y Target y in client coordinates.
         */
        virtual void SetPosition(IPlatformWindow& window, int x, int y) = 0;

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
         * @param enabled True to capture the pointer and report motion deltas only.
         * @throws PlatformNotSupportedException If the platform reports no `RelativeMouse` capability.
         */
        virtual void SetRelativeMode(bool enabled) = 0;

        /**
         * @brief Gets whether relative mode is active.
         *
         * @return True if the pointer is captured.
         */
        [[nodiscard]] virtual bool IsRelativeMode() const = 0;
    };

} // namespace CNA::Platform
