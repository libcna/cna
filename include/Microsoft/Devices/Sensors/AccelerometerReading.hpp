//
// Created by robertvokac on 5/25/25.
//
#pragma once
#include "CNA/Prop.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Devices::Sensors {
    using Xna::Framework::Vector3;

    struct AccelerometerReading {
        DEF_PROP(Vector3, Acceleration, getter1, setter0, member1, static0, constret1, ref1, constmet1)
    };
}
