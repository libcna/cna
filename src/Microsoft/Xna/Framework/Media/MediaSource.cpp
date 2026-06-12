// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/MediaSource.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Media
{
    MediaSource::MediaSource()
    {
    }

    MediaSourceType MediaSource::getMediaSourceTypeProperty() const
    {
        // TODO: implement media source type retrieval
        throw std::runtime_error("not implemented");
    }

    std::string MediaSource::getNameProperty() const
    {
        // TODO: implement media source name retrieval
        throw std::runtime_error("not implemented");
    }

    std::vector<MediaSource*> MediaSource::GetAvailableMediaSources()
    {
        return {};
    }

    std::string MediaSource::ToString() const
    {
        // TODO: implement media source string representation
        throw std::runtime_error("not implemented");
    }

    const std::string& MediaSource::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.MediaSource";
        return typeName;
    }
}
