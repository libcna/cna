// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "System/IServiceProvider.hpp"
#include "System/IO/Stream.hpp"
#include "System/Resources/ResourceManager.hpp"

namespace Microsoft::Xna::Framework::Content
{
    /**
     * @brief Loads content from application resources rather than loose files.
     *
     * XNA's ResourceContentManager reads assets held in a resource family instead of from the
     * content root: it overrides ContentManager's OpenStream and serves each asset from the
     * `System.Resources.ResourceManager` it was constructed with, which is what makes a game that
     * embeds its compiled content work without shipping a Content directory.
     *
     * Constructed with a resource manager, that is exactly what this does. Constructed with only a
     * service provider -- CNA's own overload, which XNA has no counterpart for -- there is no
     * resource family to read from, and OpenStream says so rather than pretending.
     */
    class ResourceContentManager : public ContentManager
    {
    public:
        /**
         * @brief Creates a ResourceContentManager with no resource family to read from.
         *
         * CNA extension: XNA's only constructor takes a resource manager. A manager built this way
         * has nothing to serve, so OpenStream refuses; it exists because CNA had this overload
         * before the resource-backed one and callers of it are still valid code.
         *
         * @param serviceProvider Service provider for dependency resolution.
         */
        CNAEXT explicit ResourceContentManager(System::IServiceProvider* serviceProvider);

        /**
         * @brief Creates a ResourceContentManager that reads assets from @p resourceManager.
         *
         * The documented XNA constructor. Each asset is a binary resource in that family, looked up
         * by asset name, exactly as XNA looks one up.
         *
         * @param serviceProvider Service provider the ContentManager should use to locate services.
         * @param resourceManager The resource family to read assets from. Must not be null, and must
         *        outlive this manager -- XNA holds a reference to it and so does this.
         * @throws System::ArgumentNullException if @p resourceManager is null.
         */
        ResourceContentManager(System::IServiceProvider* serviceProvider,
                               System::Resources::ResourceManager* resourceManager);

        /** @brief Destroys the ResourceContentManager. */
        ~ResourceContentManager() override = default;

    protected:
        /**
         * @brief Opens an asset stream from the resource family this manager reads from.
         *
         * XNA's override: it asks the resource manager for @p assetName, requires the result to be a
         * `byte[]`, and returns a memory stream over those bytes. A missing resource and a resource
         * that is not binary are both a ContentLoadException, and each says which it was.
         *
         * @param assetName Name of the resource holding the asset.
         * @return A stream over the resource's bytes. Never null.
         * @throws ContentLoadException if this manager has no resource family, if @p assetName names
         *         no resource, or if the resource it names is not binary.
         */
        [[nodiscard]] std::unique_ptr<System::IO::Stream> OpenStream(
            const std::string& assetName) override;

    private:
        System::Resources::ResourceManager* resourceManager_ = nullptr;
    };
}
