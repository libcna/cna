// SPDX-License-Identifier: MS-PL

#include "X11EventMapper.hpp"

namespace CNA::Platform::X11 {

    WheelDirection ClassifyWheelButton(const unsigned int button)
    {
        switch (button)
        {
            case 4: return WheelDirection::Up;
            case 5: return WheelDirection::Down;
            case 6: return WheelDirection::Left;
            case 7: return WheelDirection::Right;
            default: return WheelDirection::None;
        }
    }

    std::uint8_t MapButtonNumber(const unsigned int button)
    {
        if (button >= 1 && button <= 3)
        {
            return static_cast<std::uint8_t>(button);
        }
        if (button >= 8)
        {
            // 8 -> 4, 9 -> 5, and so on: the four wheel numbers are removed from the sequence
            // rather than left as a hole.
            const unsigned int mapped = button - 4;
            return mapped <= 255u ? static_cast<std::uint8_t>(mapped) : static_cast<std::uint8_t>(0);
        }
        return 0;
    }

    bool IsRealFocusChange(const int mode, const int detail)
    {
        if (mode == NotifyGrab || mode == NotifyUngrab)
        {
            return false;
        }
        if (detail == NotifyInferior || detail == NotifyPointer ||
            detail == NotifyPointerRoot || detail == NotifyDetailNone)
        {
            return false;
        }
        return true;
    }

    bool IsAutoRepeatPair(const XKeyEvent& release, const XEvent& next)
    {
        return next.type == KeyPress && next.xkey.keycode == release.keycode &&
               next.xkey.time == release.time && next.xkey.window == release.window;
    }

} // namespace CNA::Platform::X11
