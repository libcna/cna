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

    float MathHelper::Clamp(float value, float min, float max)
    {
        if (value > max) {
            return max;
        }
        if (value < min) {
            return min;
        }
        return value;
    }

    float MathHelper::Lerp(float value1, float value2, float amount)
    {
        return value1 + (value2 - value1) * amount;
    }
}
