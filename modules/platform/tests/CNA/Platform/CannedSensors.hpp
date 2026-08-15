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
#include <set>
#include <vector>

namespace CNA::Platform::Testing {

    /** @brief Polling view over one scripted reading. */
    class CannedSensorSession final : public IPlatformSensorSession
    {
    public:
        /** @brief Stores callbacks into the owning canned service. */
        CannedSensorSession(std::function<bool(SensorReading&)> read,
                            std::function<void()> onDestroy)
            : read_(std::move(read)), onDestroy_(std::move(onDestroy)) {}

        /** @brief Removes the associated callback registration. */
        ~CannedSensorSession() override { onDestroy_(); }

        /** @brief Returns the scripted reading. */
        [[nodiscard]] bool TryGetReading(SensorReading& reading) const override
        {
            return read_(reading);
        }

    private:
        std::function<bool(SensorReading&)> read_;
        std::function<void()> onDestroy_;
    };

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
            startCalls_.clear();
            stopCalls_.clear();
            callbacks_.clear();
        }

        /** @brief Gets how many times a sensor was started. @param kind Which sensor. @return The count. */
        [[nodiscard]] int StartCount(const SensorKind kind) const
        {
            const auto found = startCalls_.find(kind);
            return found != startCalls_.end() ? found->second : 0;
        }

        /** @brief Gets how many active starts were stopped. @param kind Which sensor. @return The count. */
        [[nodiscard]] int StopCount(const SensorKind kind) const
        {
            const auto found = stopCalls_.find(kind);
            return found != stopCalls_.end() ? found->second : 0;
        }

        /** @brief Gets whether the scripted sensor is currently started. */
        [[nodiscard]] bool IsStarted(const SensorKind kind) const { return started_.contains(kind); }

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

        /** @brief Gets the scripted display rotation. */
        [[nodiscard]] SensorDisplayRotation GetDisplayRotation() const override { return rotation; }

        /** @brief Opens a canned stream and retains its callback for explicit test dispatch. */
        [[nodiscard]] std::unique_ptr<IPlatformSensorSession> OpenSensor(
            const SensorKind kind, SensorReadingCallback callback) override
        {
            const auto found = readings_.find(kind);
            if (found == readings_.end())
            {
                return nullptr;
            }
            const std::uint64_t token = nextCallbackToken_++;
            callbacks_[kind][token] = std::move(callback);
            return std::make_unique<CannedSensorSession>(
                [this, kind](SensorReading& reading)
                {
                    const auto current = readings_.find(kind);
                    if (current == readings_.end())
                    {
                        return false;
                    }
                    reading = current->second;
                    return true;
                },
                [this, kind, token]
                {
                    const auto group = callbacks_.find(kind);
                    if (group == callbacks_.end())
                    {
                        return;
                    }
                    group->second.erase(token);
                    if (group->second.empty())
                    {
                        callbacks_.erase(group);
                    }
                });
        }

        /** @brief Delivers one scripted reading through an opened stream's callback. */
        void Dispatch(const SensorKind kind)
        {
            const auto callbackGroup = callbacks_.find(kind);
            const auto reading = readings_.find(kind);
            if (callbackGroup == callbacks_.end() || reading == readings_.end())
            {
                return;
            }

            // Copy before invocation: a handler may destroy its own or another session, which
            // mutates the registered callback map while this call is still on the stack.
            const SensorReading value = reading->second;
            std::vector<SensorReadingCallback> deliveries;
            deliveries.reserve(callbackGroup->second.size());
            for (const auto& [token, callback] : callbackGroup->second)
            {
                (void)token;
                if (callback)
                {
                    deliveries.push_back(callback);
                }
            }
            for (const auto& delivery : deliveries)
            {
                delivery(value);
            }
        }

        /** @brief Scripted current display rotation. */
        SensorDisplayRotation rotation = SensorDisplayRotation::Unknown;

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
            if (started_.insert(kind).second)
            {
                ++startCalls_[kind];
            }
        }

        /** @brief Records a stop. @param kind Which sensor. */
        void Stop(const SensorKind kind) override
        {
            if (started_.erase(kind) != 0)
            {
                ++stopCalls_[kind];
            }
        }

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
        std::set<SensorKind> started_;
        std::map<SensorKind, int> startCalls_;
        std::map<SensorKind, int> stopCalls_;
        std::map<SensorKind, std::map<std::uint64_t, SensorReadingCallback>> callbacks_;
        std::uint64_t nextCallbackToken_ = 1;
        std::vector<SensorInfo> enumeration_;
    };

    /** @brief A platform that is real in every respect except its motion sensors. */
    class CannedSensorPlatform final : public PlatformTestDecorator
    {
    public:
        /** @brief Reports the scripted sensor service as available. */
        [[nodiscard]] PlatformCapabilities GetCapabilities() const override
        {
            PlatformCapabilities capabilities = PlatformTestDecorator::GetCapabilities();
            capabilities.sensors = true;
            return capabilities;
        }

        /** @brief Tracks and forwards sensor-subsystem acquisition. */
        void AcquireSubsystem(const PlatformSubsystem subsystem) override
        {
            PlatformTestDecorator::AcquireSubsystem(subsystem);
            if (subsystem == PlatformSubsystem::Sensor)
            {
                ++sensorSubsystemBalance;
                ++sensorSubsystemAcquisitions;
            }
        }

        /** @brief Tracks and forwards sensor-subsystem release. */
        void ReleaseSubsystem(const PlatformSubsystem subsystem) override
        {
            PlatformTestDecorator::ReleaseSubsystem(subsystem);
            if (subsystem == PlatformSubsystem::Sensor)
            {
                --sensorSubsystemBalance;
            }
        }

        /** @brief Gets the scripted sensor service. @return The service; never null. */
        [[nodiscard]] IPlatformSensors* GetSensors() override { return &sensors_; }

        /** @brief Gets the scripted service for writing. @return The scripted service. */
        [[nodiscard]] CannedSensors& Canned() { return sensors_; }

        /** @brief Outstanding sensor-subsystem references held by public accessors. */
        int sensorSubsystemBalance = 0;
        /** @brief Total sensor-subsystem acquisitions made by public accessors. */
        int sensorSubsystemAcquisitions = 0;

    private:
        CannedSensors sensors_;
    };

} // namespace CNA::Platform::Testing
