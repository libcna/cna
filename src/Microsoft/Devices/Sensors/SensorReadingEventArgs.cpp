//
// Created by robertvokac on 5/25/25.
//

#include "Microsoft/Devices/Sensors/SensorReadingEventArgs.h"

namespace Microsoft::Devices::Sensors {

    template<typename T>
    T SensorReadingEventArgs<T>::getSensorReading() const { return SensorReading_; }

    template<typename T>
    void SensorReadingEventArgs<T>::setSensorReading(const T& property) { SensorReading_ = property; }

    template<typename T>
    SensorReadingEventArgs<T>::SensorReadingEventArgs() : SensorReading_{} {
    }
}
