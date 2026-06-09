// SPDX-License-Identifier: MS-PL

#pragma once

#include "Microsoft/Xna/Framework/CurveKeyCollection.hpp"
#include "Microsoft/Xna/Framework/CurveLoopType.hpp"
#include "Microsoft/Xna/Framework/CurveTangent.hpp"

namespace Microsoft::Xna::Framework
{
    /// Collection of CurveKey points that can be evaluated as a curve.
    class Curve
    {
    public:
        /// Gets true when this curve has zero or one keys.
        [[nodiscard]] bool getIsConstantProperty() const;

        /// Gets the collection of curve keys.
        [[nodiscard]] CurveKeyCollection& getKeysProperty();
        [[nodiscard]] const CurveKeyCollection& getKeysProperty() const;

        /// Gets or sets how values after the last control point are handled.
        [[nodiscard]] CurveLoopType getPostLoopProperty() const;
        void setPostLoopProperty(CurveLoopType value);

        /// Gets or sets how values before the first control point are handled.
        [[nodiscard]] CurveLoopType getPreLoopProperty() const;
        void setPreLoopProperty(CurveLoopType value);

        /// Constructs an empty curve.
        Curve();

        /// Creates a copy of this curve.
        [[nodiscard]] Curve Clone() const;

        /// Evaluates the curve value at the given position.
        [[nodiscard]] float Evaluate(float position) const;

        /// Computes matching in/out tangents for all keys.
        void ComputeTangents(CurveTangent tangentType);

        /// Computes separate in/out tangents for all keys.
        void ComputeTangents(CurveTangent tangentInType, CurveTangent tangentOutType);

        /// Computes matching in/out tangents for a single key.
        void ComputeTangent(int keyIndex, CurveTangent tangentType);

        /// Computes separate in/out tangents for a single key.
        void ComputeTangent(int keyIndex, CurveTangent tangentInType, CurveTangent tangentOutType);

    private:
        explicit Curve(const CurveKeyCollection& keys);

        [[nodiscard]] int GetNumberOfCycle(float position) const;
        [[nodiscard]] float GetCurvePosition(float position) const;

        CurveKeyCollection keys;
        CurveLoopType postLoop;
        CurveLoopType preLoop;
    };
}
