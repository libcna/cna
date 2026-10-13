// SPDX-License-Identifier: MS-PL
// Camera::getIsSupportedProperty().
//
// Link boundary: asking whether the platform supports it must not link the platform's camera
// service, which the rest of Camera enables (Camera.cpp; docs/link-size-architecture.md).
#include "CNA/Devices/Camera.hpp"

#ifdef CNA_DEVICES

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"

namespace CNA::Devices
{
    bool Camera::getIsSupportedProperty()
    {
        return CNA::Platform::GetCurrentPlatform().Supports(
            CNA::Platform::PlatformCapability::Camera);
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
