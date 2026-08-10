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
    class Playlist;
    class MediaLibrary;

    /** @brief An ordered, read-only collection of Playlist objects. */
    class PlaylistCollection final : public System::Object, public System::IDisposable
    {
    public:
        using iterator = std::vector<Playlist*>::iterator;
        using const_iterator = std::vector<Playlist*>::const_iterator;

        /** @brief Releases the resources used by this collection. */
        void Dispose() override;

        /**
         * @brief Gets the number of playlists in this collection.
         *
         * @return Playlist count.
         */
        [[nodiscard]] SharpRuntime::intcs getCountProperty() const;

        /**
         * @brief Gets whether this collection has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets the playlist at the specified index.
         *
         * @param index Zero-based index.
         * @return Pointer to the Playlist at that index.
         */
        [[nodiscard]] Playlist* operator[](SharpRuntime::intcs index) const;

        /** @brief Returns an iterator to the first playlist. */
        CNAEXT [[nodiscard]] iterator begin();

        /** @brief Returns an iterator past the last playlist. */
        CNAEXT [[nodiscard]] iterator end();

        /** @brief Returns a const iterator to the first playlist. */
        CNAEXT [[nodiscard]] const_iterator begin() const;

        /** @brief Returns a const iterator past the last playlist. */
        CNAEXT [[nodiscard]] const_iterator end() const;

        /** @brief Returns the fully-qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        friend class MediaLibrary;
        explicit PlaylistCollection(std::vector<Playlist*> playlists);

        CNA::Internal::Media::MediaCollectionBase<Playlist> base_;
    };
}
