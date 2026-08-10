// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ResourceContentManager.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Content
{
    ResourceContentManager::ResourceContentManager(System::IServiceProvider* serviceProvider)
        : ContentManager(serviceProvider)
    {
    }

    std::unique_ptr<System::IO::Stream> ResourceContentManager::OpenStream(const std::string& /*assetName*/)
    {
        // CNA_STUB: XNA 4.0 API surface placeholder. Behavior is not implemented yet.
        throw std::runtime_error("ResourceContentManager::OpenStream is not implemented in CNA.");
    }
}
