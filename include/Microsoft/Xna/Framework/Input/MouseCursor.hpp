// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <SDL3/SDL.h>

namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Represents a mouse cursor image.
     *
     * Wraps an SDL_Cursor* and provides the standard XNA system cursor constants.
     *
     * @note NOXNA — this is a MonoGame-derived CNA extension. No MouseCursor type exists
     * in XNA 4.0 or FNA.
     */
    NOXNA class MouseCursor
    {
    public:
        /** @brief Creates a default Arrow cursor. */
        NOXNA MouseCursor();

        /**
         * @brief Creates a cursor wrapping the given SDL cursor.
         * @param sdlCursor The SDL cursor to wrap.
         * @param owning If true, this object takes ownership of the SDL cursor.
         */
        NOXNA explicit MouseCursor(SDL_Cursor* sdlCursor, bool owning = false);

        /**
         * @brief Creates a cursor from the specified texture.
         * @param texture Texture to use as the cursor image. Must be SurfaceFormat::Color or ColorSrgb.
         * @param originX X coordinate of the image that will be used for the mouse position (the cursor's hot spot).
         * @param originY Y coordinate of the image that will be used for the mouse position (the cursor's hot spot).
         * @return A new MouseCursor built from the texture's pixels.
         */
        NOXNA static MouseCursor FromTexture2D(const Graphics::Texture2D& texture, int originX, int originY);

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
        NOXNA [[nodiscard]] SDL_Cursor* GetSDLCursor() const { return sdlCursor_; }

        /** @brief Standard arrow cursor. */
        NOXNA static MouseCursor Arrow;
        /** @brief Crosshair cursor. */
        NOXNA static MouseCursor Crosshair;
        /** @brief Hand/pointer cursor. */
        NOXNA static MouseCursor Hand;
        /** @brief I-beam text cursor. */
        NOXNA static MouseCursor IBeam;
        /** @brief No/deny cursor. */
        NOXNA static MouseCursor No;
        /** @brief Size-all cursor. */
        NOXNA static MouseCursor SizeAll;
        /** @brief Size northeast/southwest cursor. */
        NOXNA static MouseCursor SizeNESW;
        /** @brief Size north/south cursor. */
        NOXNA static MouseCursor SizeNS;
        /** @brief Size northwest/southeast cursor. */
        NOXNA static MouseCursor SizeNWSE;
        /** @brief Size west/east cursor. */
        NOXNA static MouseCursor SizeWE;
        /** @brief Wait/busy cursor. */
        NOXNA static MouseCursor Wait;
        /** @brief Wait cursor (arrow with hourglass). */
        NOXNA static MouseCursor WaitCursor;

    private:
        SDL_Cursor* sdlCursor_ = nullptr;
        bool        owning_    = false;

        NOXNA static MouseCursor MakeSystem(SDL_SystemCursor id);
    };
}
