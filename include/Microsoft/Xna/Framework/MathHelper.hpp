//
// Created by robertvokac on 6/7/25.
//

#pragma once
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework {
    using SharpRuntime::intcs;

    //static class
    class MathHelper {

    public:
        static intcs Clamp(intcs value, intcs min, intcs max);
    };
}

