// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectAnnotationCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterType.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class EffectParameterCollection;
    class Texture;
    class Texture2D;
    class Texture3D;
    class TextureCube;

    /// Represents a parameter (uniform) declared in an effect shader.
    class EffectParameter
    {
    public:
        /// Constructs an EffectParameter with full metadata.
        EffectParameter(std::string name, std::string semantic,
                        int rowCount, int columnCount,
                        EffectParameterClass paramClass,
                        EffectParameterType paramType);

        /// Gets the name of this parameter.
        [[nodiscard]] const std::string& getNameProperty() const;
        /// Gets the semantic string of this parameter.
        [[nodiscard]] const std::string& getSemanticProperty() const;
        /// Gets the number of rows for matrix parameters.
        [[nodiscard]] int getRowCountProperty() const;
        /// Gets the number of columns for matrix parameters.
        [[nodiscard]] int getColumnCountProperty() const;
        /// Gets the parameter class.
        [[nodiscard]] EffectParameterClass getParameterClassProperty() const;
        /// Gets the parameter type.
        [[nodiscard]] EffectParameterType getParameterTypeProperty() const;
        /// Gets the collection of array elements for array parameters.
        [[nodiscard]] EffectParameterCollection& getElementsProperty();
        /// Gets the collection of struct members for struct parameters.
        [[nodiscard]] EffectParameterCollection& getStructureMembersProperty();
        /// Gets the collection of annotations attached to this parameter.
        [[nodiscard]] EffectAnnotationCollection& getAnnotationsProperty();

        // GetValue overloads
        /// Gets the value of this parameter as a boolean.
        [[nodiscard]] bool GetValueBoolean() const;
        /// Gets the value of this parameter as a boolean array.
        [[nodiscard]] std::vector<bool> GetValueBooleanArray(int count) const;
        /// Gets the value of this parameter as a 32-bit integer.
        [[nodiscard]] int GetValueInt32() const;
        /// Gets the value of this parameter as an integer array.
        [[nodiscard]] std::vector<int> GetValueInt32Array(int count) const;
        /// Gets the value of this parameter as a single-precision float.
        [[nodiscard]] float GetValueSingle() const;
        /// Gets the value of this parameter as a float array.
        [[nodiscard]] std::vector<float> GetValueSingleArray(int count) const;
        /// Gets the value of this parameter as a string.
        [[nodiscard]] std::string GetValueString() const;
        /// Gets the value of this parameter as a Matrix.
        [[nodiscard]] Matrix GetValueMatrix() const;
        /// Gets the value of this parameter as a Matrix array.
        [[nodiscard]] std::vector<Matrix> GetValueMatrixArray(int count) const;
        /// Gets the transposed value of this parameter as a Matrix.
        [[nodiscard]] Matrix GetValueMatrixTranspose() const;
        /// Gets the transposed value of this parameter as a Matrix array.
        [[nodiscard]] std::vector<Matrix> GetValueMatrixTransposeArray(int count) const;
        /// Gets the value of this parameter as a Quaternion.
        [[nodiscard]] Quaternion GetValueQuaternion() const;
        /// Gets the value of this parameter as a Quaternion array.
        [[nodiscard]] std::vector<Quaternion> GetValueQuaternionArray(int count) const;
        /// Gets the value of this parameter as a Vector2.
        [[nodiscard]] Vector2 GetValueVector2() const;
        /// Gets the value of this parameter as a Vector2 array.
        [[nodiscard]] std::vector<Vector2> GetValueVector2Array(int count) const;
        /// Gets the value of this parameter as a Vector3.
        [[nodiscard]] Vector3 GetValueVector3() const;
        /// Gets the value of this parameter as a Vector3 array.
        [[nodiscard]] std::vector<Vector3> GetValueVector3Array(int count) const;
        /// Gets the value of this parameter as a Vector4.
        [[nodiscard]] Vector4 GetValueVector4() const;
        /// Gets the value of this parameter as a Vector4 array.
        [[nodiscard]] std::vector<Vector4> GetValueVector4Array(int count) const;
        /// Gets the value of this parameter as a Texture2D pointer.
        [[nodiscard]] Texture2D* GetValueTexture2D() const;
        /// Gets the value of this parameter as a Texture3D pointer.
        [[nodiscard]] Texture3D* GetValueTexture3D() const;
        /// Gets the value of this parameter as a TextureCube pointer.
        [[nodiscard]] TextureCube* GetValueTextureCube() const;

        // SetValue overloads
        /// Sets the value of this parameter from a boolean.
        void SetValue(bool value);
        /// Sets the value of this parameter from a boolean array.
        void SetValue(const std::vector<bool>& value);
        /// Sets the value of this parameter from an integer.
        void SetValue(int value);
        /// Sets the value of this parameter from an integer array.
        void SetValue(const std::vector<int>& value);
        /// Sets the value of this parameter from a float.
        void SetValue(float value);
        /// Sets the value of this parameter from a float array.
        void SetValue(const std::vector<float>& value);
        /// Sets the value of this parameter from a string.
        void SetValue(const std::string& value);
        /// Sets the value of this parameter from a Matrix.
        void SetValue(const Matrix& value);
        /// Sets the value of this parameter from a Matrix array.
        void SetValue(const std::vector<Matrix>& value);
        /// Sets the transposed value of this parameter from a Matrix.
        void SetValueTranspose(const Matrix& value);
        /// Sets the transposed value of this parameter from a Matrix array.
        void SetValueTranspose(const std::vector<Matrix>& value);
        /// Sets the value of this parameter from a Quaternion.
        void SetValue(const Quaternion& value);
        /// Sets the value of this parameter from a Quaternion array.
        void SetValue(const std::vector<Quaternion>& value);
        /// Sets the value of this parameter from a Vector2.
        void SetValue(const Vector2& value);
        /// Sets the value of this parameter from a Vector2 array.
        void SetValue(const std::vector<Vector2>& value);
        /// Sets the value of this parameter from a Vector3.
        void SetValue(const Vector3& value);
        /// Sets the value of this parameter from a Vector3 array.
        void SetValue(const std::vector<Vector3>& value);
        /// Sets the value of this parameter from a Vector4.
        void SetValue(const Vector4& value);
        /// Sets the value of this parameter from a Vector4 array.
        void SetValue(const std::vector<Vector4>& value);
        /// Sets the value of this parameter from a Texture pointer.
        void SetValue(Texture* value);
        /// Sets the value of this parameter from a Texture2D pointer.
        void SetValue(Texture2D* value);
        /// Sets the value of this parameter from a Texture3D pointer.
        void SetValue(Texture3D* value);
        /// Sets the value of this parameter from a TextureCube pointer.
        void SetValue(TextureCube* value);

    private:
        std::string name_;
        std::string semantic_;
        int rowCount_;
        int columnCount_;
        EffectParameterClass paramClass_;
        EffectParameterType paramType_;

        // Storage: raw float buffer for numeric types, string for string, pointer for textures.
        // Texture2D/3D/TextureCube do NOT inherit from Texture in CNA, so each has its own slot.
        std::vector<float> floatData_;
        std::vector<int>   intData_;
        std::string        stringData_;
        Texture*           textureData_     = nullptr;
        Texture2D*         texture2DData_   = nullptr;
        Texture3D*         texture3DData_   = nullptr;
        TextureCube*       textureCubeData_ = nullptr;

        std::unique_ptr<EffectParameterCollection> elements_;
        std::unique_ptr<EffectParameterCollection> members_;
        EffectAnnotationCollection annotations_;
    };
}
