// SPDX-License-Identifier: MS-PL

#include "X11InputDevices.hpp"

#include "X11Display.hpp"
#include "X11Error.hpp"

#include <algorithm>
#include <utility>

namespace CNA::Platform::X11 {

    std::vector<InputDeviceKind> ClassifyX11InputDevice(const X11InputDeviceFacts& device)
    {
#if defined(CNA_X11_HAVE_XI)
        if (!device.enabled || device.name.find("XTEST") != std::string::npos)
        {
            return {};
        }
        if (device.use == XISlaveKeyboard)
        {
            return {InputDeviceKind::Keyboard};
        }
        if (device.use == XISlavePointer)
        {
            if (device.touch)
            {
                return {InputDeviceKind::Mouse, InputDeviceKind::Touch};
            }
            return {InputDeviceKind::Mouse};
        }
#else
        (void) device;
#endif
        return {};
    }

    std::vector<DeviceEvent> DiffX11InputDevices(const std::map<DeviceId, std::vector<InputDeviceKind>>& before,
                                                 const std::map<DeviceId, std::vector<InputDeviceKind>>& after)
    {
        std::vector<DeviceEvent> events;
        for (const auto& [id, kinds] : before)
        {
            const auto now = after.find(id);
            for (const InputDeviceKind kind : kinds)
            {
                if (now == after.end() || std::find(now->second.begin(), now->second.end(), kind) == now->second.end())
                {
                    events.push_back(DeviceEvent{id, kind, false});
                }
            }
        }
        for (const auto& [id, kinds] : after)
        {
            const auto then = before.find(id);
            for (const InputDeviceKind kind : kinds)
            {
                if (then == before.end() ||
                    std::find(then->second.begin(), then->second.end(), kind) == then->second.end())
                {
                    events.push_back(DeviceEvent{id, kind, true});
                }
            }
        }
        return events;
    }

    X11InputDevices::X11InputDevices(X11Connection& connection, ControllerSource controllers)
        : connection_(connection), controllers_(std::move(controllers))
    {
        known_ = KindsOf(Query());
    }

    std::vector<X11InputDevices::Device> X11InputDevices::Query() const
    {
        std::vector<Device> result;
#if defined(CNA_X11_HAVE_XI)
        if (connection_.GetXInput2Opcode() < 0)
        {
            return result;
        }
        Display* display = connection_.GetDisplay();
        X11ErrorTrap trap(display);
        int count = 0;
        XIDeviceInfo* devices = XIQueryDevice(display, XIAllDevices, &count);
        trap.Sync();
        if (devices == nullptr)
        {
            return result;
        }
        for (int index = 0; index < count; ++index)
        {
            const XIDeviceInfo& device = devices[index];
            X11InputDeviceFacts facts;
            facts.id = device.deviceid;
            facts.use = device.use;
            facts.name = device.name != nullptr ? device.name : "";
            facts.enabled = device.enabled != 0;
#if defined(XI_TouchBegin)
            for (int item = 0; item < device.num_classes; ++item)
            {
                facts.touch = facts.touch || device.classes[item]->type == XITouchClass;
            }
#endif
            std::vector<InputDeviceKind> kinds = ClassifyX11InputDevice(facts);
            if (kinds.empty())
            {
                continue;
            }
            Device entry;
            entry.info.id = kX11InputDeviceIdBase + static_cast<DeviceId>(device.deviceid);
            entry.info.name = facts.name;
            entry.kinds = std::move(kinds);
            result.push_back(std::move(entry));
        }
        XIFreeDeviceInfo(devices);
#endif
        return result;
    }

    std::map<DeviceId, std::vector<InputDeviceKind>> X11InputDevices::KindsOf(const std::vector<Device>& devices)
    {
        std::map<DeviceId, std::vector<InputDeviceKind>> kinds;
        for (const Device& device : devices)
        {
            kinds[device.info.id] = device.kinds;
        }
        return kinds;
    }

    std::vector<InputDeviceInfo> X11InputDevices::GetDevices(const InputDeviceKind kind) const
    {
        if (kind == InputDeviceKind::Gamepad || kind == InputDeviceKind::Joystick)
        {
            return controllers_ ? controllers_(kind) : std::vector<InputDeviceInfo>();
        }
        std::vector<InputDeviceInfo> devices;
        for (const Device& device : Query())
        {
            if (std::find(device.kinds.begin(), device.kinds.end(), kind) != device.kinds.end())
            {
                InputDeviceInfo info = device.info;
                info.kind = kind;
                devices.push_back(std::move(info));
            }
        }
        return devices;
    }

    bool X11InputDevices::HasDevice(const InputDeviceKind kind) const
    {
        return !GetDevices(kind).empty();
    }

    void X11InputDevices::HandleHierarchyChanged(std::vector<PlatformEvent>& destination)
    {
        std::map<DeviceId, std::vector<InputDeviceKind>> now = KindsOf(Query());
        for (const DeviceEvent& change : DiffX11InputDevices(known_, now))
        {
            destination.emplace_back(change);
        }
        known_ = std::move(now);
    }

} // namespace CNA::Platform::X11
