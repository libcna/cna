// SPDX-License-Identifier: MS-PL

#pragma once

#include "Microsoft/Xna/Framework/CurveKeyCollection.hpp"
#include "Microsoft/Xna/Framework/CurveLoopType.hpp"
#include "Microsoft/Xna/Framework/CurveTangent.hpp"

namespace Microsoft::Xna::Framework
{
    /** @brief Contains a collection of CurveKey points in 2D space and provides methods for evaluating features of the curve they define. */
    class Curve
    {
    public:
        /**
         * @brief Gets whether this curve is constant.
         * @return true if the curve has zero or one keys; false otherwise.
         */
        [[nodiscard]] bool getIsConstantProperty() const;

        /**
         * @brief Gets the collection of curve keys.
         * @return A mutable reference to the key collection.
         */
        [[nodiscard]] CurveKeyCollection& getKeysProperty();

        /**
         * @brief Gets the collection of curve keys.
         * @return A const reference to the key collection.
         */
        [[nodiscard]] const CurveKeyCollection& getKeysProperty() const;

        /**
         * @brief Gets how values after the last control point are handled.
         * @return The post-loop behavior type.
         */
        [[nodiscard]] CurveLoopType getPostLoopProperty() const;

        /**
         * @brief Sets how values after the last control point are handled.
         * @param value The desired post-loop behavior.
         */
        void setPostLoopProperty(CurveLoopType value);

        /**
         * @brief Gets how values before the first control point are handled.
         * @return The pre-loop behavior type.
         */
        [[nodiscard]] CurveLoopType getPreLoopProperty() const;

        /**
         * @brief Sets how values before the first control point are handled.
         * @param value The desired pre-loop behavior.
         */
        void setPreLoopProperty(CurveLoopType value);

        /** @brief Constructs an empty curve. */
        Curve();

        /**
         * @brief Creates a copy of this curve.
         * @return A deep copy of this curve.
         */
        [[nodiscard]] Curve Clone() const;

        /**
         * @brief Evaluates the curve value at the given position.
         * @param position The position on the curve to evaluate.
         * @return The interpolated value at that position.
         */
        [[nodiscard]] float Evaluate(float position) const;

        /**
         * @brief Computes matching in/out tangents for all keys.
         * @param tangentType The tangent type to apply to both TangentIn and TangentOut.
         */
        void ComputeTangents(CurveTangent tangentType);

        /**
         * @brief Computes separate in/out tangents for all keys.
         * @param tangentInType The tangent type for TangentIn on each key.
         * @param tangentOutType The tangent type for TangentOut on each key.
         */
        void ComputeTangents(CurveTangent tangentInType, CurveTangent tangentOutType);

        /**
         * @brief Computes matching in/out tangents for a single key.
         * @param keyIndex The index of the key in the collection.
         * @param tangentType The tangent type to apply to both TangentIn and TangentOut.
         */
        void ComputeTangent(int keyIndex, CurveTangent tangentType);

        /**
         * @brief Computes separate in/out tangents for a single key.
         * @param keyIndex The index of the key in the collection.
         * @param tangentInType The tangent type for TangentIn.
         * @param tangentOutType The tangent type for TangentOut.
         */
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
