// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"
#include "X11Headers.hpp"

#include <cstdint>
#include <vector>

namespace CNA::Platform::X11 {

    /**
     * @brief The wheel direction an X button number encodes, if any.
     *
     * X's core protocol has no scroll axis. A traditional mouse reports a wheel notch as a press
     * and release of button 4, 5, 6 or 7, and those four numbers are the entire vocabulary.
     */
    enum class WheelDirection
    {
        /** @brief Not a wheel button. */
        None,
        /** @brief Button 4: one notch up. */
        Up,
        /** @brief Button 5: one notch down. */
        Down,
        /** @brief Button 6: one notch left. */
        Left,
        /** @brief Button 7: one notch right. */
        Right
    };

    /**
     * @brief Classifies an X button number as a wheel direction.
     *
     * @param button The X button number from a `ButtonPress` or `ButtonRelease`.
     * @return The direction, or `WheelDirection::None` for a real button.
     */
    [[nodiscard]] WheelDirection ClassifyWheelButton(unsigned int button);

    /**
     * @brief Maps an X button number onto CNA's button numbering.
     *
     * CNA numbers buttons 1 = left, 2 = middle, 3 = right and then counts upward. X uses the same
     * first three and then spends 4-7 on the wheel, so its first extra button is 8. Passing that
     * through unchanged would leave CNA with a gap where the wheel used to be and report the back
     * button as 8; renumbering closes the gap.
     *
     * @param button The X button number.
     * @return The CNA button number, or zero when @p button is a wheel button.
     */
    [[nodiscard]] std::uint8_t MapButtonNumber(unsigned int button);

    /**
     * @brief Decides whether a focus change is a real one or a side effect of a grab.
     *
     * X delivers `FocusIn`/`FocusOut` with `NotifyGrab` and `NotifyUngrab` modes whenever a grab
     * is taken or released — a menu opening, a window being dragged, this backend's own pointer
     * grab for relative mouse mode. None of those means the user changed windows, and reporting
     * them would make a game pause every time the player opened a menu.
     *
     * `NotifyInferior` detail is filtered for a related reason: it means focus moved between this
     * window and a child of it, and the window as a whole did not lose focus.
     *
     * @param mode The event's `mode` field.
     * @param detail The event's `detail` field.
     * @return True when this is a focus change CNA should report.
     */
    [[nodiscard]] bool IsRealFocusChange(int mode, int detail);

    /**
     * @brief Decides whether two consecutive key events are one auto-repeat.
     *
     * Without detectable auto-repeat the server sends a held key as a `KeyRelease` immediately
     * followed by a `KeyPress` for the same keycode at the *same* timestamp. Emitting the release
     * would tell the game the player let go of the movement key several times a second.
     *
     * The timestamp equality is what makes this safe: a human cannot release and re-press a key
     * inside one server millisecond, so a genuine double-tap is never coalesced.
     *
     * @param release The `KeyRelease` being considered.
     * @param next The event that immediately follows it in the queue.
     * @return True when @p next is the auto-repeat press matching @p release.
     */
    [[nodiscard]] bool IsAutoRepeatPair(const XKeyEvent& release, const XEvent& next);

} // namespace CNA::Platform::X11
