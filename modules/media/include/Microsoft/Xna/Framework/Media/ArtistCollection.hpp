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
    class Artist;
    class MediaLibrary;

    /** @brief An ordered, read-only collection of Artist objects. */
    class ArtistCollection final : public System::Object, public System::IDisposable
    {
    public:
        using iterator = std::vector<Artist*>::iterator;
        using const_iterator = std::vector<Artist*>::const_iterator;

        /** @brief Releases the resources used by this collection. */
        void Dispose() override;

        /**
         * @brief Gets the number of artists in this collection.
         *
         * @return Artist count.
         */
        [[nodiscard]] SharpRuntime::intcs getCountProperty() const;

        /**
         * @brief Gets whether this collection has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets the artist at the specified index.
         *
         * @param index Zero-based index.
         * @return Pointer to the Artist at that index.
         */
        [[nodiscard]] Artist* operator[](SharpRuntime::intcs index) const;

        /** @brief Returns an iterator to the first artist. */
        CNAEXT [[nodiscard]] iterator begin();

        /** @brief Returns an iterator past the last artist. */
        CNAEXT [[nodiscard]] iterator end();

        /** @brief Returns a const iterator to the first artist. */
        CNAEXT [[nodiscard]] const_iterator begin() const;

        /** @brief Returns a const iterator past the last artist. */
        CNAEXT [[nodiscard]] const_iterator end() const;

        /** @brief Returns the fully-qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        friend class MediaLibrary;
        explicit ArtistCollection(std::vector<Artist*> artists);

        CNA::Internal::Media::MediaCollectionBase<Artist> base_;
    };
}
