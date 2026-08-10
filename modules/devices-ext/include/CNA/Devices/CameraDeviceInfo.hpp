// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <string>

#include "CNA/Devices/CameraPosition.hpp"

namespace CNA::Devices
{
    /**
     * @brief Describes one camera device returned by `Camera::getAvailableCamerasProperty()`.
     *
     * CNA extension — no XNA/WP7 equivalent exists.
     */
    struct CameraDeviceInfo
    {
        /** @brief Human-readable device name, e.g. "Integrated Webcam". */
        std::string Name;

        /** @brief Front/back-facing, when the platform reports it. */
        CameraPosition Position = CameraPosition::Unknown;
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
