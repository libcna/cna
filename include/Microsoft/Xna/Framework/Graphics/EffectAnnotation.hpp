// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterType.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief Represents an annotation attached to an effect technique, pass, or parameter.
     *
     * Annotations carry metadata values (booleans, integers, floats, strings, vectors, matrices)
     * that tools and the application can inspect without affecting rendering state.
     */
    class EffectAnnotation
    {
    public:
        /**
         * @brief Constructs an EffectAnnotation with all metadata, optional float data, and
         *        optional cached string value.
         *
         * @param name         Name of the annotation.
         * @param semantic     Semantic string of the annotation.
         * @param rowCount     Number of rows (for matrix annotations).
         * @param columnCount  Number of columns (for matrix annotations).
         * @param paramClass   Classification of the annotation value.
         * @param paramType    Data type of the annotation value.
         * @param data         Raw float data backing this annotation (default: empty).
         * @param cachedString Cached string value returned by GetValueString() (default: empty).
         */
        EffectAnnotation(std::string name, std::string semantic,
                         int rowCount, int columnCount,
                         EffectParameterClass paramClass,
                         EffectParameterType paramType,
                         std::vector<float> data = {},
                         std::string cachedString = "");

        /**
         * @brief Gets the name of this annotation.
         *
         * @return The annotation name string.
         */
        [[nodiscard]] const std::string& getNameProperty() const;

        /**
         * @brief Gets the semantic string of this annotation.
         *
         * @return The semantic string.
         */
        [[nodiscard]] const std::string& getSemanticProperty() const;

        /**
         * @brief Gets the number of rows in a matrix annotation.
         *
         * @return Row count.
         */
        [[nodiscard]] int getRowCountProperty() const;

        /**
         * @brief Gets the number of columns in a matrix annotation.
         *
         * @return Column count.
         */
        [[nodiscard]] int getColumnCountProperty() const;

        /**
         * @brief Gets the parameter class of this annotation.
         *
         * @return The EffectParameterClass value.
         */
        [[nodiscard]] EffectParameterClass getParameterClassProperty() const;

        /**
         * @brief Gets the parameter type of this annotation.
         *
         * @return The EffectParameterType value.
         */
        [[nodiscard]] EffectParameterType getParameterTypeProperty() const;

        /**
         * @brief Gets the value of this annotation as a boolean.
         *
         * @return The boolean value.
         */
        [[nodiscard]] bool GetValueBoolean() const;

        /**
         * @brief Gets the value of this annotation as a 32-bit integer.
         *
         * @return The integer value.
         */
        [[nodiscard]] int GetValueInt32() const;

        /**
         * @brief Gets the value of this annotation as a single-precision float.
         *
         * @return The float value.
         */
        [[nodiscard]] float GetValueSingle() const;

        /**
         * @brief Gets the value of this annotation as a string.
         *
         * @return The cached string value.
         */
        [[nodiscard]] std::string GetValueString() const;

        /**
         * @brief Gets the value of this annotation as a Vector2.
         *
         * @return The Vector2 value.
         */
        [[nodiscard]] Vector2 GetValueVector2() const;

        /**
         * @brief Gets the value of this annotation as a Vector3.
         *
         * @return The Vector3 value.
         */
        [[nodiscard]] Vector3 GetValueVector3() const;

        /**
         * @brief Gets the value of this annotation as a Vector4.
         *
         * @return The Vector4 value.
         */
        [[nodiscard]] Vector4 GetValueVector4() const;

        /**
         * @brief Gets the value of this annotation as a Matrix.
         *
         * @return The Matrix value.
         */
        [[nodiscard]] Matrix GetValueMatrix() const;

    private:
        std::string name_;
        std::string semantic_;
        int rowCount_;
        int columnCount_;
        EffectParameterClass paramClass_;
        EffectParameterType paramType_;
        std::vector<float> data_;
        std::string cachedString_;
    };
}
