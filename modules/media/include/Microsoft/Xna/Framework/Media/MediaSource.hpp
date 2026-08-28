// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Media/MediaSourceType.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class MediaLibrary;

    /** @brief Represents a media source device from which the media library reads content. */
    class MediaSource final : public System::Object
    {
    public:
        /**
         * @brief Gets the type of this media source.
         *
         * @return MediaSourceType value.
         */
        [[nodiscard]] MediaSourceType getMediaSourceTypeProperty() const;

        /**
         * @brief Gets the display name of this media source.
         *
         * @return Source name string.
         */
        [[nodiscard]] std::string getNameProperty() const;

        /**
         * @brief Returns all available media sources on the current device.
         *
         * @return Vector of available MediaSource pointers.
         */
        static std::vector<MediaSource*> GetAvailableMediaSources();

        /**
         * @brief Returns a string representation of this media source.
         *
         * @return Source name string.
         */
        [[nodiscard]] std::string ToString() const override;

        /** @brief Returns the fully-qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        friend class MediaLibrary;
        MediaSource(MediaSourceType type, std::string name);

        MediaSourceType type_;
        std::string name_;
    };
}
