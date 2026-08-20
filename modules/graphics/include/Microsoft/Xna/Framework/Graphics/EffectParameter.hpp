// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <cstddef>
#include <cstdint>
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
    class Effect;
    class Texture;
    class Texture2D;
    class Texture3D;
    class TextureCube;

    /**
     * @brief Represents a parameter (uniform variable) declared in an effect shader.
     */
    class EffectParameter
    {
    public:
        /**
         * @brief Constructs an EffectParameter with full type metadata.
         *
         * @param name        Name of the parameter as declared in the shader.
         * @param semantic    Semantic string attached to the parameter.
         * @param rowCount    Number of rows (matrix parameters).
         * @param columnCount Number of columns (matrix parameters).
         * @param paramClass  Classification of the parameter (scalar, vector, matrix, etc.).
         * @param paramType   Data type of the parameter.
         */
        EffectParameter(std::string name, std::string semantic,
                        int rowCount, int columnCount,
                        EffectParameterClass paramClass,
                        EffectParameterType paramType);

        /**
         * @brief Gets the name of this parameter.
         *
         * @return The parameter name string.
         */
        [[nodiscard]] const std::string& getNameProperty() const;

        /**
         * @brief Gets the semantic string of this parameter.
         *
         * @return The semantic string.
         */
        [[nodiscard]] const std::string& getSemanticProperty() const;

        /**
         * @brief Gets the number of rows for matrix parameters.
         *
         * @return Row count.
         */
        [[nodiscard]] int getRowCountProperty() const;

        /**
         * @brief Gets the number of columns for matrix parameters.
         *
         * @return Column count.
         */
        [[nodiscard]] int getColumnCountProperty() const;

        /**
         * @brief Gets the parameter class (scalar, vector, matrix, object, or struct).
         *
         * @return The EffectParameterClass value.
         */
        [[nodiscard]] EffectParameterClass getParameterClassProperty() const;

        /**
         * @brief Gets the parameter type (bool, int, float, texture, etc.).
         *
         * @return The EffectParameterType value.
         */
        [[nodiscard]] EffectParameterType getParameterTypeProperty() const;

        /**
         * @brief Gets the collection of array elements for array parameters.
         *
         * @return Reference to the elements collection.
         */
        [[nodiscard]] EffectParameterCollection& getElementsProperty();
        /** @brief Const overload of getElementsProperty(). */
        [[nodiscard]] const EffectParameterCollection& getElementsProperty() const;

        /**
         * @brief Gets the collection of struct members for struct parameters.
         *
         * @return Reference to the structure members collection.
         */
        [[nodiscard]] EffectParameterCollection& getStructureMembersProperty();
        /** @brief Const overload of getStructureMembersProperty(). */
        [[nodiscard]] const EffectParameterCollection& getStructureMembersProperty() const;

        /**
         * @brief Gets the collection of annotations attached to this parameter.
         *
         * @return Reference to the annotation collection.
         */
        [[nodiscard]] EffectAnnotationCollection& getAnnotationsProperty();
        /** @brief Const overload of getAnnotationsProperty(). */
        [[nodiscard]] const EffectAnnotationCollection& getAnnotationsProperty() const;

        // GetValue overloads

        /**
         * @brief Gets the value of this parameter as a boolean.
         *
         * @return The boolean value stored in this parameter.
         */
        [[nodiscard]] bool GetValueBoolean() const;

        /**
         * @brief Gets the value of this parameter as a boolean array.
         *
         * @param count Number of elements to retrieve.
         * @return Vector of boolean values.
         */
        [[nodiscard]] std::vector<bool> GetValueBooleanArray(int count) const;

        /**
         * @brief Gets the value of this parameter as a 32-bit integer.
         *
         * @return The integer value stored in this parameter.
         */
        [[nodiscard]] int GetValueInt32() const;

        /**
         * @brief Gets the value of this parameter as an integer array.
         *
         * @param count Number of elements to retrieve.
         * @return Vector of integer values.
         */
        [[nodiscard]] std::vector<int> GetValueInt32Array(int count) const;

        /**
         * @brief Gets the value of this parameter as a single-precision float.
         *
         * @return The float value stored in this parameter.
         */
        [[nodiscard]] float GetValueSingle() const;

        /**
         * @brief Gets the value of this parameter as a float array.
         *
         * @param count Number of elements to retrieve.
         * @return Vector of float values.
         */
        [[nodiscard]] std::vector<float> GetValueSingleArray(int count) const;

        /**
         * @brief Gets the value of this parameter as a string.
         *
         * @return The cached string value.
         * @throws System::InvalidCastException If this is a compiled effect's parameter whose
         *         reflected EffectParameterType is not String, matching XNA 4.0.
         */
        [[nodiscard]] std::string GetValueString() const;

        /**
         * @brief Gets the value as a Matrix by unpacking column-major effect storage.
         *
         * @return The Matrix value stored in this parameter.
         */
        [[nodiscard]] Matrix GetValueMatrix() const;

        /**
         * @brief Gets Matrix values by unpacking each column-major effect-storage element.
         *
         * @param count Number of matrices to retrieve.
         * @return Vector of Matrix values.
         */
        [[nodiscard]] std::vector<Matrix> GetValueMatrixArray(int count) const;

        /**
         * @brief Gets the transposed value by reading effect storage in row-major order.
         *
         * @return The transposed Matrix value stored in this parameter.
         */
        [[nodiscard]] Matrix GetValueMatrixTranspose() const;

        /**
         * @brief Gets transposed values by reading each effect-storage element in row-major order.
         *
         * @param count Number of matrices to retrieve.
         * @return Vector of transposed Matrix values.
         */
        [[nodiscard]] std::vector<Matrix> GetValueMatrixTransposeArray(int count) const;

        /**
         * @brief Gets the value of this parameter as a Quaternion.
         *
         * @return The Quaternion value stored in this parameter.
         */
        [[nodiscard]] Quaternion GetValueQuaternion() const;

        /**
         * @brief Gets the value of this parameter as a Quaternion array.
         *
         * @param count Number of quaternions to retrieve.
         * @return Vector of Quaternion values.
         */
        [[nodiscard]] std::vector<Quaternion> GetValueQuaternionArray(int count) const;

        /**
         * @brief Gets the value of this parameter as a Vector2.
         *
         * @return The Vector2 value stored in this parameter.
         */
        [[nodiscard]] Vector2 GetValueVector2() const;

        /**
         * @brief Gets the value of this parameter as a Vector2 array.
         *
         * @param count Number of elements to retrieve.
         * @return Vector of Vector2 values.
         */
        [[nodiscard]] std::vector<Vector2> GetValueVector2Array(int count) const;

        /**
         * @brief Gets the value of this parameter as a Vector3.
         *
         * @return The Vector3 value stored in this parameter.
         */
        [[nodiscard]] Vector3 GetValueVector3() const;

        /**
         * @brief Gets the value of this parameter as a Vector3 array.
         *
         * @param count Number of elements to retrieve.
         * @return Vector of Vector3 values.
         */
        [[nodiscard]] std::vector<Vector3> GetValueVector3Array(int count) const;

        /**
         * @brief Gets the value of this parameter as a Vector4.
         *
         * @return The Vector4 value stored in this parameter.
         */
        [[nodiscard]] Vector4 GetValueVector4() const;

        /**
         * @brief Gets the value of this parameter as a Vector4 array.
         *
         * @param count Number of elements to retrieve.
         * @return Vector of Vector4 values.
         */
        [[nodiscard]] std::vector<Vector4> GetValueVector4Array(int count) const;

        /**
         * @brief Gets the value of this parameter as a Texture2D pointer.
         *
         * @return Pointer to the Texture2D, or nullptr if none is set.
         */
        [[nodiscard]] Texture2D* GetValueTexture2D() const;

        /**
         * @brief Gets the value of this parameter as a Texture3D pointer.
         *
         * @return Pointer to the Texture3D, or nullptr if none is set.
         */
        [[nodiscard]] Texture3D* GetValueTexture3D() const;

        /**
         * @brief Gets the value of this parameter as a TextureCube pointer.
         *
         * @return Pointer to the TextureCube, or nullptr if none is set.
         */
        [[nodiscard]] TextureCube* GetValueTextureCube() const;

        // SetValue overloads

        /**
         * @brief Sets the value of this parameter from a boolean.
         *
         * @param value The boolean value to store.
         */
        void SetValue(bool value);

        /**
         * @brief Sets the value of this parameter from a boolean array.
         *
         * @param value The array of boolean values to store.
         */
        void SetValue(const std::vector<bool>& value);

        /**
         * @brief Sets the value of this parameter from a 32-bit integer.
         *
         * @param value The integer value to store.
         */
        void SetValue(int value);

        /**
         * @brief Sets the value of this parameter from an integer array.
         *
         * @param value The array of integer values to store.
         */
        void SetValue(const std::vector<int>& value);

        /**
         * @brief Sets the value of this parameter from a single-precision float.
         *
         * @param value The float value to store.
         */
        void SetValue(float value);

        /**
         * @brief Sets the value of this parameter from a float array.
         *
         * @param value The array of float values to store.
         */
        void SetValue(const std::vector<float>& value);

        /**
         * @brief Sets the value of this parameter from a string.
         *
         * @param value The string value to store.
         * @throws System::InvalidCastException If this is a compiled effect's parameter whose
         *         reflected EffectParameterType is not String, matching XNA 4.0.
         */
        void SetValue(const std::string& value);

        /**
         * @brief Sets the value by packing the Matrix into column-major effect storage.
         *
         * @param value The Matrix to store.
         */
        void SetValue(const Matrix& value);

        /**
         * @brief Sets values by packing each Matrix into column-major effect storage.
         *
         * @param value The array of matrices to store.
         */
        void SetValue(const std::vector<Matrix>& value);

        /**
         * @brief Sets the transposed value using row-major effect storage.
         *
         * @param value The Matrix to transpose and store.
         */
        void SetValueTranspose(const Matrix& value);

        /**
         * @brief Sets transposed values using row-major effect storage for each matrix.
         *
         * @param value The array of matrices to transpose and store.
         */
        void SetValueTranspose(const std::vector<Matrix>& value);

        /**
         * @brief Sets the value of this parameter from a Quaternion.
         *
         * @param value The Quaternion to store.
         */
        void SetValue(const Quaternion& value);

        /**
         * @brief Sets the value of this parameter from a Quaternion array.
         *
         * @param value The array of Quaternion values to store.
         */
        void SetValue(const std::vector<Quaternion>& value);

        /**
         * @brief Sets the value of this parameter from a Vector2.
         *
         * @param value The Vector2 to store.
         */
        void SetValue(const Vector2& value);

        /**
         * @brief Sets the value of this parameter from a Vector2 array.
         *
         * @param value The array of Vector2 values to store.
         */
        void SetValue(const std::vector<Vector2>& value);

        /**
         * @brief Sets the value of this parameter from a Vector3.
         *
         * @param value The Vector3 to store.
         */
        void SetValue(const Vector3& value);

        /**
         * @brief Sets the value of this parameter from a Vector3 array.
         *
         * @param value The array of Vector3 values to store.
         */
        void SetValue(const std::vector<Vector3>& value);

        /**
         * @brief Sets the value of this parameter from a Vector4.
         *
         * @param value The Vector4 to store.
         */
        void SetValue(const Vector4& value);

        /**
         * @brief Sets the value of this parameter from a Vector4 array.
         *
         * @param value The array of Vector4 values to store.
         */
        void SetValue(const std::vector<Vector4>& value);

        /**
         * @brief Sets the value of this parameter from a base Texture pointer.
         *
         * @param value Pointer to the texture to bind.
         */
        void SetValue(Texture* value);

        /**
         * @brief Sets the value of this parameter from a Texture2D pointer.
         *
         * @param value Pointer to the Texture2D to bind.
         */
        void SetValue(Texture2D* value);

        /**
         * @brief Sets the value of this parameter from a Texture3D pointer.
         *
         * @param value Pointer to the Texture3D to bind.
         */
        void SetValue(Texture3D* value);

        /**
         * @brief Sets the value of this parameter from a TextureCube pointer.
         *
         * @param value Pointer to the TextureCube to bind.
         */
        void SetValue(TextureCube* value);

    private:
        struct CompiledStorage
        {
            std::vector<std::uint8_t> bytes;
            std::string stringValue;
            Texture* texture = nullptr;
            std::uint32_t runtimeIndex = 0;
            bool dirty = false;
        };

        EffectParameter(std::string name, std::string semantic,
                        int rowCount, int columnCount, int elementCount,
                        EffectParameterClass paramClass, EffectParameterType paramType,
                        std::shared_ptr<CompiledStorage> storage,
                        std::size_t byteOffset, std::size_t byteSize);

        void RequireStringParameter(const char* operation) const;
        void RequireNumericParameter(const char* operation) const;

        [[nodiscard]] bool IsCompiledInternal() const noexcept;
        [[nodiscard]] const void* GetRawValueInternal() const noexcept;
        [[nodiscard]] std::size_t GetRawValueSizeInternal() const noexcept;
        [[nodiscard]] Texture* GetTextureInternal() const noexcept;
        [[nodiscard]] std::uint32_t GetRuntimeIndexInternal() const noexcept;
        [[nodiscard]] bool IsDirtyInternal() const noexcept;
        void MarkCleanInternal() noexcept;
        void CopyMutableValueFromInternal(const EffectParameter& source);

        std::string name_;
        std::string semantic_;
        int rowCount_;
        int columnCount_;
        EffectParameterClass paramClass_;
        EffectParameterType paramType_;
        int elementCount_ = 0;

        // Storage: raw float buffer for numeric types, string for string, pointer for textures.
        // Texture2D/Texture3D/TextureCube each get their own slot even though all three now
        // inherit Texture (matching FNA) -- collapsing these into one Texture* slot is a
        // separate, larger EffectParameter API-shape change, out of scope here (plans/plan_graphics.md
        // Task 863).
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

        // Compiled parameters and all of their element/member views share one bounded byte store.
        // Standalone/stock parameters leave this null and retain the historical vector storage.
        std::shared_ptr<CompiledStorage> compiledStorage_;
        std::size_t compiledByteOffset_ = 0;
        std::size_t compiledByteSize_ = 0;

        friend class Effect;
    };
}
