// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ResourceContentManager.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/IO/MemoryStream.hpp"

namespace Microsoft::Xna::Framework::Content
{
    ResourceContentManager::ResourceContentManager(System::IServiceProvider* serviceProvider)
        : ContentManager(serviceProvider)
    {
    }

    ResourceContentManager::ResourceContentManager(
        System::IServiceProvider* serviceProvider,
        System::Resources::ResourceManager* resourceManager)
        : ContentManager(serviceProvider), resourceManager_(resourceManager)
    {
        // XNA rejects a null resource manager before storing it, which is the only validation its
        // constructor does.
        if (resourceManager_ == nullptr)
        {
            throw System::ArgumentNullException("resourceManager");
        }
    }

    std::unique_ptr<System::IO::Stream> ResourceContentManager::OpenStream(
        const std::string& assetName)
    {
        if (resourceManager_ == nullptr)
        {
            throw ContentLoadException(
                "ResourceContentManager: this manager was constructed without a ResourceManager, "
                "so it has no resource family to read '" + assetName + "' from.");
        }

        const std::optional<std::any> resource = resourceManager_->GetObject(assetName);
        if (!resource.has_value())
        {
            // XNA's OpenResourceNotFound.
            throw ContentLoadException(
                "ResourceContentManager: the resource '" + assetName + "' could not be found.");
        }

        const auto* bytes = std::any_cast<std::vector<std::uint8_t>>(&*resource);
        if (bytes == nullptr)
        {
            // XNA's OpenResourceNotBinary: the resource exists but is not a byte[]. Said separately
            // from "not found", because the two mean different mistakes -- a missing resource and a
            // resource authored as the wrong kind.
            throw ContentLoadException(
                "ResourceContentManager: the resource '" + assetName +
                "' is not a binary resource.");
        }

        if (bytes->size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            throw ContentLoadException(
                "ResourceContentManager: the resource '" + assetName + "' is too large to open.");
        }

        // MemoryStream copies the range, so the stream owns its bytes and the resource family is not
        // holding anything open on this manager's behalf.
        return std::make_unique<System::IO::MemoryStream>(
            bytes->data(), static_cast<SharpRuntime::intcs>(bytes->size()), false);
    }
}
