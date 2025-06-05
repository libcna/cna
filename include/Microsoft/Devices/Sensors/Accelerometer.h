//
// Created by robertvokac on 5/25/25.
//

#ifndef ACCELEROMETER_H
#define ACCELEROMETER_H
#include <SDL3/SDL_sensor.h>

#include "SensorState.h"
#include "CNA/CnaHelper.h"
#include "Microsoft/Devices/Sensors/AccelerometerReading.h"
#include "Microsoft/Devices/Sensors/SensorBase.h"

namespace Microsoft::Devices::Sensors {
    using Microsoft::Devices::Sensors::SensorBase;
    using Microsoft::Devices::Sensors::AccelerometerReading;

    class Accelerometer final : public SensorBase<AccelerometerReading> {
    private: static SDL_Sensor* g_sensor;
        ////
    private: static int instanceCount;
    private: static std::vector<Accelerometer> instances;

    private: static constexpr CNA::byte MaxSensorCount = 1;

        /**
         * Getter for property: Supported
         *
         * @return boolean
         */
    public: [[nodiscard]] static bool getIsSupportedProperty();
    public: [[nodiscard]] SensorState getStateProperty() const;
    public: Accelerometer();
    public:
        void Start();

        void Stop();

        void Dispose(bool disposing) override;


        //todo : remove me
        // void SimulateNewValue(const AccelerometerReading& reading) {
        //     //RaiseCurrentValueChanged(reading);
        // }
    };
} // Microsoft::Devices::Sensors

#endif //ACCELEROMETER_H
