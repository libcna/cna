// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Media
{
    /** @brief An ordered, read-only collection of Song objects. */
    class SongCollection final : public System::Object, public System::IDisposable
    {
    public:
        using iterator = std::vector<Song*>::iterator;
        using const_iterator = std::vector<Song*>::const_iterator;

        /**
         * @brief Creates a song collection from a list of song pointers.
         *
         * The collection does not take ownership of the song pointers.
         *
         * @param songs Vector of Song pointers.
         */
        NOXNA explicit SongCollection(std::vector<Song*> songs);

        /**
         * @brief Gets the song at the specified index.
         *
         * @param index Zero-based index.
         * @return Pointer to the Song at that index.
         */
        [[nodiscard]] Song* operator[](SharpRuntime::intcs index) const;

        /**
         * @brief Gets the number of songs in this collection.
         *
         * @return Song count.
         */
        [[nodiscard]] SharpRuntime::intcs getCountProperty() const;

        /**
         * @brief Gets whether this collection has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /** @brief Clears the collection and marks it disposed. */
        void Dispose() override;

        /** @brief Returns an iterator to the first song. */
        NOXNA [[nodiscard]] iterator begin();

        /** @brief Returns an iterator past the last song. */
        NOXNA [[nodiscard]] iterator end();

        /** @brief Returns a const iterator to the first song. */
        NOXNA [[nodiscard]] const_iterator begin() const;

        /** @brief Returns a const iterator past the last song. */
        NOXNA [[nodiscard]] const_iterator end() const;

        /** @brief Returns the fully-qualified .NET type name. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::vector<Song*> innerList_;
        bool isDisposed_;
    };
}
