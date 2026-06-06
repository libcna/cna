#include "Microsoft/Xna/Framework/Audio/RendererDetail.hpp"

#include <utility>

namespace Microsoft::Xna::Framework::Audio
{
    RendererDetail::RendererDetail(std::string friendlyName, std::string rendererId)
        : friendlyName_(std::move(friendlyName)),
          rendererId_(std::move(rendererId))
    {
    }

    const std::string& RendererDetail::getFriendlyNameProperty() const
    {
        return friendlyName_;
    }

    const std::string& RendererDetail::getRendererIdProperty() const
    {
        return rendererId_;
    }

    std::string RendererDetail::ToString() const
    {
        return friendlyName_;
    }

    bool RendererDetail::operator==(const RendererDetail& other) const
    {
        return rendererId_ == other.rendererId_;
    }

    bool RendererDetail::operator!=(const RendererDetail& other) const
    {
        return !(*this == other);
    }
}
