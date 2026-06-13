// SPDX-License-Identifier: MS-PL
#pragma once

#include <chrono>
#include <string>

#include "CNA/CNAHelper.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class PictureAlbum;

    /**
     * @brief Represents a picture in the device media library.
     *
     * @note Status: Stub — media library catalog access not implemented.
     */
    class Picture final : public System::Object, public System::IDisposable
    {
    public:
        /** @brief Releases the resources used by this picture. */
        void Dispose() override;

        /**
         * @brief Gets the album that contains this picture.
         *
         * @return Pointer to the containing PictureAlbum.
         */
        [[nodiscard]] PictureAlbum* getAlbumProperty() const;

        /**
         * @brief Gets the date and time this picture was taken.
         *
         * @return Date/time as a system_clock time_point.
         */
        [[nodiscard]] std::chrono::system_clock::time_point getDateProperty() const;

        /**
         * @brief Gets the height of this picture in pixels.
         *
         * @return Picture height.
         */
        [[nodiscard]] SharpRuntime::intcs getHeightProperty() const;

        /**
         * @brief Gets whether this picture has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets the display name of this picture.
         *
         * @return Picture name string.
         */
        [[nodiscard]] std::string getNameProperty() const;

        /**
         * @brief Gets the width of this picture in pixels.
         *
         * @return Picture width.
         */
        [[nodiscard]] SharpRuntime::intcs getWidthProperty() const;

        /**
         * @brief Returns the full-size image data for this picture, or nullptr if unavailable.
         *
         * @return Opaque pointer to image data.
         */
        void* GetImage();

        /**
         * @brief Returns a thumbnail image for this picture, or nullptr if unavailable.
         *
         * @return Opaque pointer to thumbnail data.
         */
        void* GetThumbnail();

        /**
         * @brief Returns whether this picture is equal to another.
         *
         * @param other Picture to compare with.
         * @return true if equal; otherwise false.
         */
        [[nodiscard]] bool Equals(const Picture* other) const;

        /**
         * @brief Gets the hash code for this Picture instance.
         * @return Hash code of the object.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Returns a string representation of this picture.
         *
         * @return Picture name string.
         */
        [[nodiscard]] std::string ToString() const;

        /** @brief Returns the fully-qualified .NET type name. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Returns whether two pictures are equal. */
        friend bool operator==(const Picture& lhs, const Picture& rhs);

        /** @brief Returns whether two pictures are not equal. */
        friend bool operator!=(const Picture& lhs, const Picture& rhs);

    private:
        Picture();
    };
}
