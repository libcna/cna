// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Input/Sensors.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <vector>

// Internal (CNA) seam over SDL3's host-device sensor API (SDL_sensor), used by CNA::Input::Sensors.
// This exists ONLY so sensor enumeration/reads can be injected in tests (CI has no motion sensors).
// Production uses the real SDL-backed implementation (open -> read -> close).
namespace CNA::Internal::Input
{
    /**
     * @brief Injectable seam over the SDL3 host-device sensor API.
     */
    struct ISystemSensorBackend
    {
        virtual ~ISystemSensorBackend() = default;

        /** @brief Enumerates the host device's sensors (SDL_GetSensors + name/type getters). */
        virtual std::vector<CNA::Input::SensorInfoEXT> GetSensors() = 0;

        /**
         * @brief Reads the current data of the first sensor of the given kind.
         * @param type The sensor kind to read (Accelerometer or Gyroscope).
         * @param out Output receiving the 3-axis sample.
         * @return True if such a sensor was read; false if none is present.
         */
        virtual bool ReadSensor(CNA::Input::SensorTypeEXT type, Microsoft::Xna::Framework::Vector3& out) = 0;
    };

    /**
     * @brief Returns the active system sensor backend (the real SDL one unless overridden).
     */
    ISystemSensorBackend& system_sensor_backend();

    /**
     * @brief Test-only: swaps in a fake sensor backend; pass nullptr to restore the real one.
     */
    void SetSystemSensorBackendForTests(ISystemSensorBackend* backend);
}
