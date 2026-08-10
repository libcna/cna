// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/Curve.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "Microsoft/Xna/Framework/MathHelper.hpp"

namespace Microsoft::Xna::Framework
{
    bool Curve::getIsConstantProperty() const
    {
        return keys.getCountProperty() <= 1;
    }

    CurveKeyCollection& Curve::getKeysProperty()
    {
        return keys;
    }

    const CurveKeyCollection& Curve::getKeysProperty() const
    {
        return keys;
    }

    CurveLoopType Curve::getPostLoopProperty() const
    {
        return postLoop;
    }

    void Curve::setPostLoopProperty(CurveLoopType value)
    {
        postLoop = value;
    }

    CurveLoopType Curve::getPreLoopProperty() const
    {
        return preLoop;
    }

    void Curve::setPreLoopProperty(CurveLoopType value)
    {
        preLoop = value;
    }

    Curve::Curve()
        : keys(), postLoop(CurveLoopType::Constant), preLoop(CurveLoopType::Constant)
    {
    }

    Curve::Curve(const CurveKeyCollection& keys)
        : keys(keys), postLoop(CurveLoopType::Constant), preLoop(CurveLoopType::Constant)
    {
    }

    Curve Curve::Clone() const
    {
        Curve curve(keys.Clone());
        curve.setPreLoopProperty(preLoop);
        curve.setPostLoopProperty(postLoop);
        return curve;
    }

    float Curve::Evaluate(float position) const
    {
        if (keys.getCountProperty() == 0)
        {
            return 0.0f;
        }
        if (keys.getCountProperty() == 1)
        {
            return keys.getItemProperty(0).getValueProperty();
        }

        const CurveKey& first = keys.getItemProperty(0);
        const CurveKey& last = keys.getItemProperty(keys.getCountProperty() - 1);

        if (position < first.getPositionProperty())
        {
            switch (preLoop)
            {
            case CurveLoopType::Constant:
                return first.getValueProperty();

            case CurveLoopType::Linear:
                return first.getValueProperty() - first.getTangentInProperty() * (first.getPositionProperty() -
                    position);

            case CurveLoopType::Cycle:
                {
                    const int cycle = GetNumberOfCycle(position);
                    const float virtualPos = position - (cycle * (last.getPositionProperty() - first.
                        getPositionProperty()));
                    return GetCurvePosition(virtualPos);
                }

            case CurveLoopType::CycleOffset:
                {
                    const int cycle = GetNumberOfCycle(position);
                    const float virtualPos = position - (cycle * (last.getPositionProperty() - first.
                        getPositionProperty()));
                    return GetCurvePosition(virtualPos) + cycle * (last.getValueProperty() - first.getValueProperty());
                }

            case CurveLoopType::Oscillate:
                {
                    const int cycle = GetNumberOfCycle(position);
                    float virtualPos = 0.0f;
                    if (cycle % 2 == 0)
                    {
                        virtualPos = position - (cycle * (last.getPositionProperty() - first.getPositionProperty()));
                    }
                    else
                    {
                        virtualPos = last.getPositionProperty() - position + first.getPositionProperty() + (cycle * (
                            last.getPositionProperty() - first.getPositionProperty()));
                    }
                    return GetCurvePosition(virtualPos);
                }
            }
        }
        else if (position > last.getPositionProperty())
        {
            switch (postLoop)
            {
            case CurveLoopType::Constant:
                return last.getValueProperty();

            case CurveLoopType::Linear:
                return last.getValueProperty() + first.getTangentOutProperty() * (position - last.
                    getPositionProperty());

            case CurveLoopType::Cycle:
                {
                    const int cycle = GetNumberOfCycle(position);
                    const float virtualPos = position - (cycle * (last.getPositionProperty() - first.
                        getPositionProperty()));
                    return GetCurvePosition(virtualPos);
                }

            case CurveLoopType::CycleOffset:
                {
                    const int cycle = GetNumberOfCycle(position);
                    const float virtualPos = position - (cycle * (last.getPositionProperty() - first.
                        getPositionProperty()));
                    return GetCurvePosition(virtualPos) + cycle * (last.getValueProperty() - first.getValueProperty());
                }

            case CurveLoopType::Oscillate:
                {
                    const int cycle = GetNumberOfCycle(position);
                    float virtualPos = position - (cycle * (last.getPositionProperty() - first.getPositionProperty()));
                    if (cycle % 2 != 0)
                    {
                        virtualPos = last.getPositionProperty() - position + first.getPositionProperty() + (cycle * (
                            last.getPositionProperty() - first.getPositionProperty()));
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
        for (int i = 0; i < keys.getCountProperty(); i += 1)
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
        // FNA has no bounds check here; C++ adds one to avoid undefined behaviour
        if (keyIndex < 0 || keyIndex >= keys.getCountProperty())
        {
            throw std::out_of_range("Curve keyIndex is out of range");
        }

        CurveKey& key = keys.getItemProperty(keyIndex);

        float p0 = key.getPositionProperty();
        float p = key.getPositionProperty();
        float p1 = key.getPositionProperty();

        float v0 = key.getValueProperty();
        float v = key.getValueProperty();
        float v1 = key.getValueProperty();

        if (keyIndex > 0)
        {
            p0 = keys.getItemProperty(keyIndex - 1).getPositionProperty();
            v0 = keys.getItemProperty(keyIndex - 1).getValueProperty();
        }

        if (keyIndex < keys.getCountProperty() - 1)
        {
            p1 = keys.getItemProperty(keyIndex + 1).getPositionProperty();
            v1 = keys.getItemProperty(keyIndex + 1).getValueProperty();
        }

        switch (tangentInType)
        {
        case CurveTangent::Flat:
            key.setTangentInProperty(0.0f);
            break;

        case CurveTangent::Linear:
            key.setTangentInProperty(v - v0);
            break;

        case CurveTangent::Smooth:
            {
                const float pn = p1 - p0;
                if (MathHelper::WithinEpsilon(pn, 0.0f))
                {
                    key.setTangentInProperty(0.0f);
                }
                else
                {
                    key.setTangentInProperty((v1 - v0) * ((p - p0) / pn));
                }
                break;
            }
        }

        switch (tangentOutType)
        {
        case CurveTangent::Flat:
            key.setTangentOutProperty(0.0f);
            break;

        case CurveTangent::Linear:
            key.setTangentOutProperty(v1 - v);
            break;

        case CurveTangent::Smooth:
            {
                const float pn = p1 - p0;
                if (std::fabs(pn) < std::numeric_limits<float>::denorm_min())
                {
                    key.setTangentOutProperty(0.0f);
                }
                else
                {
                    key.setTangentOutProperty((v1 - v0) * ((p1 - p) / pn));
                }
                break;
            }
        }
    }

    int Curve::GetNumberOfCycle(float position) const
    {
        const CurveKey& first = keys.getItemProperty(0);
        const CurveKey& last = keys.getItemProperty(keys.getCountProperty() - 1);
        float cycle = (position - first.getPositionProperty()) / (last.getPositionProperty() - first.
            getPositionProperty());
        if (cycle < 0.0f)
        {
            cycle -= 1.0f;
        }
        return static_cast<int>(cycle);
    }

    float Curve::GetCurvePosition(float position) const
    {
        CurveKey prev = keys.getItemProperty(0);
        for (int i = 1; i < keys.getCountProperty(); i += 1)
        {
            CurveKey next = keys.getItemProperty(i);
            if (next.getPositionProperty() >= position)
            {
                if (prev.getContinuityProperty() == CurveContinuity::Step)
                {
                    if (position >= 1.0f)
                    {
                        return next.getValueProperty();
                    }
                    return prev.getValueProperty();
                }

                const float t = (position - prev.getPositionProperty()) / (next.getPositionProperty() - prev.
                    getPositionProperty());
                const float ts = t * t;
                const float tss = ts * t;

                return (2.0f * tss - 3.0f * ts + 1.0f) * prev.getValueProperty() +
                    (tss - 2.0f * ts + t) * prev.getTangentOutProperty() +
                    (3.0f * ts - 2.0f * tss) * next.getValueProperty() +
                    (tss - ts) * next.getTangentInProperty();
            }
            prev = next;
        }
        return 0.0f;
    }
}
