// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/ContentManagerAccess.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"

namespace CNA::Internal
{
    using Microsoft::Xna::Framework::Content::ContentManager;

    std::string ContentManagerAccess::BuildAssetPath(const ContentManager& manager,
                                                    const std::string& assetName)
    {
        return manager.BuildAssetPath(assetName);
    }

    std::string ContentManagerAccess::ResolveExistingAssetPath(const ContentManager& manager,
                                                              const std::string& path)
    {
        return manager.ResolveExistingAssetPath(path);
    }

    std::string ContentManagerAccess::NormalizeKey(const ContentManager& manager,
                                                  const std::string& assetName)
    {
        return manager.NormalizeKey(assetName);
    }

    Microsoft::Xna::Framework::Graphics::GraphicsDevice&
    ContentManagerAccess::GraphicsDeviceFromContentReader(
        const Microsoft::Xna::Framework::Content::ContentReader& input)
    {
        const ContentManager* manager = input.getContentManagerProperty();
        Microsoft::Xna::Framework::Graphics::GraphicsDevice* device =
            manager != nullptr ? manager->FindGraphicsDevice() : nullptr;
        if (device == nullptr)
        {
            throw Microsoft::Xna::Framework::Content::ContentLoadException(
                "Error loading \"" + input.getAssetNameProperty() +
                "\". GraphicsDevice component not found.");
        }
        return *device;
    }
} // namespace CNA::Internal
