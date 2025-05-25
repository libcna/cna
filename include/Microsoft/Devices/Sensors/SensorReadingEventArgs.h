//
// Created by robertvokac on 5/25/25.
//

#ifndef SENSORREADINGEVENTARGS_H
#define SENSORREADINGEVENTARGS_H
#include "NeoSdk/Property.h"


namespace Microsoft::Devices::Sensors {
    template<typename T>
    class SensorReadingEventArgs {


    public:
        DEF_PROP_AUTO(T, SensorReading, T())

        SensorReadingEventArgs():
            IMPL_PROP_AUTO(T, SensorReading) {
        }
    };
}


#endif //SENSORREADINGEVENTARGS_H
