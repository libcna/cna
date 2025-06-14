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
#include "System/IDisposable.h"

namespace Microsoft::Devices::Sensors {
    template<typename TSensorReading>
    class SensorBase: public System::IDisposable {
    public:
        System::EventHandler<SensorReadingEventArgs<TSensorReading>> CurrentValueChanged;

        void Dispose(bool disposing) {
        }
    };
}


#endif //SENSORBASE_H
