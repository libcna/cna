//
// Created by robertvokac on 5/25/25.
//

#ifndef ACCELEROMETERREADING_H
#define ACCELEROMETERREADING_H
#include "CNA/Prop.h"
#include "Microsoft/Xna/Framework/Vector3.h"

namespace Microsoft::Devices::Sensors {
    using Xna::Framework::Vector3;

    struct AccelerometerReading {
        DEF_PROP(Vector3, Acceleration, getter1, setter0, member1, static0, constret1, ref1, constmet1)
    };
}
#endif //ACCELEROMETERREADING_H
