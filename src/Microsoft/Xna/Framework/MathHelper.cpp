//
// Created by robertvokac on 6/7/25.
//

#include "Microsoft/Xna/Framework/MathHelper.h"

namespace Microsoft::Xna::Framework {
    int MathHelper::Clamp(int value, const int& min, const int& max)
    {
        value = value > max ? max : value;
        value = value < min ? min : value;
        return value;
    }
}

