//
// Created by robertvokac on 5/25/25.
//

#pragma once
#include <functional>
#include <vector>

#include "SensorReadingEventArgs.hpp"
#include "System/EventHandler.hpp"
#include "Microsoft/Devices/Sensors/SensorBase.hpp"
#include "System/IDisposable.hpp"

namespace Microsoft::Devices::Sensors {
    template<typename TSensorReading>
    class SensorBase: public System::IDisposable {
    public:
        System::EventHandler<SensorReadingEventArgs<TSensorReading>> CurrentValueChanged;

        void Dispose(bool disposing) {
        }
    };
}


