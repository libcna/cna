#pragma once

#include "Microsoft/Xna/Framework/CurveKeyCollection.hpp"
#include "Microsoft/Xna/Framework/CurveLoopType.hpp"
#include "Microsoft/Xna/Framework/CurveTangent.hpp"

namespace Microsoft::Xna::Framework
{
    /// A collection of CurveKey control points that can be evaluated as a one-dimensional curve.
    class Curve
    {
    public:
        /// The collection of keys that define this curve.
        CurveKeyCollection Keys;

        /// Loop behavior for positions after the last key.
        CurveLoopType PostLoop;

        /// Loop behavior for positions before the first key.
        CurveLoopType PreLoop;

        /// Constructs an empty curve.
        Curve();

        /// Returns true when the curve has zero or one key.
        [[nodiscard]] bool IsConstant() const;
        [[nodiscard]] bool getIsConstantProperty() const;

        /// Creates a copy of this curve.
        [[nodiscard]] Curve Clone() const;

        /// Evaluates the curve value at the given position.
        [[nodiscard]] float Evaluate(float position) const;

        /// Computes matching in/out tangents for every key.
        void ComputeTangents(CurveTangent tangentType);

        /// Computes in/out tangents for every key using separate tangent modes.
        void ComputeTangents(CurveTangent tangentInType, CurveTangent tangentOutType);

        /// Computes matching in/out tangents for a single key.
        void ComputeTangent(int keyIndex, CurveTangent tangentType);

        /// Computes in/out tangents for a single key using separate tangent modes.
        void ComputeTangent(int keyIndex, CurveTangent tangentInType, CurveTangent tangentOutType);

    private:
        explicit Curve(const CurveKeyCollection& keys);

        [[nodiscard]] int GetNumberOfCycle(float position) const;
        [[nodiscard]] float GetCurvePosition(float position) const;
    };
}
