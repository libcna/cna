// SPDX-License-Identifier: MS-PL

#include "TerminalMouse.hpp"

#include "TerminalKeyboard.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <utility>

namespace CNA::Platform::Terminal {

    TerminalMouse::TerminalMouse(std::shared_ptr<TerminalInputDecoder> decoder)
        : decoder_(std::move(decoder))
    {
    }

    void TerminalMouse::Update() { decoder_->PumpMouse(); }

    const MouseSnapshot& TerminalMouse::GetSnapshot() const
    {
        return decoder_->GetMouseSnapshot();
    }

    MouseDelta TerminalMouse::ConsumeRelativeDelta() { return {}; }

    void TerminalMouse::SetPosition(const WindowId window, const int x, const int y)
    {
        (void)window;
        (void)x;
        (void)y;
        throw PlatformException("TerminalMouse::SetPosition",
                                "SGR-1006 reports the pointer but cannot warp it");
    }

    void TerminalMouse::SetCursorVisible(const bool visible)
    {
        (void)visible;
        throw PlatformException("TerminalMouse::SetCursorVisible",
                                "a terminal cannot control its emulator's graphical pointer");
    }

    void TerminalMouse::SetCursor(const SystemCursor cursor)
    {
        (void)cursor;
        throw PlatformNotSupportedException(PlatformCapability::CursorShapes, "Terminal");
    }

    void TerminalMouse::SetRelativeMode(const WindowId window, const bool enabled)
    {
        (void)window;
        (void)enabled;
        throw PlatformNotSupportedException(PlatformCapability::RelativeMouse, "Terminal");
    }

    bool TerminalMouse::IsRelativeMode() const { return false; }

    bool TerminalMouse::SetCapture(const bool enabled)
    {
        (void)enabled;
        throw PlatformNotSupportedException(PlatformCapability::GlobalPointer, "Terminal");
    }

    bool TerminalMouse::TryGetGlobalPosition(float& x, float& y) const
    {
        (void)x;
        (void)y;
        throw PlatformNotSupportedException(PlatformCapability::GlobalPointer, "Terminal");
    }

    bool TerminalMouse::SetGlobalPosition(const float x, const float y)
    {
        (void)x;
        (void)y;
        throw PlatformNotSupportedException(PlatformCapability::GlobalPointer, "Terminal");
    }

} // namespace CNA::Platform::Terminal
