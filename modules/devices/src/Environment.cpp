// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Environment.hpp"

#include "CNA/TargetPlatform.hpp"

namespace Microsoft::Devices
{
    DeviceType Environment::getDeviceTypeProperty()
    {
        return CNA::isMobilePlatform() ? DeviceType::Device : DeviceType::Emulator;
    }
}
