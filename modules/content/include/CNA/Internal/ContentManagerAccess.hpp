// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

namespace Microsoft::Xna::Framework::Content
{
    class ContentManager;
    class ContentReader;
}

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
}

namespace CNA::Internal
{
    /** @brief Internal path access for the C adapter and built-in media loaders. */
    struct ContentManagerAccess
    {
        /**
         * @brief Builds an asset path using the manager's content root.
         * @param manager Content manager whose path policy applies.
         * @param assetName Logical asset name.
         * @return The manager's asset path.
         */
        [[nodiscard]] static std::string BuildAssetPath(
            const Microsoft::Xna::Framework::Content::ContentManager& manager, const std::string& assetName);

        /**
         * @brief Applies the manager's existing-file path resolution.
         * @param manager Content manager whose path policy applies.
         * @param path Candidate asset path.
         * @return The resolved path.
         */
        [[nodiscard]] static std::string ResolveExistingAssetPath(
            const Microsoft::Xna::Framework::Content::ContentManager& manager, const std::string& path);

        /**
         * @brief Normalizes a logical asset name using the manager's cache-key policy.
         * @param manager Content manager whose key policy applies.
         * @param assetName Logical asset name.
         * @return The normalized cache key.
         */
        [[nodiscard]] static std::string NormalizeKey(
            const Microsoft::Xna::Framework::Content::ContentManager& manager, const std::string& assetName);

        /**
         * @brief The device an `.xnb` graphics reader creates its resource on: Refresh
         *        `GraphicsContentHelper.GraphicsDeviceFromContentReader`, beside CNA's injection.
         *
         * A device given to the reader's manager with the CNAEXT `setGraphicsDevice` is used
         * first, without consulting the service provider. Otherwise the device comes from the
         * manager's `IGraphicsDeviceService`, looked up on every call as Refresh does
         * (IL_0000-001f).
         *
         * @param input The reader whose asset needs the device.
         * @return The device.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException naming the reader's
         *         asset when the provider has no `IGraphicsDeviceService` (IL_0021-0036) or the
         *         service has no device (IL_003f-0053): `Error loading "{0}". GraphicsDevice
         *         component not found.` (FrameworkResources.NoGraphicsDeviceContent).
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Graphics::GraphicsDevice&
        GraphicsDeviceFromContentReader(const Microsoft::Xna::Framework::Content::ContentReader& input);
    };
} // namespace CNA::Internal
