// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/Input/IPlatformMouse.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <span>

namespace
{
    /// Converts logical game coordinates to the active platform window's client-coordinate
    /// space. The renderer owns presentation geometry (including letterbox offsets); input knows
    /// only the stable WindowId used to find it. With no renderer or transform the coordinates
    /// pass through, which is the correct 1:1/windowless fallback.
    void logical_to_window(const CNA::Platform::WindowId window, const float logX,
                           const float logY, float& outX, float& outY)
    {
        outX = logX;
        outY = logY;

        if (auto* renderer = CNA::Internal::Renderers::IGraphicsRenderer::GetForWindow(window))
        {
            float wx = logX, wy = logY;
            if (renderer->TransformLogicalToWindow(logX, logY, wx, wy))
            {
                outX = wx;
                outY = wy;
            }
        }
    }

    /// Converts platform window-client coordinates back into the logical game coordinates
    /// MouseState exposes. This is the inverse of logical_to_window above and uses the same
    /// renderer selected solely by stable platform identity.
    void window_to_logical(const CNA::Platform::WindowId window,
                           const float windowX, const float windowY, float& outX, float& outY)
    {
        outX = windowX;
        outY = windowY;

        if (auto* renderer = CNA::Internal::Renderers::IGraphicsRenderer::GetForWindow(window))
        {
            float logicalX = windowX;
            float logicalY = windowY;
            if (renderer->TransformWindowToLogical(windowX, windowY, logicalX, logicalY))
            {
                outX = logicalX;
                outY = logicalY;
            }
        }
    }

    [[nodiscard]] CNA::Platform::IPlatformMouse* CurrentMouse()
    {
        return CNA::Platform::GetCurrentPlatform().GetMouse();
    }

    [[nodiscard]] Microsoft::Xna::Framework::Input::ButtonState Button(
        const std::uint8_t buttons, const std::uint8_t bit)
    {
        return (buttons & bit) != 0
            ? Microsoft::Xna::Framework::Input::ButtonState::Pressed
            : Microsoft::Xna::Framework::Input::ButtonState::Released;
    }
}

namespace Microsoft::Xna::Framework::Input
{
    std::uintptr_t Mouse::windowHandle_ = 0;
    std::uint32_t Mouse::windowId_ = 0;
    // DEC-06: ClickedEXT is multicast (MulticastAction<int>), matching FNA's Action<int>.
    System::MulticastAction<int> Mouse::ClickedEXT;

    std::uintptr_t Mouse::getWindowHandleProperty()
    {
        return windowHandle_;
    }

    void Mouse::setWindowHandleProperty(const std::uintptr_t value)
    {
        windowHandle_ = value;
        windowId_ = 0;
        if (value == 0)
        {
            return;
        }

        // WindowHandle is a compatibility token, not necessarily an address. Only the selected
        // platform may interpret it; Mouse retains the resulting stable id and discards the
        // temporary non-owning wrapper immediately.
        try
        {
            const auto window = CNA::Platform::GetCurrentPlatform().AdoptWindowHandle(value);
            windowId_ = window != nullptr ? window->GetId() : 0;
        }
        catch (const CNA::Platform::PlatformException&)
        {
            // Preserve the legacy setter's tolerant invalid/no-window behaviour. The public token
            // still round-trips, while platform operations safely fall back to snapshot identity.
        }
    }

    void Mouse::INTERNAL_setWindow(
        const std::uintptr_t handle, const std::uint32_t windowId)
    {
        windowHandle_ = handle;
        windowId_ = handle != 0 ? windowId : 0;
    }

    // Coordinate model: IPlatformMouse owns a window-client snapshot. The public XNA surface maps
    // that through the active renderer into logical game coordinates, and SetPosition performs the
    // exact inverse before handing the target back to the platform. Wheel totals already use XNA's
    // cumulative 120-unit convention in the contract.

    MouseState Mouse::GetState()
    {
        CNA::Platform::IPlatformMouse* mouse = CurrentMouse();
        if (mouse == nullptr)
        {
            return MouseState();
        }

        const CNA::Platform::MouseSnapshot& snapshot = mouse->GetSnapshot();
        int x = snapshot.x;
        int y = snapshot.y;
        if (mouse->IsRelativeMode())
        {
            const CNA::Platform::MouseDelta delta = mouse->ConsumeRelativeDelta();
            x = delta.x;
            y = delta.y;
        }
        else
        {
            float logicalX = static_cast<float>(snapshot.x);
            float logicalY = static_cast<float>(snapshot.y);
            const CNA::Platform::WindowId window = windowId_ != 0 ? windowId_ : snapshot.window;
            window_to_logical(window, logicalX, logicalY, logicalX, logicalY);
            x = static_cast<int>(logicalX);
            y = static_cast<int>(logicalY);
        }

        return MouseState(x, y, snapshot.scrollY,
                          Button(snapshot.buttons, 0x01),
                          Button(snapshot.buttons, 0x02),
                          Button(snapshot.buttons, 0x04),
                          Button(snapshot.buttons, 0x08),
                          Button(snapshot.buttons, 0x10),
                          snapshot.scrollX);
    }

    void Mouse::SetPosition(int x, int y)
    {
        // In relative mode, this function is meaningless (Mouse.cs:106-110).
        if (getIsRelativeMouseModeEXTProperty())
        {
            return;
        }

        CNA::Platform::IPlatformMouse* mouse = CurrentMouse();
        if (mouse == nullptr)
        {
            return;
        }

        // Convert logical -> window so the cursor lands at the correct physical pixel on a
        // scaled/letterboxed window. A zero WindowId asks the service to record the position but
        // not hand a native API an invalid window.
        const CNA::Platform::WindowId target = windowId_ != 0
            ? windowId_
            : mouse->GetSnapshot().window;
        float windowX = static_cast<float>(x);
        float windowY = static_cast<float>(y);
        logical_to_window(target, static_cast<float>(x), static_cast<float>(y), windowX, windowY);
        mouse->SetPosition(target, static_cast<int>(windowX), static_cast<int>(windowY));
    }

    void Mouse::SetCursor(MouseCursor& cursor)
    {
        CNA::Platform::IPlatformMouse* mouse = CurrentMouse();
        if (mouse == nullptr || cursor.isDisposed_)
        {
            return;
        }

        if (cursor.isCustom_)
        {
            mouse->SetCursor(CNA::Platform::CursorImage{
                cursor.width_, cursor.height_, cursor.originX_, cursor.originY_,
                std::span<const std::uint32_t>(cursor.rgba_)});
            return;
        }

        using PlatformCursor = CNA::Platform::SystemCursor;
        switch (cursor.systemShape_)
        {
            case MouseCursor::ShapeArrow:     mouse->SetCursor(PlatformCursor::Arrow); break;
            case MouseCursor::ShapeCrosshair: mouse->SetCursor(PlatformCursor::Crosshair); break;
            case MouseCursor::ShapeHand:      mouse->SetCursor(PlatformCursor::Pointer); break;
            case MouseCursor::ShapeIBeam:     mouse->SetCursor(PlatformCursor::IBeam); break;
            case MouseCursor::ShapeNo:        mouse->SetCursor(PlatformCursor::NotAllowed); break;
            case MouseCursor::ShapeSizeAll:   mouse->SetCursor(PlatformCursor::Move); break;
            case MouseCursor::ShapeSizeNESW:  mouse->SetCursor(PlatformCursor::NeswResize); break;
            case MouseCursor::ShapeSizeNS:    mouse->SetCursor(PlatformCursor::NsResize); break;
            case MouseCursor::ShapeSizeNWSE:  mouse->SetCursor(PlatformCursor::NwseResize); break;
            case MouseCursor::ShapeSizeWE:    mouse->SetCursor(PlatformCursor::EwResize); break;
            case MouseCursor::ShapeWait:      mouse->SetCursor(PlatformCursor::Wait); break;
            case MouseCursor::ShapeWaitArrow: mouse->SetCursor(PlatformCursor::Progress); break;
        }
    }

    bool Mouse::getIsRelativeMouseModeEXTProperty()
    {
        CNA::Platform::IPlatformMouse* mouse = CurrentMouse();
        return mouse != nullptr && mouse->IsRelativeMode();
    }

    void Mouse::setIsRelativeMouseModeEXTProperty(const bool value)
    {
        CNA::Platform::IPlatformMouse* mouse = CurrentMouse();
        if (mouse == nullptr)
        {
            return;
        }

        const CNA::Platform::WindowId target = windowId_ != 0
            ? windowId_
            : mouse->GetSnapshot().window;
        if (target == 0)
        {
            return;
        }
        mouse->SetRelativeMode(target, value);
    }

    bool Mouse::SetCaptureEXT(const bool enabled)
    {
        CNA::Platform::IPlatformMouse* mouse = CNA::Platform::GetCurrentPlatform().GetMouse();
        return mouse != nullptr && mouse->SetCapture(enabled);
    }

    void Mouse::GetGlobalPositionEXT(int& x, int& y)
    {
        // Zeroed first, so a platform with no desktop pointer answers (0, 0) rather than
        // leaving the caller's own values in place -- the previous native backend always wrote both.
        x = 0;
        y = 0;

        CNA::Platform::IPlatformMouse* mouse = CNA::Platform::GetCurrentPlatform().GetMouse();
        if (mouse == nullptr)
        {
            return;
        }

        float fx = 0.0f;
        float fy = 0.0f;
        if (!mouse->TryGetGlobalPosition(fx, fy))
        {
            return;
        }
        x = static_cast<int>(fx);
        y = static_cast<int>(fy);
    }

    bool Mouse::WarpGlobalEXT(const int x, const int y)
    {
        CNA::Platform::IPlatformMouse* mouse = CNA::Platform::GetCurrentPlatform().GetMouse();
        return mouse != nullptr &&
               mouse->SetGlobalPosition(static_cast<float>(x), static_cast<float>(y));
    }

    void Mouse::INTERNAL_onClicked(int button)
    {
        if (ClickedEXT)
            ClickedEXT(button);
    }

    void Mouse::ResetForTests()
    {
        windowHandle_ = 0;
        windowId_ = 0;
        ClickedEXT    = nullptr;
    }
}
