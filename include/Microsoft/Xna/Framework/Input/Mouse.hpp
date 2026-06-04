#pragma once

#include "Microsoft/Xna/Framework/Input/MouseCursor.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "CNA/CNAHelper.hpp"

#include <cstdint>
#include <functional>

namespace Microsoft::Xna::Framework::Input
{
    /// Allows reading position and button state from the mouse.
    class Mouse
    {
    public:
        Mouse() = delete;

        /// Gets or sets the native window handle used for mouse state queries.
        NOXNA static std::uintptr_t WindowHandle;

        /// Returns a snapshot of the current mouse state.
        static MouseState GetState();

        /// Sets the mouse cursor's position relative to the game window.
        static void SetPosition(int x, int y);

        /// Sets the mouse cursor image.
        static void SetCursor(MouseCursor cursor);

        // FNA extension: fires when a mouse button is clicked.
        NOXNA static std::function<void(int)> ClickedEXT;

        // Internal scaling values (set by GraphicsDeviceManager).
        NOXNA static int INTERNAL_WindowWidth;
        NOXNA static int INTERNAL_WindowHeight;
        NOXNA static int INTERNAL_BackBufferWidth;
        NOXNA static int INTERNAL_BackBufferHeight;
        NOXNA static int INTERNAL_MouseWheel;

        NOXNA static void INTERNAL_onClicked(int button);
    };
}
