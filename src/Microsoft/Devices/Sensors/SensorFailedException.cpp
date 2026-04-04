//
// Created by robertvokac on 6/1/25.
//

#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
namespace Microsoft::Devices::Sensors {

    SensorFailedException::SensorFailedException() : Exception() {
    }

    SensorFailedException::SensorFailedException(const char * str) : System::Exception(str) {

    }
}
