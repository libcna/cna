//
// Created by robertvokac on 5/26/25.
//

#pragma once
#include "SensorFailedException.hpp"

namespace Microsoft::Devices::Sensors {
    class AccelerometerFailedException : public SensorFailedException {
    public:
        AccelerometerFailedException();

        explicit AccelerometerFailedException(const char * str);
    };
};

