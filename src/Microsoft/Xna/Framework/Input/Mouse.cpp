// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
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
}

namespace Microsoft::Xna::Framework::Input
{
    std::uintptr_t           Mouse::windowHandle_           = 0;
    std::function<void(int)> Mouse::ClickedEXT              = nullptr;

    std::uintptr_t Mouse::getWindowHandleProperty()
    {
        return windowHandle_;
    }

    void Mouse::setWindowHandleProperty(const std::uintptr_t value)
    {
        windowHandle_ = value;
    }

    // Intentional deviation from Mouse.cs: FNA scales GetState()/SetPosition
    // coordinates by a fixed INTERNAL_WindowWidth/Height <-> INTERNAL_BackBufferWidth/
    // Height ratio (its "faux-backbuffer"). CNA solves the equivalent window<->logical
    // coordinate problem more generally: SdlInputBridge's to_logical_position() already
    // converts every mouse position CNA stores via IGraphicsBackend::TransformWindowToLogical
    // (or SDL_RenderCoordinatesFromWindow for the SDL_Renderer backend) before it reaches
    // InputManager, so GetState() needs no extra scaling. There is currently no inverse
    // (logical -> window) transform on IGraphicsBackend, so SetPosition passes coordinates
    // straight through to SDL_WarpMouseInWindow as window-space; this is correct when the
    // window size matches the logical/render resolution and will be off by the letterbox
    // scale factor otherwise. Adding a general inverse transform is a graphics-layer change
    // (IGraphicsBackend + all backend implementations), out of scope for this input branch.
    // FNA's separate INTERNAL_MouseWheel accumulator (SDL3_FNAPlatform.cs) has no CNA
    // equivalent field either, for the same reason ScrollWheelValue already needs no local
    // mirror here: InputManager::AddScrollWheelDelta already accumulates it cumulatively.

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

        CNA::Internal::Input::InputManager::SetMousePosition(x, y);
        SDL_WarpMouseInWindow(resolve_mouse_window(windowHandle_), static_cast<float>(x), static_cast<float>(y));
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
}
