// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class GraphicsResource;
}

namespace CNA::Internal::Graphics
{
    /**
     * @brief Arms deferred GraphicsDevice.ResourceCreated for the next resource built on @p device.
     *
     * Microsoft XNA raises ResourceCreated from DeviceResourceManager::AddTrackedObject
     * (IL_00dc-00e3), the last step of each tracked resource's public constructor -- after its
     * argument validation and native creation (VertexBuffer::.ctor IL_0019-002c, then IL_005e;
     * OcclusionQuery::.ctor IL_0019-0040, then IL_009e). A tracked type passes its device through
     * this function in its GraphicsResource/Texture base initializer: that base constructor then
     * registers the resource without announcing it, and the most-derived constructor calls
     * PublishResourceCreated() as its last statement. A constructor that throws before then has
     * announced nothing and, never announced, raises no ResourceDestroyed either.
     *
     * @param device The device the base constructor is about to receive.
     * @return @p device, unchanged.
     */
    Microsoft::Xna::Framework::Graphics::GraphicsDevice* DeferResourceCreated(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice* device) noexcept;

    /**
     * @brief Raises GraphicsDevice.ResourceCreated for a finished resource whose announcement was
     *        deferred; does nothing for one already announced or never deferred.
     * @param resource The resource whose most-derived constructor has completed its work.
     */
    void PublishResourceCreated(Microsoft::Xna::Framework::Graphics::GraphicsResource& resource);
}
