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
        dgetter(Vector3, Acceleration)
    };
}
#endif //ACCELEROMETERREADING_H
