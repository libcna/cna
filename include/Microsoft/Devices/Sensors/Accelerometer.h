//
// Created by robertvokac on 5/25/25.
//

#ifndef ACCELEROMETER_H
#define ACCELEROMETER_H
#include "AccelerometerReading.h"
#include "SensorBase.h"

namespace WindowsPhoneSpeedyBlupi {

    using Microsoft::Devices::Sensors::SensorBase;
    using Microsoft::Devices::Sensors::AccelerometerReading;
class Accelerometer : public SensorBase<AccelerometerReading> {
public:
    void Start() { }
    void Stop() { }
    //todo : remove me
    void SimulateNewValue(const AccelerometerReading& reading) {
        RaiseCurrentValueChanged(reading);
    }
};

} // WindowsPhoneSpeedyBlupi

#endif //ACCELEROMETER_H
