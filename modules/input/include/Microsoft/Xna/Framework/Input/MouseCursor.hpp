// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/IDisposable.hpp"

// Opaque forward declaration of SDL's cursor handle. This public header wraps an SDL cursor but must
// not pull <SDL3/SDL.h> into consumers: a strict-XNA header (Mouse.hpp) includes this one, so doing so
// would drag all of SDL into the public XNA include tree. A pointer to the incomplete type is all the
// public API needs; MouseCursor.cpp includes the real SDL header.
struct SDL_Cursor;

namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Represents a mouse cursor image.
     *
     * Wraps an SDL_Cursor* and provides the standard stock system cursors as lazily-created,
     * process-lifetime singletons (each getXProperty() below constructs its SDL cursor on first
     * access, matching MonoGame's static-constructor-triggered lazy initialization).
     *
     * @note CNAEXT — this is a MonoGame-derived CNA extension. No MouseCursor type exists
     * in XNA 4.0 or FNA.
     */
    CNAEXT class MouseCursor : public System::IDisposable
    {
    public:
        /** @brief Creates a default Arrow cursor. */
        CNAEXT MouseCursor();

        /**
         * @brief Creates a cursor wrapping the given SDL cursor.
         * @param sdlCursor The SDL cursor to wrap.
         * @param owning If true, this object takes ownership of the SDL cursor.
         */
        CNAEXT explicit MouseCursor(SDL_Cursor* sdlCursor, bool owning = false);

        /**
         * @brief Creates a cursor from the specified texture.
         * @param texture Texture to use as the cursor image. Must be SurfaceFormat::Color or ColorSrgbEXT.
         * @param originX X coordinate of the image that will be used for the mouse position (the cursor's hot spot).
         * @param originY Y coordinate of the image that will be used for the mouse position (the cursor's hot spot).
         * @return A new MouseCursor built from the texture's pixels.
         */
        CNAEXT static MouseCursor FromTexture2D(const Graphics::Texture2D& texture, int originX, int originY);

        MouseCursor(const MouseCursor&)            = delete;
        MouseCursor& operator=(const MouseCursor&) = delete;
        /** @brief Move-constructs a MouseCursor, transferring SDL cursor ownership. */
        MouseCursor(MouseCursor&& other) noexcept;
        /** @brief Move-assigns a MouseCursor, transferring SDL cursor ownership. */
        MouseCursor& operator=(MouseCursor&& other) noexcept;

        /** @brief Destructor; disposes the cursor if owned. */
        ~MouseCursor() override;

        /**
         * @brief Releases the SDL cursor if owned. Safe to call more than once.
         *
         * @note For the stock system-cursor singletons (getArrowProperty() etc.) this is a
         *       deliberate no-op — they are process-lifetime shared instances, so disposing one
         *       must not free the SDL cursor out from under every other user. Do not `std::move`
         *       a stock-cursor reference either; obtain and use it in place.
         */
        CNAEXT void Dispose() override;

        /**
         * @brief Returns the underlying SDL_Cursor pointer (not owned by the caller).
         * @return The SDL_Cursor pointer.
         */
        CNAEXT [[nodiscard]] SDL_Cursor* GetSDLCursor() const { return sdlCursor_; }

        /**
         * @brief Gets the default arrow cursor.
         * @return Reference to the shared stock cursor instance.
         */
        CNAEXT [[nodiscard]] static MouseCursor& getArrowProperty();
        /**
         * @brief Gets the crosshair ("+") cursor.
         * @return Reference to the shared stock cursor instance.
         */
        CNAEXT [[nodiscard]] static MouseCursor& getCrosshairProperty();
        /**
         * @brief Gets the hand cursor, usually used for web links.
         * @return Reference to the shared stock cursor instance.
         */
        CNAEXT [[nodiscard]] static MouseCursor& getHandProperty();
        /**
         * @brief Gets the cursor that appears when the mouse is over text editing regions.
         * @return Reference to the shared stock cursor instance.
         */
        CNAEXT [[nodiscard]] static MouseCursor& getIBeamProperty();
        /**
         * @brief Gets the cursor that points that something is invalid, usually a cross.
         * @return Reference to the shared stock cursor instance.
         */
        CNAEXT [[nodiscard]] static MouseCursor& getNoProperty();
        /**
         * @brief Gets the size-all cursor which points in all directions.
         * @return Reference to the shared stock cursor instance.
         */
        CNAEXT [[nodiscard]] static MouseCursor& getSizeAllProperty();
        /**
         * @brief Gets the northeast/southwest ("/") cursor.
         * @return Reference to the shared stock cursor instance.
         */
        CNAEXT [[nodiscard]] static MouseCursor& getSizeNESWProperty();
        /**
         * @brief Gets the vertical north/south ("|") cursor.
         * @return Reference to the shared stock cursor instance.
         */
        CNAEXT [[nodiscard]] static MouseCursor& getSizeNSProperty();
        /**
         * @brief Gets the northwest/southeast ("\") cursor.
         * @return Reference to the shared stock cursor instance.
         */
        CNAEXT [[nodiscard]] static MouseCursor& getSizeNWSEProperty();
        /**
         * @brief Gets the horizontal west/east ("-") cursor.
         * @return Reference to the shared stock cursor instance.
         */
        CNAEXT [[nodiscard]] static MouseCursor& getSizeWEProperty();
        /**
         * @brief Gets the waiting cursor that appears while the application/system is busy.
         * @return Reference to the shared stock cursor instance.
         */
        CNAEXT [[nodiscard]] static MouseCursor& getWaitProperty();
        /**
         * @brief Gets the cross between Arrow and Wait cursors.
         * @return Reference to the shared stock cursor instance.
         */
        CNAEXT [[nodiscard]] static MouseCursor& getWaitArrowProperty();

    private:
        SDL_Cursor* sdlCursor_        = nullptr;
        bool        owning_           = false;
        bool        isDisposed_       = false;
        // True for the stock system-cursor singletons: Dispose()/destructor no-op so the shared
        // process-lifetime SDL cursor is never freed (which would corrupt it for every other user
        // and risks a free-after-SDL_Quit at static teardown).
        bool        isSystemSingleton_ = false;

        // Takes the SDL_SystemCursor value as a plain int so the enum stays out of this public header
        // (the .cpp casts it back). id values come from SDL_SYSTEM_CURSOR_* in MouseCursor.cpp.
        CNAEXT static MouseCursor MakeSystem(int systemCursorId);
    };
}
