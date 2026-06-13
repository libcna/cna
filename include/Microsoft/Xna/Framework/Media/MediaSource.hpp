// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Media/MediaSourceType.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Media
{
    /**
     * @brief Represents a media source device from which the media library reads content.
     *
     * @note Status: Stub — media source enumeration not implemented.
     */
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
        [[nodiscard]] std::string ToString() const;

        /** @brief Returns the fully-qualified .NET type name. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        MediaSource();
    };
}
