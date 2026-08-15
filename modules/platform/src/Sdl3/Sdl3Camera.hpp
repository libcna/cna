// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformCamera.hpp"

namespace CNA::Platform::Sdl3 {

    /** @brief SDL3-backed camera enumeration and RGBA8 capture sessions. */
    class Sdl3CameraProvider final : public IPlatformCameraProvider
    {
    public:
        /**
         * @brief Gets whether this SDL build contains at least one camera driver.
         * @return True when camera operations have a native implementation.
         */
        [[nodiscard]] static bool IsSupported();

        /** @brief Enumerates connected SDL cameras. @return Their stable descriptions. */
        [[nodiscard]] std::vector<PlatformCameraInfo> GetCameras() const override;

        /**
         * @brief Opens an SDL camera and requests RGBA8 conversion.
         * @param id The SDL camera instance id in CNA's width-independent representation.
         * @return The session, or null when it cannot be opened.
         */
        [[nodiscard]] std::unique_ptr<IPlatformCamera> OpenCamera(CameraId id) override;
    };

} // namespace CNA::Platform::Sdl3
