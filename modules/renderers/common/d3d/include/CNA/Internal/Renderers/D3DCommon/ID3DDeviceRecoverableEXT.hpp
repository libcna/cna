// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Internal::Renderers::D3DCommon
{
    /**
     * @brief Internal contract for a renderer-owned resource that survives D3D device recreation.
     *
     * The owning renderer calls ReleaseDeviceResourcesEXT before releasing the old device, then
     * RecreateDeviceResourcesEXT after constructing the replacement device. Implementations retain
     * only API-independent descriptions and CPU shadows between those calls.
     */
    class ID3DDeviceRecoverableEXT
    {
    public:
        /** @brief Destroys every native object associated with the old D3D device. */
        virtual void ReleaseDeviceResourcesEXT() noexcept = 0;

        /** @brief Recreates native objects against the replacement D3D device. */
        virtual void RecreateDeviceResourcesEXT() = 0;

    protected:
        /** @brief Allows destruction through implementing resource classes only. */
        ~ID3DDeviceRecoverableEXT() = default;
    };
}
