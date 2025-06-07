//
// Created by robertvokac on 5/28/25.
//

#include "System/Random.h"

namespace System {
    Random::Random() : generator(std::random_device{}()) {
    }

    csint Random::Next(const csint& maxValue) {
        return Next(0, maxValue);
    }

    csint Random::Next(const csint& minValue, const csint& maxValue) {
        std::uniform_int_distribution<int> distribution(minValue, maxValue - 1);
        return distribution(generator);
    }

    csint Random::Next() {
        return Next(0, CSINT_MAX);
    }
} // System
