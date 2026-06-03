#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include <filesystem>

namespace Microsoft::Xna::Framework::Content
{
    const std::string& ContentManager::getRootDirectoryProperty() const
    {
        return RootDirectory_;
    }

    void ContentManager::setRootDirectoryProperty(const std::string& v)
    {
        RootDirectory_ = v;
    }

    ContentManager::ContentManager() = default;

    void ContentManager::setGraphicsDevice(Graphics::GraphicsDevice& graphicsDevice)
    {
        graphicsDevice_ = &graphicsDevice;
    }

    void ContentManager::Dispose()
    {
    }

    std::string ContentManager::BuildAssetPath(const std::string& assetName) const
    {
        if (assetName.empty())
        {
            return getRootDirectoryProperty();
        }

        if (getRootDirectoryProperty().empty())
        {
            return assetName;
        }

        namespace fs = std::filesystem;
        return (fs::path(getRootDirectoryProperty()) / assetName).string();
    }
}
