//
// Created by robertvokac on 6/1/25.
//

#include "Microsoft/Devices/Sensors/AccelerometerFailedException.hpp"
namespace Microsoft::Devices::Sensors {

    AccelerometerFailedException::AccelerometerFailedException() : SensorFailedException() {
    }

    AccelerometerFailedException::AccelerometerFailedException(const char * str) : SensorFailedException(str) {

    }
}
