#pragma once

#include <string>
#include <vector>

#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class Playlist;

    /// Collection of playlists.
    /// @note Status: Stub — media library catalog access not implemented
    class PlaylistCollection final : public System::Object, public System::IDisposable
    {
    public:
        using iterator = std::vector<Playlist*>::iterator;
        using const_iterator = std::vector<Playlist*>::const_iterator;

        void Dispose() override;

        [[nodiscard]] SharpRuntime::intcs getCountProperty() const;
        [[nodiscard]] bool getIsDisposedProperty() const;
        [[nodiscard]] Playlist* operator[](SharpRuntime::intcs index) const;

        [[nodiscard]] iterator begin();
        [[nodiscard]] iterator end();
        [[nodiscard]] const_iterator begin() const;
        [[nodiscard]] const_iterator end() const;

        [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::vector<Playlist*> innerList_;
    };
}
