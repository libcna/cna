//
// Created by robertvokac on 6/7/25.
//

#include "Microsoft/Xna/Framework/MathHelper.hpp"

namespace Microsoft::Xna::Framework {
    intcs MathHelper::Clamp(const intcs value, const intcs min, const intcs max)
    {
        if (value > max) {
            return max;
        }
        if (value < min) {
            return min;
        }
        return value;
    }
}
