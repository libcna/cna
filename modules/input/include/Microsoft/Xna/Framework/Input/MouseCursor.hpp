// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/IDisposable.hpp"

#include <cstdint>
#include <vector>

namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Represents a mouse cursor image.
     *
     * Stores a platform-neutral system shape or an owned copy of custom RGBA pixels. The active
     * platform creates and owns the corresponding native cursor when `Mouse::SetCursor` is called.
     * Standard stock cursors are lazily-created process-lifetime singletons.
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
         * @brief Creates a cursor from the specified texture.
         * @param texture Texture to use as the cursor image. Must be SurfaceFormat::Color or ColorSrgbEXT.
         * @param originX X coordinate of the image that will be used for the mouse position (the cursor's hot spot).
         * @param originY Y coordinate of the image that will be used for the mouse position (the cursor's hot spot).
         * @return A new MouseCursor built from the texture's pixels.
         */
        CNAEXT static MouseCursor FromTexture2D(const Graphics::Texture2D& texture, int originX, int originY);

        MouseCursor(const MouseCursor&)            = delete;
        MouseCursor& operator=(const MouseCursor&) = delete;
        /** @brief Move-constructs a MouseCursor, transferring its image description. */
        MouseCursor(MouseCursor&& other) noexcept;
        /** @brief Move-assigns a MouseCursor, transferring its image description. */
        MouseCursor& operator=(MouseCursor&& other) noexcept;

        /** @brief Destructor; disposes this cursor description. */
        ~MouseCursor() override;

        /**
         * @brief Releases owned custom pixels and makes this cursor unusable. Safe to repeat.
         *
         * @note For the stock system-cursor singletons (getArrowProperty() etc.) this is a
         *       deliberate no-op — they are process-lifetime shared instances, so disposing one
         *       must not invalidate the cursor for every other user. Do not `std::move`
         *       a stock-cursor reference either; obtain and use it in place.
         */
        CNAEXT void Dispose() override;

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
        friend class Mouse;

        static constexpr std::uint8_t ShapeArrow = 0;
        static constexpr std::uint8_t ShapeCrosshair = 1;
        static constexpr std::uint8_t ShapeHand = 2;
        static constexpr std::uint8_t ShapeIBeam = 3;
        static constexpr std::uint8_t ShapeNo = 4;
        static constexpr std::uint8_t ShapeSizeAll = 5;
        static constexpr std::uint8_t ShapeSizeNESW = 6;
        static constexpr std::uint8_t ShapeSizeNS = 7;
        static constexpr std::uint8_t ShapeSizeNWSE = 8;
        static constexpr std::uint8_t ShapeSizeWE = 9;
        static constexpr std::uint8_t ShapeWait = 10;
        static constexpr std::uint8_t ShapeWaitArrow = 11;

        explicit MouseCursor(std::uint8_t shape, bool systemSingleton) noexcept;
        MouseCursor(int width, int height, int originX, int originY,
                    std::vector<std::uint32_t> rgba) noexcept;
        static MouseCursor MakeSystem(std::uint8_t shape);

        std::uint8_t systemShape_ = ShapeArrow;
        std::vector<std::uint32_t> rgba_;
        int width_ = 0;
        int height_ = 0;
        int originX_ = 0;
        int originY_ = 0;
        bool isCustom_ = false;
        bool isDisposed_ = false;
        bool isSystemSingleton_ = false;
    };
}
