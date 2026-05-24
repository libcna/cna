#pragma once

#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework
{
    using SharpRuntime::intcs;

    /// Common mathematical constants and helper operations used by the framework.
    class MathHelper final
    {
    public:
        MathHelper() = delete;

        /// Mathematical constant e.
        static constexpr float E = 2.71828175f;

        /// Base-10 logarithm of e.
        static constexpr float Log10E = 0.4342945f;

        /// Base-2 logarithm of e.
        static constexpr float Log2E = 1.442695f;

        /// The value of pi.
        static constexpr float Pi = 3.14159274f;

        /// Pi divided by two.
        static constexpr float PiOver2 = 1.57079637f;

        /// Pi divided by four.
        static constexpr float PiOver4 = 0.7853982f;

        /// Pi multiplied by two.
        static constexpr float TwoPi = 6.28318548f;

        /// Float epsilon used by framework-level approximate comparisons.
        static const float MachineEpsilonFloat;

        /// Computes one coordinate of a point expressed with barycentric coordinates.
        static float Barycentric(float value1, float value2, float value3, float amount1, float amount2);

        /// Performs Catmull-Rom interpolation between the supplied positions.
        static float CatmullRom(float value1, float value2, float value3, float value4, float amount);

        /// Restricts a floating-point value to the inclusive range [min, max].
        static float Clamp(float value, float min, float max);

        /// Returns the absolute distance between two scalar values.
        static float Distance(float value1, float value2);

        /// Performs Hermite spline interpolation.
        static float Hermite(float value1, float tangent1, float value2, float tangent2, float amount);

        /// Linearly interpolates between two values.
        static float Lerp(float value1, float value2, float amount);

        /// Returns the larger of two values.
        static float Max(float value1, float value2);

        /// Returns the smaller of two values.
        static float Min(float value1, float value2);

        /// Interpolates between two values using a cubic smoothing curve.
        static float SmoothStep(float value1, float value2, float amount);

        /// Converts radians to degrees.
        static float ToDegrees(float radians);

        /// Converts degrees to radians.
        static float ToRadians(float degrees);

        /// Wraps an angle in radians to the range (-pi, pi].
        static float WrapAngle(float angle);

        /// Framework-internal integer clamp helper.
        static intcs Clamp(intcs value, intcs min, intcs max);

        /// Returns true when two floats are closer than the framework epsilon.
        static bool WithinEpsilon(float floatA, float floatB);

        /// Returns the closest valid MSAA power-of-two value at or below the input.
        static intcs ClosestMSAAPower(intcs value);

    private:
        static float GetMachineEpsilonFloat();
    };
}
