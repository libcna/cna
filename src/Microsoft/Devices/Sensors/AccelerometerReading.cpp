//
// Created by robertvokac on 5/25/25.
//

#include "Microsoft/Devices/Sensors/AccelerometerReading.h"

namespace Microsoft::Devices::Sensors {
    const Vector3& AccelerometerReading::getAccelerationProperty() const { return Acceleration_ ; }
}