// SPDX-License-Identifier: MS-PL

#pragma once

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework
{
    using SharpRuntime::intcs;

    /** @brief Contains commonly used precalculated values and mathematical operations. */
    class MathHelper final
    {
    public:
        /** @brief Deleted; MathHelper is a static utility class. */
        NOXNA MathHelper() = delete;

        /** @brief Mathematical constant e. */
        static constexpr float E = 2.71828175f;

        /** @brief Base-10 logarithm of e. */
        static constexpr float Log10E = 0.4342945f;

        /** @brief Base-2 logarithm of e. */
        static constexpr float Log2E = 1.442695f;

        /** @brief The value of pi. */
        static constexpr float Pi = 3.14159274f;

        /** @brief Pi divided by two. */
        static constexpr float PiOver2 = 1.57079637f;

        /** @brief Pi divided by four. */
        static constexpr float PiOver4 = 0.7853982f;

        /** @brief Pi multiplied by two. */
        static constexpr float TwoPi = 6.28318548f;

        // FNA marks the four members below as 'internal'; C++ has no assembly-scope visibility,
        // so they are public here. Other framework types (e.g. Vector2, Matrix) rely on them.

        /** @brief Float epsilon used by framework-level approximate comparisons. */
        static const float MachineEpsilonFloat;

        /**
         * @brief Computes one coordinate of a point expressed with barycentric coordinates.
         *
         * @param value1 The first value in the interpolation.
         * @param value2 The second value in the interpolation.
         * @param value3 The third value in the interpolation.
         * @param amount1 Barycentric scalar b2.
         * @param amount2 Barycentric scalar b3.
         * @return The Cartesian coordinate corresponding to the barycentric inputs.
         */
        static float Barycentric(float value1, float value2, float value3, float amount1, float amount2);

        /**
         * @brief Performs Catmull-Rom interpolation between the supplied positions.
         *
         * @param value1 The first position.
         * @param value2 The second position.
         * @param value3 The third position.
         * @param value4 The fourth position.
         * @param amount Weighting factor.
         * @return The result of the Catmull-Rom interpolation.
         */
        static float CatmullRom(float value1, float value2, float value3, float value4, float amount);

        /**
         * @brief Restricts a floating-point value to the inclusive range [min, max].
         *
         * @param value The value to clamp.
         * @param min The minimum bound.
         * @param max The maximum bound.
         * @return The clamped value.
         */
        static float Clamp(float value, float min, float max);

        /**
         * @brief Returns the absolute distance between two scalar values.
         *
         * @param value1 The first value.
         * @param value2 The second value.
         * @return The absolute difference between the two values.
         */
        static float Distance(float value1, float value2);

        /**
         * @brief Performs Hermite spline interpolation.
         *
         * @param value1 Source position.
         * @param tangent1 Source tangent.
         * @param value2 Destination position.
         * @param tangent2 Destination tangent.
         * @param amount Weighting factor.
         * @return The result of the Hermite spline interpolation.
         */
        static float Hermite(float value1, float tangent1, float value2, float tangent2, float amount);

        /**
         * @brief Linearly interpolates between two values.
         *
         * @param value1 Source value.
         * @param value2 Destination value.
         * @param amount Value between 0 and 1 indicating the interpolation weight.
         * @return The linearly interpolated value.
         */
        static float Lerp(float value1, float value2, float amount);

        /**
         * @brief Returns the larger of two values.
         *
         * @param value1 The first value.
         * @param value2 The second value.
         * @return The larger of the two values.
         */
        static float Max(float value1, float value2);

        /**
         * @brief Returns the smaller of two values.
         *
         * @param value1 The first value.
         * @param value2 The second value.
         * @return The smaller of the two values.
         */
        static float Min(float value1, float value2);

        /**
         * @brief Interpolates between two values using a cubic smoothing curve.
         *
         * @param value1 Source value.
         * @param value2 Destination value.
         * @param amount Weighting value between 0 and 1.
         * @return The smooth-stepped interpolation result.
         */
        static float SmoothStep(float value1, float value2, float amount);

        /**
         * @brief Converts radians to degrees.
         *
         * @param radians The angle in radians.
         * @return The angle in degrees.
         */
        static float ToDegrees(float radians);

        /**
         * @brief Converts degrees to radians.
         *
         * @param degrees The angle in degrees.
         * @return The angle in radians.
         */
        static float ToRadians(float degrees);

        /**
         * @brief Wraps an angle in radians to the range (-pi, pi].
         *
         * @param angle The angle in radians to wrap.
         * @return The wrapped angle.
         */
        static float WrapAngle(float angle);

        /**
         * @brief Framework-internal integer clamp helper.
         *
         * @param value The value to clamp.
         * @param min The minimum bound.
         * @param max The maximum bound.
         * @return The clamped integer value.
         */
        static intcs Clamp(intcs value, intcs min, intcs max);

        /**
         * @brief Returns true when two floats are closer than the framework epsilon.
         *
         * @param floatA The first float value.
         * @param floatB The second float value.
         * @return @c true if the values are within epsilon of each other; @c false otherwise.
         */
        static bool WithinEpsilon(float floatA, float floatB);

        /**
         * @brief Returns the closest valid MSAA power-of-two value at or below the input.
         *
         * @param value The input sample count.
         * @return The largest power-of-two MSAA value that does not exceed @p value.
         */
        static intcs ClosestMSAAPower(intcs value);

    private:
        static float GetMachineEpsilonFloat();
    };
}
