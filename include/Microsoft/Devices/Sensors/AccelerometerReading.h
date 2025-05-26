//
// Created by robertvokac on 5/25/25.
//

#ifndef ACCELEROMETERREADING_H
#define ACCELEROMETERREADING_H
#include "NeoSdk/Property.h"
#include "Microsoft/Xna/Framework/Vector3.h"

namespace Microsoft::Devices::Sensors {

struct AccelerometerReading {
    using Microsoft::Xna::Framework::Vector3;
    DEF_PROP_AUTO(Vector3, Acceleration, Vector3())
    AccelerometerReading() : IMPL_PROP_AUTO(Vector3, Acceleration) {
    }
};

}
#endif //ACCELEROMETERREADING_H
