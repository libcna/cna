// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformHaptics.hpp"
#include "CNA/Platform/Input/IPlatformSensors.hpp"

#include <map>
#include <vector>

namespace CNA::Platform::Sdl3 {

    /** @brief SDL3-backed motion sensors. */
    class Sdl3Sensors final : public IPlatformSensors
    {
    public:
        /** @brief Closes any sensor this service opened. */
        ~Sdl3Sensors() override;

        /**
         * @brief Gets whether a sensor is present.
         * @param kind Which sensor.
         * @return True if the device exposes it.
         */
        [[nodiscard]] bool IsAvailable(SensorKind kind) const override;
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

    private:
        std::map<SensorKind, void*> open_;
    };

    /** @brief SDL3-backed standalone haptic devices. */
    class Sdl3Haptics final : public IPlatformHaptics
    {
    public:
        /** @brief Closes every device this service opened. */
        ~Sdl3Haptics() override;

        /** @brief Gets the connected device count. @return The count. */
        [[nodiscard]] int GetCount() const override;
        /**
         * @brief Gets a device's name.
         * @param index The device to name.
         * @return The name, or empty for an invalid index.
         */
        [[nodiscard]] std::string GetName(int index) const override;
        /**
         * @brief Gets whether a device supports simple rumble.
         * @param index The device to query.
         * @return True if supported.
         */
        [[nodiscard]] bool SupportsRumble(int index) const override;
        /**
         * @brief Plays a rumble effect.
         * @param index The device to rumble.
         * @param strength Intensity in [0, 1]; clamped.
         * @param durationMilliseconds How long to rumble.
         * @return True if accepted.
         */
        bool PlayRumble(int index, float strength, std::uint32_t durationMilliseconds) override;
        /**
         * @brief Stops a rumble effect.
         * @param index The device to stop.
         * @return True if accepted.
         */
        bool StopRumble(int index) override;

    private:
        /// Opened lazily and cached: SDL_OpenHaptic is not cheap, and VibrateController calls
        /// Start/Stop repeatedly on the same device.
        void* Acquire(int index);
        void CloseAll();

        std::map<int, void*> open_;
        mutable std::vector<std::uint32_t> ids_;
    };

} // namespace CNA::Platform::Sdl3
