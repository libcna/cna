// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <cmath>

namespace Microsoft::Xna::Framework
{
    const float MathHelper::MachineEpsilonFloat = MathHelper::GetMachineEpsilonFloat();

    float MathHelper::Barycentric(
        float value1,
        float value2,
        float value3,
        float amount1,
        float amount2
    )
    {
        // XNA MathHelper.Barycentric IL_0000-000e: narrow only at the return.
        return static_cast<float>(static_cast<double>(value1) + (static_cast<double>(value2) - value1) * amount1 + (static_cast<double>(value3) - value1) * amount2);
    }

    float MathHelper::CatmullRom(
        float value1,
        float value2,
        float value3,
        float value4,
        float amount
    )
    {
        // XNA Refresh MathHelper.CatmullRom IL_0000-0053:
        // keep the value difference wide with the surrounding polynomial.
        /* Using formula from http://www.mvps.org/directx/articles/catmull/
         * Internally using doubles not to lose precision.
         */
        const double amountSquared = static_cast<double>(amount * amount);
        const double amountCubed = amountSquared * amount;

        return static_cast<float>(
            0.5 *
            (
                (2.0 * value2 + (static_cast<double>(value3) - value1) * amount) +
                ((2.0 * value1 - 5.0 * value2 + 4.0 * value3 - value4) * amountSquared) +
                ((3.0 * value2 - value1 - 3.0 * value3 + value4) * amountCubed)
            )
        );
    }

    float MathHelper::Clamp(float value, float min, float max)
    {
        value = (value > max) ? max : value;
        value = (value < min) ? min : value;
        return value;
    }

    float MathHelper::Distance(float value1, float value2)
    {
        return std::fabs(value1 - value2);
    }

    float MathHelper::Hermite(
        float value1,
        float tangent1,
        float value2,
        float tangent2,
        float amount
    )
    {
        // XNA MathHelper.Hermite IL_0000–0056 evaluates the four basis products
        // even at endpoints; FNA's epsilon shortcuts hide 0*Inf/NaN and tiny tangents.
        const double s = amount;
        const double sSquared = s * s;
        const double sCubed = sSquared * s;
        const double h1 = 2.0 * sCubed - 3.0 * sSquared + 1.0;
        const double h2 = -2.0 * sCubed + 3.0 * sSquared;
        const double h3 = sCubed - 2.0 * sSquared + s;
        const double h4 = sCubed - sSquared;
        return static_cast<float>(value1 * h1 + value2 * h2 + tangent1 * h3 + tangent2 * h4);
    }

    float MathHelper::Lerp(float value1, float value2, float amount)
    {
        // XNA MathHelper.Lerp IL_0000-0007: no intermediate Single store.
        return static_cast<float>(static_cast<double>(value1) + (static_cast<double>(value2) - value1) * amount);
    }

    float MathHelper::Max(float value1, float value2)
    {
        // XNA's scalar helper propagates a first-operand NaN, unlike vector Max.
        return std::isnan(value1) || value1 > value2 ? value1 : value2;
    }

    float MathHelper::Min(float value1, float value2)
    {
        // XNA's scalar helper propagates a first-operand NaN, unlike vector Min.
        return std::isnan(value1) || value1 < value2 ? value1 : value2;
    }

    float MathHelper::SmoothStep(float value1, float value2, float amount)
    {
        // XNA MathHelper.SmoothStep IL_0011–0024 uses Lerp, preserving its
        // nonfinite endpoint arithmetic instead of FNA's Hermite shortcut.
        const float t = Clamp(amount, 0.0f, 1.0f);
        return Lerp(value1, value2, t * t * (3.0f - 2.0f * t));
    }

    float MathHelper::ToDegrees(float radians)
    {
        // The conversion factor is `180 / Pi` computed in single precision from XNA's own `Pi`,
        // not the exactly rounded 57.29578: over 31 measured angles that form answers every one
        // and `radians * 57.29577951308232f` answers 12 of them
        // (tests/reference/xna40/framework/matrix-oracle.txt, `todegrees/*`;
        // `XNASWEEP-172`).
        return radians * (180.0f / Pi);
    }

    float MathHelper::ToRadians(float degrees)
    {
        // A single-precision multiplication by a single-precision constant. Multiplying in
        // `double` by the exact factor and narrowing once is a different answer, by an ulp, on an
        // angle small enough for it to show (`toradians/1eneg07`).
        return degrees * 0.017453292519943295f;
    }

    float MathHelper::WrapAngle(float angle)
    {
        if ((angle > -Pi) && (angle <= Pi))
        {
            return angle;
        }

        // FNA uses the C# % operator; std::fmod is equivalent for float modulo.
        angle = std::fmod(angle, TwoPi);

        if (angle <= -Pi)
        {
            return angle + TwoPi;
        }

        if (angle > Pi)
        {
            return angle - TwoPi;
        }

        return angle;
    }

    intcs MathHelper::Clamp(intcs value, intcs min, intcs max)
    {
        value = (value > max) ? max : value;
        value = (value < min) ? min : value;
        return value;
    }

    bool MathHelper::WithinEpsilon(float floatA, float floatB)
    {
        return std::fabs(floatA - floatB) < MachineEpsilonFloat;
    }

    intcs MathHelper::ClosestMSAAPower(intcs value)
    {
        /* Checking for the highest power of two _after_ than the given int:
         * http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
         * Take result, divide by 2, get the highest power of two _before_!
         * -flibit
         */
        if (value == 1)
        {
            // ... Except for 1, which is invalid for MSAA -flibit
            return 0;
        }

        intcs result = value - 1;
        result |= result >> 1;
        result |= result >> 2;
        result |= result >> 4;
        result |= result >> 8;
        result |= result >> 16;
        result += 1;

        if (result == value)
        {
            return result;
        }

        return result >> 1;
    }

    float MathHelper::GetMachineEpsilonFloat()
    {
        float machineEpsilon = 1.0f;
        float comparison;

        /* Keep halving the working value of machineEpsilon until we get a number that
         * when added to 1.0f will still evaluate as equal to 1.0f.
         */
        do
        {
            machineEpsilon *= 0.5f;
            comparison = 1.0f + machineEpsilon;
        }
        while (comparison > 1.0f);

        return machineEpsilon;
    }
}
