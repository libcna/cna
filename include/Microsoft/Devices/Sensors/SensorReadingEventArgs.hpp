//
// Created by robertvokac on 5/25/25.
//
#pragma once
#include <algorithm>

#include "CNA/Prop.hpp"

namespace Microsoft::Devices::Sensors {
    template<typename T>
    class SensorReadingEventArgs {
    private:
        DEF_MEMBER(T,SensorReading)
    public:
        SensorReadingEventArgs() : SensorReading_{} {}
        //IMPL_PROP(T, SensorReading, getter1, setter1, member0, static0, constret1, ref1, constmet1, SensorReadingEventArgs, nothing)
        const T &getSensorReadingProperty() const { return SensorReading_; }
        void setSensorReadingProperty(const T &v) { SensorReading_ = v; }
        void setSensorReadingProperty(T &&v) { SensorReading_ = std::move(v); }

    };


}

