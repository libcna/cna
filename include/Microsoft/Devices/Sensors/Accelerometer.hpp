//
// Created by robertvokac on 5/25/25.
//

#pragma once
#include <SDL3/SDL_sensor.h>

#include "SensorState.hpp"
#include "CNA/CnaHelper.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerReading.hpp"
#include "Microsoft/Devices/Sensors/SensorBase.hpp"

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
        DEF_PROP(bool, IsSupported, getter1, setter0, member0, static1, constret0, ref0, constmet0)
        DEF_PROP(SensorState, State, getter1, setter0, member0, static0, constret0, ref0, constmet1)

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

