// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Media
{
    /// Collection of songs.
    class SongCollection final : public System::Object, public System::IDisposable
    {
    public:
        using iterator = std::vector<Song*>::iterator;
        using const_iterator = std::vector<Song*>::const_iterator;

        /// Creates a song collection from a list of song pointers. The collection does not own the songs.
        explicit SongCollection(std::vector<Song*> songs);

        /// Gets the song at the specified index.
        [[nodiscard]] Song* operator[](SharpRuntime::intcs index) const;

        /// Gets the number of songs in the collection.
        [[nodiscard]] SharpRuntime::intcs getCountProperty() const;

        /// Gets whether this collection has been disposed.
        [[nodiscard]] bool getIsDisposedProperty() const;

        /// Clears the collection and marks it disposed.
        void Dispose() override;

        [[nodiscard]] iterator begin();
        [[nodiscard]] iterator end();
        [[nodiscard]] const_iterator begin() const;
        [[nodiscard]] const_iterator end() const;

        [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::vector<Song*> innerList_;
        bool isDisposed_;
    };
}
