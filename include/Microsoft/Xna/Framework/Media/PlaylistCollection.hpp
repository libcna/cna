// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class Playlist;

    /**
     * @brief An ordered, read-only collection of Playlist objects.
     *
     * @note Status: Stub — media library catalog access not implemented.
     */
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
        [[nodiscard]] iterator begin();

        /** @brief Returns an iterator past the last playlist. */
        [[nodiscard]] iterator end();

        /** @brief Returns a const iterator to the first playlist. */
        [[nodiscard]] const_iterator begin() const;

        /** @brief Returns a const iterator past the last playlist. */
        [[nodiscard]] const_iterator end() const;

        /** @brief Returns the fully-qualified .NET type name. */
        [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::vector<Playlist*> innerList_;
    };
}
