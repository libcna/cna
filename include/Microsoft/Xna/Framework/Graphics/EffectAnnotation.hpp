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
    /// Represents an annotation attached to an effect technique, pass, or parameter.
    class EffectAnnotation
    {
    public:
        /// Constructs an EffectAnnotation with all metadata and optional float data.
        EffectAnnotation(std::string name, std::string semantic,
                         int rowCount, int columnCount,
                         EffectParameterClass paramClass,
                         EffectParameterType paramType,
                         std::vector<float> data = {});

        /// Gets the name of this annotation.
        [[nodiscard]] const std::string& getNameProperty() const;
        /// Gets the semantic string of this annotation.
        [[nodiscard]] const std::string& getSemanticProperty() const;
        /// Gets the number of rows in a matrix annotation.
        [[nodiscard]] int getRowCountProperty() const;
        /// Gets the number of columns in a matrix annotation.
        [[nodiscard]] int getColumnCountProperty() const;
        /// Gets the parameter class of this annotation.
        [[nodiscard]] EffectParameterClass getParameterClassProperty() const;
        /// Gets the parameter type of this annotation.
        [[nodiscard]] EffectParameterType getParameterTypeProperty() const;

        /// Gets the value of this annotation as a boolean.
        [[nodiscard]] bool GetValueBoolean() const;
        /// Gets the value of this annotation as a 32-bit integer.
        [[nodiscard]] int GetValueInt32() const;
        /// Gets the value of this annotation as a single-precision float.
        [[nodiscard]] float GetValueSingle() const;
        /// Gets the value of this annotation as a string.
        [[nodiscard]] std::string GetValueString() const;
        /// Gets the value of this annotation as a Vector2.
        [[nodiscard]] Vector2 GetValueVector2() const;
        /// Gets the value of this annotation as a Vector3.
        [[nodiscard]] Vector3 GetValueVector3() const;
        /// Gets the value of this annotation as a Vector4.
        [[nodiscard]] Vector4 GetValueVector4() const;
        /// Gets the value of this annotation as a Matrix.
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
