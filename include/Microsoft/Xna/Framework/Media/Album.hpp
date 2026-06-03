#pragma once

#include <string>

#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class Artist;
    class Genre;
    class SongCollection;

    /// Represents a music album.
    /// @note Status: Stub — media library catalog access not implemented
    class Album final : public System::Object, public System::IDisposable
    {
    public:
        void Dispose() override;

        [[nodiscard]] Artist* getArtistProperty() const;
        [[nodiscard]] System::TimeSpan getDurationProperty() const;
        [[nodiscard]] Genre* getGenreProperty() const;
        [[nodiscard]] bool getHasArtProperty() const;
        [[nodiscard]] bool getIsDisposedProperty() const;
        [[nodiscard]] std::string getNameProperty() const;
        [[nodiscard]] SongCollection* getSongsProperty() const;

        void* GetAlbumArt();
        void* GetThumbnail();

        [[nodiscard]] bool Equals(const Album* other) const;
        [[nodiscard]] std::string ToString() const;

        [[nodiscard]] const std::string& GetTypeName() const override;

        friend bool operator==(const Album& lhs, const Album& rhs);
        friend bool operator!=(const Album& lhs, const Album& rhs);
    };
}
