//
// Created by robertvokac on 5/25/25.
//

#ifndef SENSORBASE_H
#define SENSORBASE_H
#include <functional>
#include <vector>

#include "SensorReadingEventArgs.h"
#include "System/EventHandler.h"
#include "Microsoft/Devices/Sensors/SensorBase.h"

namespace Microsoft::Devices::Sensors {
    template<typename TSensorReading>
    class SensorBase {

    public:
        System::EventHandler<SensorReadingEventArgs<TSensorReading>> CurrentValueChanged;

    };
}


#endif //SENSORBASE_H
