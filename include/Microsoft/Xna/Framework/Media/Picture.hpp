#pragma once

#include <chrono>
#include <string>

#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class PictureAlbum;

    /// Represents a picture in the media library.
    /// @note Status: Stub — media library catalog access not implemented
    class Picture final : public System::Object, public System::IDisposable
    {
    public:
        void Dispose() override;

        [[nodiscard]] PictureAlbum* getAlbumProperty() const;
        [[nodiscard]] std::chrono::system_clock::time_point getDateProperty() const;
        [[nodiscard]] SharpRuntime::intcs getHeightProperty() const;
        [[nodiscard]] bool getIsDisposedProperty() const;
        [[nodiscard]] std::string getNameProperty() const;
        [[nodiscard]] SharpRuntime::intcs getWidthProperty() const;

        void* GetImage();
        void* GetThumbnail();

        [[nodiscard]] bool Equals(const Picture* other) const;
        [[nodiscard]] std::string ToString() const;

        [[nodiscard]] const std::string& GetTypeName() const override;

        friend bool operator==(const Picture& lhs, const Picture& rhs);
        friend bool operator!=(const Picture& lhs, const Picture& rhs);
    };
}
