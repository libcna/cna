// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformJoystick.hpp"

#include "PlatformTestDecorator.hpp"

#include <map>
#include <utility>

namespace CNA::Platform::Testing {

    /** @brief Deterministic raw-joystick service shared by input and haptic tests. */
    class CannedJoystick final : public IPlatformJoystick
    {
    public:
        /** @brief Adds or replaces a connected device with a pending state. */
        void Connect(JoystickInfo info, JoystickCapabilities capabilities,
                     JoystickSnapshot state = {})
        {
            capabilities.connected = true;
            capabilities.kind = info.kind;
            capabilities.name = info.name;
            devices_[info.id] = Device{std::move(info), std::move(capabilities), state, state};
        }

        /** @brief Removes a connected device. */
        void Disconnect(const DeviceId id) { devices_.erase(id); }

        /** @brief Replaces the state copied by the next Update(). */
        void SetPending(const DeviceId id, JoystickSnapshot state)
        {
            const auto item = devices_.find(id);
            if (item != devices_.end())
            {
                item->second.pending = std::move(state);
            }
        }

        /** @brief Copies every pending state into the published frame. */
        void Update() override
        {
            ++updateCount;
            for (auto& [id, device] : devices_)
            {
                (void)id;
                device.current = device.pending;
            }
        }

        /** @brief Gets descriptors in ascending id order. */
        [[nodiscard]] std::vector<JoystickInfo> GetJoysticks() const override
        {
            std::vector<JoystickInfo> result;
            result.reserve(devices_.size());
            for (const auto& [id, device] : devices_)
            {
                (void)id;
                result.push_back(device.info);
            }
            return result;
        }

        /** @brief Gets whether the id is connected. */
        [[nodiscard]] bool IsConnected(const DeviceId id) const override
        {
            return devices_.contains(id);
        }

        /** @brief Gets capabilities or a default disconnected value. */
        [[nodiscard]] JoystickCapabilities GetCapabilities(const DeviceId id) const override
        {
            const auto item = devices_.find(id);
            return item != devices_.end() ? item->second.capabilities : JoystickCapabilities{};
        }

        /** @brief Gets the current snapshot or an empty value. */
        [[nodiscard]] JoystickSnapshot GetSnapshot(const DeviceId id) const override
        {
            const auto item = devices_.find(id);
            return item != devices_.end() ? item->second.current : JoystickSnapshot{};
        }

        /** @brief Number of snapshot publication calls. */
        int updateCount = 0;

    private:
        struct Device
        {
            JoystickInfo info;
            JoystickCapabilities capabilities;
            JoystickSnapshot current;
            JoystickSnapshot pending;
        };

        std::map<DeviceId, Device> devices_;
    };

    /** @brief Platform decorator exposing one canned raw-joystick service. */
    class CannedJoystickPlatform final : public PlatformTestDecorator
    {
    public:
        /** @brief Gets the canned joystick service. */
        [[nodiscard]] IPlatformJoystick* GetJoystick() override { return &joystick; }

        /** @brief Reports the service/capability pairing required by the root contract. */
        [[nodiscard]] PlatformCapabilities GetCapabilities() const override
        {
            PlatformCapabilities capabilities = PlatformTestDecorator::GetCapabilities();
            capabilities.joystick = true;
            return capabilities;
        }

        /** @brief Mutable deterministic service controlled by the test. */
        CannedJoystick joystick;
    };

} // namespace CNA::Platform::Testing
