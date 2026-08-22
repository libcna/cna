// SPDX-License-Identifier: MS-PL
#pragma once

#include <chrono>
#include <string>

#include "CNA/CNAHelper.hpp"
#include "System/IDisposable.hpp"
#include "System/IO/Stream.hpp"
#include "System/Object.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class MediaLibrary;
    class PictureAlbum;

    /** @brief Represents a picture in the device media library. */
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
         * @brief Returns a stream over the full-size image data for this picture.
         *
         * The FNA/XNA return type is Stream (not the previous placeholder void*, corrected here
         * to match the real API shape now that this member is genuinely implemented).
         *
         * @return Pointer to a Stream over the image data. Caller owns the Stream.
         */
        System::IO::Stream* GetImage();

        /**
         * @brief Returns a stream over a thumbnail of this picture.
         *
         * No separate thumbnail is generated -- returns the same image as GetImage(), matching
         * this being a from-scratch feature with no FNA behavior to differ against.
         *
         * @return Pointer to a Stream over the thumbnail image data. Caller owns the Stream.
         */
        System::IO::Stream* GetThumbnail();

        /**
         * @brief Returns whether this picture is equal to another.
         *
         * @param other Picture to compare with.
         * @return true if equal; otherwise false.
         */
        [[nodiscard]] bool Equals(const Picture* other) const;

        /**
         * @brief Returns whether this picture is equal to another object.
         *
         * @param obj Object to compare with.
         * @return true if the object is an equal Picture; otherwise false.
         */
        [[nodiscard]] bool Equals(const System::Object* obj) const override;

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
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Returns this picture's resolved file path, usable as the token accepted by
         * MediaLibrary::GetPictureFromToken().
         *
         * FNA's "opaque library token" has no real equivalent source on desktop (it historically
         * came from a native Zune/Xbox picture-picker UI); the resolved file path is used as a
         * simple, real, stable token instead so the token API is actually usable end-to-end, not
         * just present (plans/plan_media.md MEDIA-62; no FNA logic to port, see plan §0).
         *
         * @return Token string identifying this picture.
         */
        CNAEXT [[nodiscard]] const std::string& getTokenEXT() const { return path_; }

        /** @brief Returns whether two pictures are equal. */
        friend bool operator==(const Picture& lhs, const Picture& rhs);

        /** @brief Returns whether two pictures are not equal. */
        friend bool operator!=(const Picture& lhs, const Picture& rhs);

    private:
        friend class MediaLibrary;
        Picture(std::string name, PictureAlbum* album, SharpRuntime::intcs width,
                SharpRuntime::intcs height, std::chrono::system_clock::time_point date,
                std::string path);

        std::string name_;
        PictureAlbum* album_; // non-owning -- owned by MediaLibrary
        SharpRuntime::intcs width_;
        SharpRuntime::intcs height_;
        std::chrono::system_clock::time_point date_;
        std::string path_; // resolved file path -- also the equality key
        bool isDisposed_ = false;
    };
}
