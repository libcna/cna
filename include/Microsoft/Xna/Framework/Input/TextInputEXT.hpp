// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace Microsoft::Xna::Framework::Input
{
    /// Provides text input events and on-screen keyboard control (FNA extension).
    class TextInputEXT
    {
    public:
        TextInputEXT() = delete;

        /// Raised for each printable character produced by the keyboard.
        static std::function<void(char)> TextInput;

        /// Raised during IME composition with draft text, start offset, and length.
        static std::function<void(const std::string&, int, int)> TextEditing;

        /// Native window handle used by text input APIs.
        static std::uintptr_t WindowHandle;

        /// Returns true if text input mode is currently active.
        static bool IsTextInputActive();

        /// Returns true if the on-screen keyboard is currently visible.
        static bool IsScreenKeyboardShown();

        /// Returns true if the on-screen keyboard is visible for the given native window.
        static bool IsScreenKeyboardShown(std::uintptr_t window);

        /// Activates text input mode and shows the on-screen keyboard if applicable.
        static void StartTextInput();

        /// Deactivates text input mode.
        static void StopTextInput();

        /// Hints to the platform where text is being entered (for IME popup placement).
        static void SetInputRectangle(const Microsoft::Xna::Framework::Rectangle& rectangle);

        // Internal dispatch helpers — called by the SDL event bridge.
        static void INTERNAL_OnTextInput(char c);
        static void INTERNAL_OnTextEditing(const std::string& text, int start, int length);
    };
}
