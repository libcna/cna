#pragma once

#include <string>

#include "System/IDisposable.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class AlbumCollection;
    class SongCollection;

    /// Represents a music artist.
    /// @note Status: Stub — media library catalog access not implemented
    class Artist final : public System::Object, public System::IDisposable
    {
    public:
        void Dispose() override;

        [[nodiscard]] AlbumCollection* getAlbumsProperty() const;
        [[nodiscard]] bool getIsDisposedProperty() const;
        [[nodiscard]] std::string getNameProperty() const;
        [[nodiscard]] SongCollection* getSongsProperty() const;

        [[nodiscard]] bool Equals(const Artist* other) const;
        [[nodiscard]] std::string ToString() const;

        [[nodiscard]] const std::string& GetTypeName() const override;

        friend bool operator==(const Artist& lhs, const Artist& rhs);
        friend bool operator!=(const Artist& lhs, const Artist& rhs);
    };
}
