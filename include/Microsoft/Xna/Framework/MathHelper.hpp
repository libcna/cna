//
// Created by robertvokac on 6/7/25.
//

#pragma once
#include "CNA/CnaHelper.hpp"

namespace Microsoft::Xna::Framework {
    using CNA::intcs;

    //static class
    class MathHelper {

    public:
        static intcs Clamp(intcs value, intcs min, intcs max);
    };
}

