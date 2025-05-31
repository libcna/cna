//
// Created by robertvokac on 5/25/25.
//

#ifndef SENSORREADINGEVENTARGS_H
#define SENSORREADINGEVENTARGS_H
#include "CNA/Prop.h"

namespace Microsoft::Devices::Sensors {
    template<typename T>
    class SensorReadingEventArgs {
        ddata(T, SensorReading)

    public:
        SensorReadingEventArgs();
    };
}

#endif //SENSORREADINGEVENTARGS_H
