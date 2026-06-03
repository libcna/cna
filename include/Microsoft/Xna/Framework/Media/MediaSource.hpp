#pragma once

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Media/MediaSourceType.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Media
{
    /// Represents a media source device.
    /// @note Status: Stub — media source enumeration not implemented
    class MediaSource final : public System::Object
    {
    public:
        [[nodiscard]] MediaSourceType getMediaSourceTypeProperty() const;
        [[nodiscard]] std::string getNameProperty() const;

        static std::vector<MediaSource*> GetAvailableMediaSources();

        [[nodiscard]] std::string ToString() const;

        [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        MediaSource();
    };
}
