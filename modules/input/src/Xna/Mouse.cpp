// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/Input/IPlatformMouse.hpp"
#include <SDL3/SDL.h>

namespace
{
    [[nodiscard]] CNA::Platform::WindowId window_id(SDL_Window* window)
    {
        return window != nullptr ? SDL_GetWindowID(window) : 0;
    }

    /// Resolves the window Mouse operates on: the published WindowHandle if set,
    /// otherwise the currently focused window (matches the fallback pattern used
    /// throughout SdlInputBridge.cpp).
    SDL_Window* resolve_mouse_window(const std::uintptr_t windowHandle)
    {
        return windowHandle != 0
            ? reinterpret_cast<SDL_Window*>(windowHandle)
            : SDL_GetMouseFocus();
    }

    /// Converts a point from logical (game/render) coordinates to physical window coordinates —
    /// the inverse of SdlInputBridge::to_logical_position, so SetPosition warps the OS cursor to
    /// the right pixel on a scaled window (plan.md a-0001). Two renderer paths:
    ///   - SDL_Renderer: SDL_RenderCoordinatesToWindow — fully offset-aware, so true-letterbox
    ///     modes (bars) map correctly, including the centering offset (verified, task 858).
    ///   - Other renderers: IGraphicsRenderer::TransformLogicalToWindow — for EasyGL this is a
    ///     uniform height-scale with no offset, which is exact for its FixedHeightDynamicWidth
    ///     model (the logical viewport fills the window; no bars, so no offset).
    /// Falls back to pass-through (window == logical) when no scaling transform is available —
    /// correct when window size == render resolution.
    void logical_to_window(SDL_Window* window, float logX, float logY, float& outX, float& outY)
    {
        outX = logX;
        outY = logY;
        if (window == nullptr)
            return;

        if (SDL_Renderer* renderer = SDL_GetRenderer(window))
        {
            float wx = logX, wy = logY;
            if (SDL_RenderCoordinatesToWindow(renderer, logX, logY, &wx, &wy))
            {
                outX = wx;
                outY = wy;
                return;
            }
        }

        if (auto* renderer = CNA::Internal::Renderers::IGraphicsRenderer::GetForWindow(
                window_id(window)))
        {
            float wx = logX, wy = logY;
            if (renderer->TransformLogicalToWindow(logX, logY, wx, wy))
            {
                outX = wx;
                outY = wy;
            }
        }
    }

    /// Converts the platform contract's window-client coordinates back into the logical game
    /// coordinates MouseState exposes. This is the inverse of logical_to_window above.
    void window_to_logical(SDL_Window* window, const CNA::Platform::WindowId windowId,
                           const float windowX, const float windowY, float& outX, float& outY)
    {
        outX = windowX;
        outY = windowY;

        if (window != nullptr)
        {
            if (SDL_Renderer* renderer = SDL_GetRenderer(window))
            {
                float logicalX = windowX;
                float logicalY = windowY;
                if (SDL_RenderCoordinatesFromWindow(
                        renderer, windowX, windowY, &logicalX, &logicalY))
                {
                    outX = logicalX;
                    outY = logicalY;
                    return;
                }
            }
        }

        if (auto* renderer = CNA::Internal::Renderers::IGraphicsRenderer::GetForWindow(windowId))
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
        windowId_ = window_id(reinterpret_cast<SDL_Window*>(value));
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
            SDL_Window* nativeWindow = resolve_mouse_window(windowHandle_);
            const CNA::Platform::WindowId window = windowId_ != 0 ? windowId_ : snapshot.window;
            window_to_logical(nativeWindow, window, logicalX, logicalY, logicalX, logicalY);
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
        SDL_Window* window = resolve_mouse_window(windowHandle_);
        float windowX = static_cast<float>(x);
        float windowY = static_cast<float>(y);
        logical_to_window(window, static_cast<float>(x), static_cast<float>(y), windowX, windowY);
        const CNA::Platform::WindowId target = windowId_ != 0
            ? windowId_
            : mouse->GetSnapshot().window;
        mouse->SetPosition(target, static_cast<int>(windowX), static_cast<int>(windowY));
    }

    void Mouse::SetCursor(MouseCursor& cursor)
    {
        // Guard against a disposed or empty cursor (GetSDLCursor() == nullptr): SDL_SetCursor(NULL)
        // does NOT clear the cursor — it forces a redraw of the *current* cursor — so passing a
        // disposed cursor through would silently keep the old cursor while looking like it changed.
        // No-op instead, matching MonoGame's guard against an invalid cursor (it throws on a null
        // MouseCursor; CNA takes a reference so only the disposed-handle case is reachable here).
        SDL_Cursor* handle = cursor.GetSDLCursor();
        if (handle == nullptr)
        {
            return;
        }
        SDL_SetCursor(handle);
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
        // leaving the caller's own values in place -- the previous SDL call always wrote both.
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
