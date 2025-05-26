//
// Created by robertvokac on 5/25/25.
//

#ifndef SENSORBASE_H
#define SENSORBASE_H
#include <functional>
#include <vector>

#include "Accelerometer.h"
#include "SensorReadingEventArgs.h"
#include "System/EventHandler.h"

namespace Microsoft::Devices::Sensors {
    template<typename TSensorReading>
    class SensorBase {

    public:
        System::EventHandler<SensorReadingEventArgs<TSensorReading>> CurrentValueChanged;

    };
}


#endif //SENSORBASE_H
