// SPDX-License-Identifier: MS-PL
#pragma once

// Shared test scaffolding: a platform whose motion sensors answer from a script.
//
// A build machine has no accelerometer and no gyroscope, so the readings and the enumeration have
// to be supplied. This replaces the per-subsystem `ISystemSensorBackend` seam with the same
// canned-service shape the rest of the platform scaffolding uses.

#include "CNA/Platform/Input/IPlatformSensors.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"

#include <map>
#include <vector>

namespace CNA::Platform::Testing {

    /** @brief A sensor service that reports whatever a test put in it. */
    class CannedSensors final : public IPlatformSensors
    {
    public:
        /**
         * @brief Makes a sensor present and gives it a reading.
         *
         * @param kind Which sensor.
         * @param reading What it reports.
         */
        void Set(const SensorKind kind, const SensorReading& reading) { readings_[kind] = reading; }

        /**
         * @brief Sets the list `GetSensors()` reports.
         *
         * Independent of the readings on purpose: a device can enumerate a sensor whose data is
         * not yet flowing, and a caller must cope with that.
         *
         * @param sensors The list to report.
         */
        void SetEnumeration(std::vector<SensorInfo> sensors) { enumeration_ = std::move(sensors); }

        /** @brief Removes every scripted sensor and reading. */
        void Clear()
        {
            readings_.clear();
            enumeration_.clear();
            started_.clear();
        }

        /** @brief Gets how many times a sensor was started. @param kind Which sensor. @return The count. */
        [[nodiscard]] int StartCount(const SensorKind kind) const
        {
            const auto found = started_.find(kind);
            return found != started_.end() ? found->second : 0;
        }

        /** @brief Gets the scripted enumeration. @return The scripted list. */
        [[nodiscard]] std::vector<SensorInfo> GetSensors() const override { return enumeration_; }

        /**
         * @brief Gets whether a sensor was scripted.
         * @param kind Which sensor.
         * @return True if it has a reading.
         */
        [[nodiscard]] bool IsAvailable(const SensorKind kind) const override
        {
            return readings_.find(kind) != readings_.end();
        }

        /**
         * @brief Records a start, refusing for a sensor that was never scripted.
         * @param kind Which sensor.
         */
        void Start(const SensorKind kind) override
        {
            if (!IsAvailable(kind))
            {
                throw PlatformNotSupportedException(PlatformCapability::Sensors, "Canned");
            }
            ++started_[kind];
        }

        /** @brief Records a stop. @param kind Which sensor. */
        void Stop(const SensorKind kind) override { started_.erase(kind); }

        /**
         * @brief Reads a scripted sensor.
         * @param kind Which sensor.
         * @param reading Receives the scripted reading; untouched when absent.
         * @return True if the sensor was scripted.
         */
        [[nodiscard]] bool TryGetReading(const SensorKind kind, SensorReading& reading) const override
        {
            const auto found = readings_.find(kind);
            if (found == readings_.end())
            {
                return false;
            }
            reading = found->second;
            return true;
        }

    private:
        std::map<SensorKind, SensorReading> readings_;
        std::map<SensorKind, int> started_;
        std::vector<SensorInfo> enumeration_;
    };

    /** @brief A platform that is real in every respect except its motion sensors. */
    class CannedSensorPlatform final : public PlatformTestDecorator
    {
    public:
        /** @brief Gets the scripted sensor service. @return The service; never null. */
        [[nodiscard]] IPlatformSensors* GetSensors() override { return &sensors_; }

        /** @brief Gets the scripted service for writing. @return The scripted service. */
        [[nodiscard]] CannedSensors& Canned() { return sensors_; }

    private:
        CannedSensors sensors_;
    };

} // namespace CNA::Platform::Testing
