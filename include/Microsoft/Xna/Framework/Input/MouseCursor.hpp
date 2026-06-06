// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#pragma once

#include <SDL3/SDL.h>

namespace Microsoft::Xna::Framework::Input
{
    /// Represents a mouse cursor image.
    ///
    /// Wraps an SDL_Cursor* and provides the standard XNA system cursor constants.
    class MouseCursor
    {
    public:
        /// Creates a default (Arrow) cursor.
        MouseCursor();

        /// Creates a cursor wrapping the given SDL cursor (takes ownership).
        explicit MouseCursor(SDL_Cursor* sdlCursor, bool owning = false);

        MouseCursor(const MouseCursor&)            = delete;
        MouseCursor& operator=(const MouseCursor&) = delete;
        MouseCursor(MouseCursor&&)                 = default;
        MouseCursor& operator=(MouseCursor&&)      = default;

        ~MouseCursor();

        /// Returns the underlying SDL_Cursor pointer (not owned by the caller).
        [[nodiscard]] SDL_Cursor* GetSDLCursor() const { return sdlCursor_; }

        // --- Standard XNA / MonoGame system cursors ---
        static MouseCursor Arrow;
        static MouseCursor Crosshair;
        static MouseCursor Hand;
        static MouseCursor IBeam;
        static MouseCursor No;
        static MouseCursor SizeAll;
        static MouseCursor SizeNESW;
        static MouseCursor SizeNS;
        static MouseCursor SizeNWSE;
        static MouseCursor SizeWE;
        static MouseCursor Wait;
        static MouseCursor WaitCursor;

    private:
        SDL_Cursor* sdlCursor_ = nullptr;
        bool        owning_    = false;

        static MouseCursor MakeSystem(SDL_SystemCursor id);
    };
}
