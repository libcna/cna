// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Input
{
    /**
     * @brief CNAEXT — the kind of a host-device motion sensor, using CNA-owned vocabulary.
     */
    CNAEXT enum class SensorTypeEXT
    {
        /** @brief Unknown or unrecognized sensor. */
        Unknown,
        /** @brief Accelerometer (reports acceleration in m/s²). */
        Accelerometer,
        /** @brief Gyroscope (reports angular velocity in rad/s). */
        Gyroscope,
        /** @brief Left-side accelerometer (dual-sensor controllers). */
        AccelerometerLeft,
        /** @brief Left-side gyroscope (dual-sensor controllers). */
        GyroscopeLeft,
        /** @brief Right-side accelerometer (dual-sensor controllers). */
        AccelerometerRight,
        /** @brief Right-side gyroscope (dual-sensor controllers). */
        GyroscopeRight
    };

    /**
     * @brief CNAEXT — identity of one enumerated host-device motion sensor.
     */
    CNAEXT struct SensorInfoEXT
    {
        /** @brief The platform sensor instance id. */
        std::uint32_t id = 0;
        /** @brief The sensor's human-readable name, or empty if SDL reports none. */
        std::string name;
        /** @brief The sensor's kind. */
        SensorTypeEXT type = SensorTypeEXT::Unknown;
    };

    /**
     * @brief Compares two sensor descriptors for equality (id, name, and type).
     * @param left The left operand.
     * @param right The right operand.
     * @return True if all fields are equal.
     */
    [[nodiscard]] inline bool operator==(const SensorInfoEXT& left, const SensorInfoEXT& right)
    {
        return left.id == right.id && left.name == right.name && left.type == right.type;
    }

    /**
     * @brief Compares two sensor descriptors for inequality.
     * @param left The left operand.
     * @param right The right operand.
     * @return True if the descriptors differ.
     */
    [[nodiscard]] inline bool operator!=(const SensorInfoEXT& left, const SensorInfoEXT& right)
    {
        return !(left == right);
    }

    /**
     * @brief CNAEXT — reads the host device's own motion sensors through the selected platform.
     *
     * Distinct from a gamepad's gyro/accelerometer (`GamePad::Get{Gyro,Accelerometer}EXT`): these are
     * the machine's own sensors (phone/tablet/laptop). Values use standard units — accelerometer
     * in m/s², gyroscope in rad/s. Platform notes: mainly a mobile feature (Android full; Windows/Linux
     * partial; macOS none; Web permission-gated), so the enumeration is often empty on desktop.
     */
    CNAEXT class Sensors
    {
    public:
        /** @brief Static-only utility; not instantiable. */
        Sensors() = delete;

        /**
         * @brief Enumerates the host device's motion sensors.
         * @return A list of sensor id/name/type descriptors (empty if none or unsupported).
         */
        CNAEXT [[nodiscard]] static std::vector<SensorInfoEXT> GetSensorsEXT();

        /**
         * @brief Reads the current data of the first accelerometer.
         * @param acceleration Output receiving the acceleration vector in m/s².
         * @return True if an accelerometer was read; false if none is present.
         */
        CNAEXT static bool GetAccelerometerEXT(Microsoft::Xna::Framework::Vector3& acceleration);

        /**
         * @brief Reads the current data of the first gyroscope.
         * @param angularVelocity Output receiving the angular velocity vector in rad/s.
         * @return True if a gyroscope was read; false if none is present.
         */
        CNAEXT static bool GetGyroscopeEXT(Microsoft::Xna::Framework::Vector3& angularVelocity);
    };
}
