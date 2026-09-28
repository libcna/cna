// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "CNA/Platform/Input/IPlatformSensors.hpp"

namespace CNA::Platform
{
    class IPlatform;

    /** @brief Shared opt-in software sensor; no physical sensor subsystem is required. */
    class KeyboardAccelerometer
    {
    public:
        /** @brief Gets whether keyboard readings replace the primary accelerometer.
         * @return True when software readings are enabled. */
        [[nodiscard]] static bool IsEnabled();
        /** @brief Changes mode. Throws InvalidOperationException while a stream is open.
         * @param enabled True to use keyboard readings. */
        static void SetEnabled(bool enabled);
        /** @brief Gets the software sensor service.
         * @return The process-owned software service. */
        [[nodiscard]] static IPlatformSensors& GetSensors();
        /** @brief Selects the software primary accelerometer or the platform's real service.
         * @param platform Platform supplying physical sensors.
         * @param kind Sensor kind to resolve.
         * @return The selected service, or null when unavailable. */
        [[nodiscard]] static IPlatformSensors* Resolve(IPlatform& platform, SensorKind kind);
        /** @brief Publishes one frame: arrows tilt a normalized 1 g vector; unfocused is neutral.
         * @param keyboard The current frame snapshot.
         * @param focused True when the game accepts keyboard input. */
        static void Update(const KeyboardSnapshot& keyboard, bool focused);
    };
}
