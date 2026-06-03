#pragma once

#include <string>

#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class SongCollection;

    /// Represents a playlist of songs.
    /// @note Status: Stub — media library catalog access not implemented
    class Playlist final : public System::Object, public System::IDisposable
    {
    public:
        void Dispose() override;

        [[nodiscard]] System::TimeSpan getDurationProperty() const;
        [[nodiscard]] bool getIsDisposedProperty() const;
        [[nodiscard]] std::string getNameProperty() const;
        [[nodiscard]] SongCollection* getSongsProperty() const;

        [[nodiscard]] bool Equals(const Playlist* other) const;
        [[nodiscard]] std::string ToString() const;

        [[nodiscard]] const std::string& GetTypeName() const override;

        friend bool operator==(const Playlist& lhs, const Playlist& rhs);
        friend bool operator!=(const Playlist& lhs, const Playlist& rhs);
    };
}
