// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/ContentManagerAccess.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"

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
} // namespace CNA::Internal
