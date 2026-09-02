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
        return value1 + (value2 - value1) * amount1 + (value3 - value1) * amount2;
    }

    float MathHelper::CatmullRom(
        float value1,
        float value2,
        float value3,
        float value4,
        float amount
    )
    {
        /* Using formula from http://www.mvps.org/directx/articles/catmull/
         * Internally using doubles not to lose precision.
         */
        const double amountSquared = static_cast<double>(amount * amount);
        const double amountCubed = amountSquared * amount;

        return static_cast<float>(
            0.5 *
            (
                (2.0 * value2 + (value3 - value1) * amount) +
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
        /* All transformed to double not to lose precision
         * Otherwise, for high numbers of param:amount the result is NaN instead
         * of Infinity.
         */
        const double v1 = value1;
        const double v2 = value2;
        const double t1 = tangent1;
        const double t2 = tangent2;
        const double s = amount;

        double result;
        const double sCubed = s * s * s;
        const double sSquared = s * s;

        if (WithinEpsilon(amount, 0.0f))
        {
            result = value1;
        }
        else if (WithinEpsilon(amount, 1.0f))
        {
            result = value2;
        }
        else
        {
            result = (
                ((2.0 * v1 - 2.0 * v2 + t2 + t1) * sCubed) +
                ((3.0 * v2 - 3.0 * v1 - 2.0 * t1 - t2) * sSquared) +
                (t1 * s) +
                v1
            );
        }

        return static_cast<float>(result);
    }

    float MathHelper::Lerp(float value1, float value2, float amount)
    {
        return value1 + (value2 - value1) * amount;
    }

    float MathHelper::Max(float value1, float value2)
    {
        return value1 > value2 ? value1 : value2;
    }

    float MathHelper::Min(float value1, float value2)
    {
        return value1 < value2 ? value1 : value2;
    }

    float MathHelper::SmoothStep(float value1, float value2, float amount)
    {
        float result = Clamp(amount, 0.0f, 1.0f);
        result = Hermite(value1, 0.0f, value2, 0.0f, result);
        return result;
    }

    float MathHelper::ToDegrees(float radians)
    {
        return static_cast<float>(radians * 57.295779513082320876798154814105);
    }

    float MathHelper::ToRadians(float degrees)
    {
        return static_cast<float>(degrees * 0.017453292519943295769236907684886);
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
