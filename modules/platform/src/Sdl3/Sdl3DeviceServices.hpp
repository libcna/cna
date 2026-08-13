// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformHaptics.hpp"
#include "CNA/Platform/Input/IPlatformSensors.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace CNA::Platform::Sdl3 {

    /** @brief SDL3-backed motion sensors. */
    class Sdl3Sensors final : public IPlatformSensors
    {
    public:
        /** @brief Closes any sensor this service opened. */
        ~Sdl3Sensors() override;

        /**
         * @brief Lists every sensor the device exposes.
         * @return The attached sensors; empty when none are present.
         */
        [[nodiscard]] std::vector<SensorInfo> GetSensors() const override;
        /**
         * @brief Gets whether a sensor is present.
         * @param kind Which sensor.
         * @return True if the device exposes it.
         */
        [[nodiscard]] bool IsAvailable(SensorKind kind) const override;
        /** @brief Gets the primary display's current rotation. */
        [[nodiscard]] SensorDisplayRotation GetDisplayRotation() const override;
        /** @brief Opens an independently owned sensor stream. */
        [[nodiscard]] std::unique_ptr<IPlatformSensorSession> OpenSensor(
            SensorKind kind, SensorReadingCallback callback) override;
        /**
         * @brief Opens a sensor and begins delivering readings.
         * @param kind Which sensor.
         */
        void Start(SensorKind kind) override;
        /**
         * @brief Closes a sensor.
         * @param kind Which sensor.
         */
        void Stop(SensorKind kind) override;
        /**
         * @brief Gets the most recent reading.
         * @param kind Which sensor.
         * @param reading Receives the reading; untouched on false.
         * @return True if a reading was available.
         */
        [[nodiscard]] bool TryGetReading(SensorKind kind, SensorReading& reading) const override;

        /** @brief Closes cached handles before the sensor subsystem's final release. */
        void Deactivate();

    private:
        void CloseAll();
        std::shared_ptr<std::recursive_mutex> nativeMutex_ =
            std::make_shared<std::recursive_mutex>();
        std::map<SensorKind, void*> open_;
    };

    /** @brief SDL3-backed standalone haptic devices. */
    class Sdl3Haptics final : public IPlatformHaptics
    {
    public:
        /** @brief Closes every device this service opened. */
        ~Sdl3Haptics() override;

        /** @brief Gets connected haptic descriptors in ascending id order. */
        [[nodiscard]] std::vector<HapticInfo> GetHaptics() const override;
        /** @brief Gets the first suitable non-gamepad vibration device. */
        [[nodiscard]] std::optional<HapticInfo> GetDefaultVibrationDevice() const override;
        /** @brief Gets whether an id is currently connected. */
        [[nodiscard]] bool IsConnected(DeviceId id) const override;
        /**
         * @brief Gets whether a device supports simple rumble.
         * @param id The device to query.
         * @return True if supported.
         */
        [[nodiscard]] bool SupportsRumble(DeviceId id) const override;
        /** @brief Opens and initializes simple rumble for an id. */
        bool InitializeRumble(DeviceId id) override;
        /**
         * @brief Plays a rumble effect.
         * @param id The device to rumble.
         * @param strength Intensity in [0, 1]; clamped.
         * @param durationMilliseconds How long to rumble.
         * @return True if accepted.
         */
        bool PlayRumble(DeviceId id, float strength,
                        std::uint32_t durationMilliseconds) override;
        /** @brief Plays a two-motor effect, when the device supports one. */
        bool PlayLeftRight(DeviceId id, float largeMotor, float smallMotor,
                           std::uint32_t durationMilliseconds) override;
        /**
         * @brief Stops a rumble effect.
         * @param id The device to stop.
         * @return True if accepted.
         */
        bool StopRumble(DeviceId id) override;
        /** @brief Stops every active effect owned by this service. */
        bool StopAll(DeviceId id) override;

        /** @brief Closes cached handles before the haptic subsystem's final release. */
        void Deactivate();

    private:
        /// Opened lazily and cached: SDL_OpenHaptic is not cheap, and VibrateController calls
        /// Start/Stop repeatedly on the same device.
        void* Acquire(DeviceId id);
        void CloseAll();
        [[nodiscard]] std::vector<DeviceId> EnumerateIds() const;
        void RetireMissing(const std::vector<DeviceId>& ids) const;
        [[nodiscard]] bool ProbeRumble(DeviceId id) const;
        void DestroyLeftRight(DeviceId id);

        mutable std::map<DeviceId, void*> open_;
        mutable std::map<DeviceId, bool> rumbleInitialized_;
        mutable std::map<DeviceId, int> leftRightEffects_;
        mutable std::recursive_mutex mutex_;
    };

} // namespace CNA::Platform::Sdl3
