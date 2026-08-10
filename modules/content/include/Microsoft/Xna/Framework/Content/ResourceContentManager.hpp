// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "System/IServiceProvider.hpp"
#include "System/IO/Stream.hpp"

namespace Microsoft::Xna::Framework::Content
{
    /**
     * @brief Loads content from application-embedded resources rather than loose files.
     *
     * XNA 4.0 uses this to read assets compiled into the assembly. CNA's file-extension
     * content pipeline does not support embedded binaries; calls to OpenStream will
     * throw std::runtime_error.
     *
     * @note CNA_STUB: XNA 4.0 API surface placeholder. Behavior is not implemented yet.
     */
    class ResourceContentManager : public ContentManager
    {
    public:
        /**
         * @brief Creates a ResourceContentManager using the supplied service provider.
         *
         * @param serviceProvider Service provider for dependency resolution.
         */
        explicit ResourceContentManager(System::IServiceProvider* serviceProvider);

        /** @brief Destroys the ResourceContentManager. */
        ~ResourceContentManager() override = default;

    protected:
        /**
         * @brief Opens an asset stream from embedded application resources.
         *
         * Not implemented in CNA — throws std::runtime_error.
         *
         * @param assetName Name of the embedded asset.
         * @return Unique pointer to a Stream over the embedded data.
         */
        [[nodiscard]] virtual std::unique_ptr<System::IO::Stream> OpenStream(const std::string& assetName);
    };
}
