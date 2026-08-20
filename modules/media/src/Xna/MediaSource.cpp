// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/MediaSource.hpp"

#include <utility>

namespace Microsoft::Xna::Framework::Media
{
    MediaSource::MediaSource(MediaSourceType type, std::string name)
        : type_(type), name_(std::move(name))
    {
    }

    MediaSourceType MediaSource::getMediaSourceTypeProperty() const
    {
        return type_;
    }

    std::string MediaSource::getNameProperty() const
    {
        return name_;
    }

    std::vector<MediaSource*> MediaSource::GetAvailableMediaSources()
    {
        // No real "device enumeration" concept on desktop -- there is exactly one real source,
        // the local device itself (plans/plan_media.md MEDIA-61; no FNA logic to port, see plan §0).
        return { new MediaSource(MediaSourceType::LocalDevice, "Local Device") };
    }

    std::string MediaSource::ToString() const
    {
        return name_;
    }

    const std::string& MediaSource::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.MediaSource";
        return typeName;
    }
}
