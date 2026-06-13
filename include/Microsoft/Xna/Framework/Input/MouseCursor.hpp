// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#pragma once

#include <SDL3/SDL.h>

namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Represents a mouse cursor image.
     *
     * Wraps an SDL_Cursor* and provides the standard XNA system cursor constants.
     */
    class MouseCursor
    {
    public:
        /** @brief Creates a default Arrow cursor. */
        MouseCursor();

        /**
         * @brief Creates a cursor wrapping the given SDL cursor.
         * @param sdlCursor The SDL cursor to wrap.
         * @param owning If true, this object takes ownership of the SDL cursor.
         */
        explicit MouseCursor(SDL_Cursor* sdlCursor, bool owning = false);

        MouseCursor(const MouseCursor&)            = delete;
        MouseCursor& operator=(const MouseCursor&) = delete;
        MouseCursor(MouseCursor&&)                 = default;
        MouseCursor& operator=(MouseCursor&&)      = default;

        /** @brief Destructor; releases the SDL cursor if owned. */
        ~MouseCursor();

        /**
         * @brief Returns the underlying SDL_Cursor pointer (not owned by the caller).
         * @return The SDL_Cursor pointer.
         */
        [[nodiscard]] SDL_Cursor* GetSDLCursor() const { return sdlCursor_; }

        /** @brief Standard arrow cursor. */
        static MouseCursor Arrow;
        /** @brief Crosshair cursor. */
        static MouseCursor Crosshair;
        /** @brief Hand/pointer cursor. */
        static MouseCursor Hand;
        /** @brief I-beam text cursor. */
        static MouseCursor IBeam;
        /** @brief No/deny cursor. */
        static MouseCursor No;
        /** @brief Size-all cursor. */
        static MouseCursor SizeAll;
        /** @brief Size northeast/southwest cursor. */
        static MouseCursor SizeNESW;
        /** @brief Size north/south cursor. */
        static MouseCursor SizeNS;
        /** @brief Size northwest/southeast cursor. */
        static MouseCursor SizeNWSE;
        /** @brief Size west/east cursor. */
        static MouseCursor SizeWE;
        /** @brief Wait/busy cursor. */
        static MouseCursor Wait;
        /** @brief Wait cursor (arrow with hourglass). */
        static MouseCursor WaitCursor;

    private:
        SDL_Cursor* sdlCursor_ = nullptr;
        bool        owning_    = false;

        static MouseCursor MakeSystem(SDL_SystemCursor id);
    };
}
