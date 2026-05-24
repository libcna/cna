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
        /** Clamps an integer value to the range [min, max]. */
        static intcs Clamp(intcs value, intcs min, intcs max);

        /** Clamps a float value to the range [min, max]. Mirrors FNA MathHelper.Clamp(float,float,float). */
        static float Clamp(float value, float min, float max);

        /**
         * @brief Linearly interpolates between two float values.
         *
         * Mirrors FNA @c MathHelper.Lerp(float, float, float).
         * @param value1 Source value.
         * @param value2 Destination value.
         * @param amount Interpolation factor in [0,1].
         */
        static float Lerp(float value1, float value2, float amount);
    };
}

