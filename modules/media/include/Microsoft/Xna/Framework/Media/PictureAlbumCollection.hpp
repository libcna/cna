// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Media/MediaCollectionBase.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class PictureAlbum;
    class MediaLibrary;

    /** @brief An ordered, read-only collection of PictureAlbum objects. */
    class PictureAlbumCollection final : public System::Object, public System::IDisposable
    {
    public:
        using iterator = std::vector<PictureAlbum*>::iterator;
        using const_iterator = std::vector<PictureAlbum*>::const_iterator;

        /** @brief Releases the resources used by this collection. */
        void Dispose() override;

        /**
         * @brief Gets the number of picture albums in this collection.
         *
         * @return Album count.
         */
        [[nodiscard]] SharpRuntime::intcs getCountProperty() const;

        /**
         * @brief Gets whether this collection has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets the picture album at the specified index.
         *
         * @param index Zero-based index.
         * @return Pointer to the PictureAlbum at that index.
         */
        [[nodiscard]] PictureAlbum* operator[](SharpRuntime::intcs index) const;

        /** @brief Returns an iterator to the first picture album. */
        NOXNA [[nodiscard]] iterator begin();

        /** @brief Returns an iterator past the last picture album. */
        NOXNA [[nodiscard]] iterator end();

        /** @brief Returns a const iterator to the first picture album. */
        NOXNA [[nodiscard]] const_iterator begin() const;

        /** @brief Returns a const iterator past the last picture album. */
        NOXNA [[nodiscard]] const_iterator end() const;

        /** @brief Returns the fully-qualified .NET type name. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        friend class MediaLibrary;
        explicit PictureAlbumCollection(std::vector<PictureAlbum*> albums);

        CNA::Internal::Media::MediaCollectionBase<PictureAlbum> base_;
    };
}
