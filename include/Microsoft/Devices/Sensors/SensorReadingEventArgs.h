//
// Created by robertvokac on 5/25/25.
//

#ifndef SENSORREADINGEVENTARGS_H
#define SENSORREADINGEVENTARGS_H
#include "CNA/Prop.h"

namespace Microsoft::Devices::Sensors {
    template<typename T>
    class SensorReadingEventArgs {
    private:
        T SensorReading_;


    public:
        [[nodiscard]] T getSensorReadingProperty() const {
            return SensorReading_;
        }

        void setSensorReadingProperty(const T &v) {
            SensorReading_ = v;
        }

        SensorReadingEventArgs() : SensorReading_{} {}
    };
}

#endif //SENSORREADINGEVENTARGS_H
