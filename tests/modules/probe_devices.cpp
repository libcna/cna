// SPDX-License-Identifier: MS-PL
// Minimal-link probe for CNA::Devices (plans/MODULARIZATION_PLAN.md §4/§11): the XNA-compatible
// device base (Microsoft::Devices sensors/vibration) must be usable without the CNA device
// extensions (devices-ext), graphics extensions or networking. The paired
// ModuleLinkClosure_Devices ctest asserts neither extension archive appears.
#include "Microsoft/Devices/Sensors/AccelerometerReading.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"

#include <cstdio>

int main()
{
    using namespace Microsoft::Devices::Sensors;

    AccelerometerReading reading;
    std::printf("devices probe: accel x=%.1f state=%d\n",
                static_cast<double>(reading.getAccelerationProperty().X),
                static_cast<int>(SensorState::NotSupported));
    return 0;
}
