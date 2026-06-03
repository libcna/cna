#include "Microsoft/Xna/Framework/Media/Picture.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Media
{
    void Picture::Dispose()
    {
        // TODO: implement picture resource cleanup
        throw std::runtime_error("not implemented");
    }

    PictureAlbum* Picture::getAlbumProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    std::chrono::system_clock::time_point Picture::getDateProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    SharpRuntime::intcs Picture::getHeightProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    bool Picture::getIsDisposedProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    std::string Picture::getNameProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    SharpRuntime::intcs Picture::getWidthProperty() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    void* Picture::GetImage()
    {
        // TODO: implement image data retrieval
        throw std::runtime_error("not implemented");
    }

    void* Picture::GetThumbnail()
    {
        // TODO: implement thumbnail retrieval
        throw std::runtime_error("not implemented");
    }

    bool Picture::Equals(const Picture* other) const
    {
        // TODO: implement equality comparison
        throw std::runtime_error("not implemented");
    }

    std::string Picture::ToString() const
    {
        // TODO: implement media library catalog access
        throw std::runtime_error("not implemented");
    }

    const std::string& Picture::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.Picture";
        return typeName;
    }

    bool operator==(const Picture& lhs, const Picture& rhs)
    {
        // TODO: implement equality comparison
        throw std::runtime_error("not implemented");
    }

    bool operator!=(const Picture& lhs, const Picture& rhs)
    {
        // TODO: implement equality comparison
        throw std::runtime_error("not implemented");
    }
}
