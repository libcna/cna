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

    class EffectParameter
    {
    public:
        EffectParameter(std::string name, std::string semantic,
                        int rowCount, int columnCount,
                        EffectParameterClass paramClass,
                        EffectParameterType paramType);

        [[nodiscard]] const std::string& getNameProperty() const;
        [[nodiscard]] const std::string& getSemanticProperty() const;
        [[nodiscard]] int getRowCountProperty() const;
        [[nodiscard]] int getColumnCountProperty() const;
        [[nodiscard]] EffectParameterClass getParameterClassProperty() const;
        [[nodiscard]] EffectParameterType getParameterTypeProperty() const;
        [[nodiscard]] EffectParameterCollection& getElementsProperty();
        [[nodiscard]] EffectParameterCollection& getStructureMembersProperty();
        [[nodiscard]] EffectAnnotationCollection& getAnnotationsProperty();

        // GetValue overloads
        [[nodiscard]] bool GetValueBoolean() const;
        [[nodiscard]] std::vector<bool> GetValueBooleanArray(int count) const;
        [[nodiscard]] int GetValueInt32() const;
        [[nodiscard]] std::vector<int> GetValueInt32Array(int count) const;
        [[nodiscard]] float GetValueSingle() const;
        [[nodiscard]] std::vector<float> GetValueSingleArray(int count) const;
        [[nodiscard]] std::string GetValueString() const;
        [[nodiscard]] Matrix GetValueMatrix() const;
        [[nodiscard]] std::vector<Matrix> GetValueMatrixArray(int count) const;
        [[nodiscard]] Matrix GetValueMatrixTranspose() const;
        [[nodiscard]] std::vector<Matrix> GetValueMatrixTransposeArray(int count) const;
        [[nodiscard]] Quaternion GetValueQuaternion() const;
        [[nodiscard]] std::vector<Quaternion> GetValueQuaternionArray(int count) const;
        [[nodiscard]] Vector2 GetValueVector2() const;
        [[nodiscard]] std::vector<Vector2> GetValueVector2Array(int count) const;
        [[nodiscard]] Vector3 GetValueVector3() const;
        [[nodiscard]] std::vector<Vector3> GetValueVector3Array(int count) const;
        [[nodiscard]] Vector4 GetValueVector4() const;
        [[nodiscard]] std::vector<Vector4> GetValueVector4Array(int count) const;
        [[nodiscard]] Texture2D* GetValueTexture2D() const;
        [[nodiscard]] Texture3D* GetValueTexture3D() const;
        [[nodiscard]] TextureCube* GetValueTextureCube() const;

        // SetValue overloads
        void SetValue(bool value);
        void SetValue(const std::vector<bool>& value);
        void SetValue(int value);
        void SetValue(const std::vector<int>& value);
        void SetValue(float value);
        void SetValue(const std::vector<float>& value);
        void SetValue(const std::string& value);
        void SetValue(const Matrix& value);
        void SetValue(const std::vector<Matrix>& value);
        void SetValueTranspose(const Matrix& value);
        void SetValueTranspose(const std::vector<Matrix>& value);
        void SetValue(const Quaternion& value);
        void SetValue(const std::vector<Quaternion>& value);
        void SetValue(const Vector2& value);
        void SetValue(const std::vector<Vector2>& value);
        void SetValue(const Vector3& value);
        void SetValue(const std::vector<Vector3>& value);
        void SetValue(const Vector4& value);
        void SetValue(const std::vector<Vector4>& value);
        void SetValue(Texture* value);
        void SetValue(Texture2D* value);
        void SetValue(Texture3D* value);
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
