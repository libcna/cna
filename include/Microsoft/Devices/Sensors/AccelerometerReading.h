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
        private: Vector3 AccelerationProperty_ = Vector3();

    public:
        [[nodiscard]] Vector3 AccelerationProperty() const;
        AccelerometerReading();
    };
}
#endif //ACCELEROMETERREADING_H
