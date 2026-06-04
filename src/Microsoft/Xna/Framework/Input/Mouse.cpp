#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "CNA/Internal/Input/InputManager.hpp"

namespace Microsoft::Xna::Framework::Input
{
    std::uintptr_t           Mouse::WindowHandle            = 0;
    std::function<void(int)> Mouse::ClickedEXT              = nullptr;
    bool                     Mouse::IsRelativeMouseModeEXT  = false;
    int                      Mouse::INTERNAL_WindowWidth     = 0;
    int                      Mouse::INTERNAL_WindowHeight    = 0;
    int                      Mouse::INTERNAL_BackBufferWidth  = 0;
    int                      Mouse::INTERNAL_BackBufferHeight = 0;
    int                      Mouse::INTERNAL_MouseWheel       = 0;

    MouseState Mouse::GetState()
    {
        return CNA::Internal::Input::InputManager::GetMouseState();
    }

    void Mouse::SetPosition(int x, int y)
    {
        CNA::Internal::Input::InputManager::SetMousePosition(x, y);
    }

    void Mouse::SetCursor(MouseCursor cursor)
    {
        (void)cursor;
    }

    void Mouse::INTERNAL_onClicked(int button)
    {
        if (ClickedEXT)
            ClickedEXT(button);
    }
}
