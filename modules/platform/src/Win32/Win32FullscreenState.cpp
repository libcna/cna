// SPDX-License-Identifier: MS-PL

#include "Win32FullscreenState.hpp"

namespace CNA::Platform::Win32 {

    void Win32FullscreenState::Capture(const LONG_PTR style, const LONG_PTR exStyle,
                                       const WINDOWPLACEMENT& placement)
    {
        if (captured_)
            return;
        style_ = style;
        exStyle_ = exStyle;
        placement_ = placement;
        captured_ = true;
    }

    bool Win32FullscreenState::Restore(LONG_PTR& style, LONG_PTR& exStyle,
                                       WINDOWPLACEMENT& placement)
    {
        if (!captured_)
            return false;
        style = style_;
        exStyle = exStyle_;
        placement = placement_;
        captured_ = false;
        return true;
    }

    LONG_PTR Win32FullscreenState::ToFullscreenStyle(const LONG_PTR baseStyle)
    {
        constexpr LONG_PTR decorations =
            WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_BORDER |
            WS_DLGFRAME;
        return (baseStyle & ~decorations) | WS_POPUP;
    }

    LONG_PTR Win32FullscreenState::ToFullscreenExStyle(const LONG_PTR baseExStyle)
    {
        constexpr LONG_PTR edges =
            WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE;
        return baseExStyle & ~edges;
    }

} // namespace CNA::Platform::Win32
