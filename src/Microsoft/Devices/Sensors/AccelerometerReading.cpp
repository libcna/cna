//
// Created by robertvokac on 5/25/25.
//

#include "Microsoft/Devices/Sensors/AccelerometerReading.h"

namespace Microsoft::Devices::Sensors {
    Vector3 AccelerometerReading::AccelerationProperty() const {
        return AccelerationProperty_;
    }

    AccelerometerReading::AccelerometerReading() {
    }
}