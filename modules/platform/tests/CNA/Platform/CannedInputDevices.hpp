// SPDX-License-Identifier: MS-PL
#pragma once

// Shared test scaffolding: a platform whose input device enumeration answers from a script.
//
// A build machine has no predictable set of mice, keyboards or touchscreens, so any test that
// depends on what is attached has to supply the answer. Before the platform contract each
// subsystem carried its own injectable backend for this; this is the single replacement, and it
// is shared because at least two suites need the same thing (device enumeration itself, and
// TouchPanel's capability probe).

#include "CNA/Platform/Input/IPlatformInputDevices.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"

#include <map>
#include <vector>

namespace CNA::Platform::Testing {

    /** @brief An enumeration service that reports whatever a test put in it. */
    class CannedInputDevices final : public IPlatformInputDevices
    {
    public:
        /**
         * @brief Sets the devices reported for one class, replacing any previous list.
         *
         * @param kind Which class of device.
         * @param devices The devices to report.
         */
        void Set(const InputDeviceKind kind, std::vector<InputDeviceInfo> devices)
        {
            devices_[kind] = std::move(devices);
        }

        /** @brief Removes every scripted device. */
        void Clear() { devices_.clear(); }

        /**
         * @brief Gets the scripted devices of one class.
         *
         * @param kind Which class of device.
         * @return The scripted list, or empty when none was set.
         */
        [[nodiscard]] std::vector<InputDeviceInfo> GetDevices(const InputDeviceKind kind) const override
        {
            const auto found = devices_.find(kind);
            return found != devices_.end() ? found->second : std::vector<InputDeviceInfo>{};
        }

        /**
         * @brief Gets whether any device of a class was scripted.
         *
         * Answered from the same map as GetDevices rather than independently, so the two can
         * never disagree — a fake that could disagree would let a real implementation's
         * disagreement go unnoticed.
         *
         * @param kind Which class of device.
         * @return True if at least one was scripted.
         */
        [[nodiscard]] bool HasDevice(const InputDeviceKind kind) const override
        {
            return !GetDevices(kind).empty();
        }

    private:
        std::map<InputDeviceKind, std::vector<InputDeviceInfo>> devices_;
    };

    /** @brief A platform that is real in every respect except its device enumeration. */
    class CannedInputDevicePlatform final : public PlatformTestDecorator
    {
    public:
        /** @brief Gets the scripted enumeration service. @return The service; never null. */
        [[nodiscard]] IPlatformInputDevices* GetInputDevices() override { return &devices_; }

        /** @brief Gets the scripted service for writing. @return The scripted service. */
        [[nodiscard]] CannedInputDevices& Canned() { return devices_; }

    private:
        CannedInputDevices devices_;
    };

} // namespace CNA::Platform::Testing
