#include "Microsoft/Xna/Framework/Curve.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace Microsoft::Xna::Framework
{
    namespace
    {
        float MachineEpsilonFloat()
        {
            static const float epsilon = []()
            {
                float machineEpsilon = 1.0f;
                float comparison = 0.0f;
                do
                {
                    machineEpsilon *= 0.5f;
                    comparison = 1.0f + machineEpsilon;
                }
                while (comparison > 1.0f);
                return machineEpsilon;
            }();
            return epsilon;
        }

        bool WithinEpsilon(float a, float b)
        {
            return std::fabs(a - b) < MachineEpsilonFloat();
        }

        bool WithinSingleEpsilon(float value)
        {
            return std::fabs(value) < std::numeric_limits<float>::denorm_min();
        }
    }

    Curve::Curve()
        : Keys(), PostLoop(CurveLoopType::Constant), PreLoop(CurveLoopType::Constant)
    {
    }

    Curve::Curve(const CurveKeyCollection& keys)
        : Keys(keys), PostLoop(CurveLoopType::Constant), PreLoop(CurveLoopType::Constant)
    {
    }

    bool Curve::IsConstant() const
    {
        return Keys.Count() <= 1;
    }

    bool Curve::getIsConstantProperty() const
    {
        return IsConstant();
    }

    Curve Curve::Clone() const
    {
        Curve curve(Keys.Clone());
        curve.PreLoop = PreLoop;
        curve.PostLoop = PostLoop;
        return curve;
    }

    float Curve::Evaluate(float position) const
    {
        if (Keys.Count() == 0)
        {
            return 0.0f;
        }
        if (Keys.Count() == 1)
        {
            return Keys[0].Value;
        }

        const CurveKey& first = Keys[0];
        const CurveKey& last = Keys[Keys.Count() - 1];

        if (position < first.Position)
        {
            switch (PreLoop)
            {
                case CurveLoopType::Constant:
                    return first.Value;

                case CurveLoopType::Linear:
                    return first.Value - first.TangentIn * (first.Position - position);

                case CurveLoopType::Cycle:
                {
                    const int cycle = GetNumberOfCycle(position);
                    const float virtualPos = position - (cycle * (last.Position - first.Position));
                    return GetCurvePosition(virtualPos);
                }

                case CurveLoopType::CycleOffset:
                {
                    const int cycle = GetNumberOfCycle(position);
                    const float virtualPos = position - (cycle * (last.Position - first.Position));
                    return GetCurvePosition(virtualPos) + cycle * (last.Value - first.Value);
                }

                case CurveLoopType::Oscillate:
                {
                    const int cycle = GetNumberOfCycle(position);
                    float virtualPos = 0.0f;
                    if (cycle % 2 == 0)
                    {
                        virtualPos = position - (cycle * (last.Position - first.Position));
                    }
                    else
                    {
                        virtualPos = last.Position - position + first.Position + (cycle * (last.Position - first.Position));
                    }
                    return GetCurvePosition(virtualPos);
                }
            }
        }
        else if (position > last.Position)
        {
            switch (PostLoop)
            {
                case CurveLoopType::Constant:
                    return last.Value;

                case CurveLoopType::Linear:
                    return last.Value + first.TangentOut * (position - last.Position);

                case CurveLoopType::Cycle:
                {
                    const int cycle = GetNumberOfCycle(position);
                    const float virtualPos = position - (cycle * (last.Position - first.Position));
                    return GetCurvePosition(virtualPos);
                }

                case CurveLoopType::CycleOffset:
                {
                    const int cycle = GetNumberOfCycle(position);
                    const float virtualPos = position - (cycle * (last.Position - first.Position));
                    return GetCurvePosition(virtualPos) + cycle * (last.Value - first.Value);
                }

                case CurveLoopType::Oscillate:
                {
                    const int cycle = GetNumberOfCycle(position);
                    float virtualPos = position - (cycle * (last.Position - first.Position));
                    if (cycle % 2 != 0)
                    {
                        virtualPos = last.Position - position + first.Position + (cycle * (last.Position - first.Position));
                    }
                    return GetCurvePosition(virtualPos);
                }
            }
        }

        return GetCurvePosition(position);
    }

    void Curve::ComputeTangents(CurveTangent tangentType)
    {
        ComputeTangents(tangentType, tangentType);
    }

    void Curve::ComputeTangents(CurveTangent tangentInType, CurveTangent tangentOutType)
    {
        for (int i = 0; i < Keys.Count(); i += 1)
        {
            ComputeTangent(i, tangentInType, tangentOutType);
        }
    }

    void Curve::ComputeTangent(int keyIndex, CurveTangent tangentType)
    {
        ComputeTangent(keyIndex, tangentType, tangentType);
    }

    void Curve::ComputeTangent(int keyIndex, CurveTangent tangentInType, CurveTangent tangentOutType)
    {
        if (keyIndex < 0 || keyIndex >= Keys.Count())
        {
            throw std::out_of_range("Curve keyIndex is out of range");
        }

        CurveKey& key = Keys[keyIndex];

        float p0 = key.Position;
        float p = key.Position;
        float p1 = key.Position;

        float v0 = key.Value;
        float v = key.Value;
        float v1 = key.Value;

        if (keyIndex > 0)
        {
            p0 = Keys[keyIndex - 1].Position;
            v0 = Keys[keyIndex - 1].Value;
        }

        if (keyIndex < Keys.Count() - 1)
        {
            p1 = Keys[keyIndex + 1].Position;
            v1 = Keys[keyIndex + 1].Value;
        }

        switch (tangentInType)
        {
            case CurveTangent::Flat:
                key.TangentIn = 0.0f;
                break;

            case CurveTangent::Linear:
                key.TangentIn = v - v0;
                break;

            case CurveTangent::Smooth:
            {
                const float pn = p1 - p0;
                if (WithinEpsilon(pn, 0.0f))
                {
                    key.TangentIn = 0.0f;
                }
                else
                {
                    key.TangentIn = (v1 - v0) * ((p - p0) / pn);
                }
                break;
            }
        }

        switch (tangentOutType)
        {
            case CurveTangent::Flat:
                key.TangentOut = 0.0f;
                break;

            case CurveTangent::Linear:
                key.TangentOut = v1 - v;
                break;

            case CurveTangent::Smooth:
            {
                const float pn = p1 - p0;
                if (WithinSingleEpsilon(pn))
                {
                    key.TangentOut = 0.0f;
                }
                else
                {
                    key.TangentOut = (v1 - v0) * ((p1 - p) / pn);
                }
                break;
            }
        }
    }

    int Curve::GetNumberOfCycle(float position) const
    {
        float cycle = (position - Keys[0].Position) /
            (Keys[Keys.Count() - 1].Position - Keys[0].Position);
        if (cycle < 0.0f)
        {
            cycle -= 1.0f;
        }
        return static_cast<int>(cycle);
    }

    float Curve::GetCurvePosition(float position) const
    {
        CurveKey prev = Keys[0];
        for (int i = 1; i < Keys.Count(); i += 1)
        {
            const CurveKey& next = Keys[i];
            if (next.Position >= position)
            {
                if (prev.Continuity == CurveContinuity::Step)
                {
                    if (position >= 1.0f)
                    {
                        return next.Value;
                    }
                    return prev.Value;
                }

                const float t = (position - prev.Position) / (next.Position - prev.Position);
                const float ts = t * t;
                const float tss = ts * t;

                return ((2.0f * tss - 3.0f * ts + 1.0f) * prev.Value) +
                       ((tss - 2.0f * ts + t) * prev.TangentOut) +
                       ((3.0f * ts - 2.0f * tss) * next.Value) +
                       ((tss - ts) * next.TangentIn);
            }
            prev = next;
        }
        return 0.0f;
    }
}
