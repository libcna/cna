// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Input/InputManager.hpp"
#include <SDL3/SDL.h>

namespace
{
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
    /// the right pixel on a scaled window (plan.md a-0001). Two backend paths:
    ///   - SDL_Renderer: SDL_RenderCoordinatesToWindow — fully offset-aware, so true-letterbox
    ///     modes (bars) map correctly, including the centering offset (verified, task 858).
    ///   - Other backends: IGraphicsBackend::TransformLogicalToWindow — for EasyGL this is a
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

        if (auto* backend = CNA::Internal::Backends::IGraphicsBackend::GetForWindow(window))
        {
            float wx = logX, wy = logY;
            if (backend->TransformLogicalToWindow(logX, logY, wx, wy))
            {
                outX = wx;
                outY = wy;
            }
        }
    }
}

namespace Microsoft::Xna::Framework::Input
{
    std::uintptr_t           Mouse::windowHandle_           = 0;
    // DEC-06: ClickedEXT is multicast (MulticastAction<int>), matching FNA's Action<int>.
    System::MulticastAction<int> Mouse::ClickedEXT;

    std::uintptr_t Mouse::getWindowHandleProperty()
    {
        return windowHandle_;
    }

    void Mouse::setWindowHandleProperty(const std::uintptr_t value)
    {
        windowHandle_ = value;
    }

    // Coordinate model: FNA scales GetState()/SetPosition by a fixed INTERNAL_WindowWidth/Height
    // <-> INTERNAL_BackBufferWidth/Height ratio (its "faux-backbuffer"). CNA solves the equivalent
    // window<->logical problem more generally through the graphics backend: SdlInputBridge's
    // to_logical_position() converts stored positions window->logical (so GetState() returns
    // logical coords), and SetPosition() below converts the caller's logical coords back to window
    // space via logical_to_window() before warping (plan.md a-0001). FNA's separate
    // INTERNAL_MouseWheel accumulator has no CNA equivalent field: InputManager::AddScrollWheelDelta
    // already accumulates ScrollWheelValue cumulatively.

    MouseState Mouse::GetState()
    {
        return CNA::Internal::Input::InputManager::GetMouseState();
    }

    void Mouse::SetPosition(int x, int y)
    {
        // In relative mode, this function is meaningless (Mouse.cs:99-103).
        if (getIsRelativeMouseModeEXTProperty())
        {
            return;
        }

        // Keep GetState() reporting the logical position the caller set...
        CNA::Internal::Input::InputManager::SetMousePosition(x, y);

        // ...but warp the OS cursor in window space: convert logical -> window so the cursor lands
        // at the correct physical pixel on a scaled/letterboxed window (a-0001).
        SDL_Window* window = resolve_mouse_window(windowHandle_);
        float windowX, windowY;
        logical_to_window(window, static_cast<float>(x), static_cast<float>(y), windowX, windowY);
        SDL_WarpMouseInWindow(window, windowX, windowY);
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
        // Safe with no window: SDL's behavior with a null window is undefined, so report false.
        SDL_Window* window = resolve_mouse_window(windowHandle_);
        if (window == nullptr)
        {
            return false;
        }
        // DEC-14: read SDL live at the API boundary (no cache); InputManager keeps its own flag,
        // written by the setter, to gate relative-delta accumulation without depending on SDL.
        return SDL_GetWindowRelativeMouseMode(window);
    }

    void Mouse::setIsRelativeMouseModeEXTProperty(const bool value)
    {
        // Safe with no window: no-op rather than passing a null window to SDL. Without a window
        // there are no motion events to accumulate, so the InputManager mode is left untouched too.
        SDL_Window* window = resolve_mouse_window(windowHandle_);
        if (window == nullptr)
        {
            return;
        }
        SDL_SetWindowRelativeMouseMode(window, value);
        CNA::Internal::Input::InputManager::SetMouseRelativeMode(value);
    }

    void Mouse::INTERNAL_onClicked(int button)
    {
        if (ClickedEXT)
            ClickedEXT(button);
    }

    void Mouse::ResetForTests()
    {
        windowHandle_ = 0;
        ClickedEXT    = nullptr;
    }
}
