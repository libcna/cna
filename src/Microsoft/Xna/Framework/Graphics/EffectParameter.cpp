// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    EffectParameter::EffectParameter(std::string name, std::string semantic,
                                     int rowCount, int columnCount,
                                     EffectParameterClass paramClass,
                                     EffectParameterType paramType)
        : name_(std::move(name)), semantic_(std::move(semantic))
        , rowCount_(rowCount), columnCount_(columnCount)
        , paramClass_(paramClass), paramType_(paramType)
        , elements_(std::make_unique<EffectParameterCollection>())
        , members_(std::make_unique<EffectParameterCollection>())
    {
    }

    const std::string& EffectParameter::getNameProperty() const { return name_; }
    const std::string& EffectParameter::getSemanticProperty() const { return semantic_; }
    int EffectParameter::getRowCountProperty() const { return rowCount_; }
    int EffectParameter::getColumnCountProperty() const { return columnCount_; }
    EffectParameterClass EffectParameter::getParameterClassProperty() const { return paramClass_; }
    EffectParameterType EffectParameter::getParameterTypeProperty() const { return paramType_; }
    EffectParameterCollection& EffectParameter::getElementsProperty() { return *elements_; }
    EffectParameterCollection& EffectParameter::getStructureMembersProperty() { return *members_; }
    EffectAnnotationCollection& EffectParameter::getAnnotationsProperty() { return annotations_; }

    // --- GetValue ---
    bool EffectParameter::GetValueBoolean() const
    {
        return !intData_.empty() && intData_[0] != 0;
    }
    std::vector<bool> EffectParameter::GetValueBooleanArray(int count) const
    {
        std::vector<bool> r;
        for (int i = 0; i < count && i < (int)intData_.size(); ++i)
            r.push_back(intData_[i] != 0);
        return r;
    }
    int EffectParameter::GetValueInt32() const
    {
        return intData_.empty() ? 0 : intData_[0];
    }
    std::vector<int> EffectParameter::GetValueInt32Array(int count) const
    {
        std::vector<int> r(intData_.begin(), intData_.begin() + std::min(count, (int)intData_.size()));
        return r;
    }
    float EffectParameter::GetValueSingle() const
    {
        return floatData_.empty() ? 0.0f : floatData_[0];
    }
    std::vector<float> EffectParameter::GetValueSingleArray(int count) const
    {
        std::vector<float> r(floatData_.begin(), floatData_.begin() + std::min(count, (int)floatData_.size()));
        return r;
    }
    std::string EffectParameter::GetValueString() const { return stringData_; }

    Matrix EffectParameter::GetValueMatrix() const
    {
        if (floatData_.size() < 16) return Matrix::getIdentityProperty();
        return Matrix(floatData_[0], floatData_[1], floatData_[2], floatData_[3],
                      floatData_[4], floatData_[5], floatData_[6], floatData_[7],
                      floatData_[8], floatData_[9], floatData_[10], floatData_[11],
                      floatData_[12], floatData_[13], floatData_[14], floatData_[15]);
    }
    std::vector<Matrix> EffectParameter::GetValueMatrixArray(int count) const
    {
        std::vector<Matrix> r;
        for (int i = 0; i < count && (i + 1) * 16 <= (int)floatData_.size(); ++i)
        {
            const float* d = floatData_.data() + i * 16;
            r.push_back(Matrix(d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7],
                               d[8], d[9], d[10], d[11], d[12], d[13], d[14], d[15]));
        }
        return r;
    }
    Matrix EffectParameter::GetValueMatrixTranspose() const
    {
        return Matrix::Transpose(GetValueMatrix());
    }
    std::vector<Matrix> EffectParameter::GetValueMatrixTransposeArray(int count) const
    {
        auto arr = GetValueMatrixArray(count);
        for (auto& m : arr) m = Matrix::Transpose(m);
        return arr;
    }
    Quaternion EffectParameter::GetValueQuaternion() const
    {
        if (floatData_.size() < 4) return {0.0f, 0.0f, 0.0f, 1.0f};
        return {floatData_[0], floatData_[1], floatData_[2], floatData_[3]};
    }
    std::vector<Quaternion> EffectParameter::GetValueQuaternionArray(int count) const
    {
        std::vector<Quaternion> r;
        for (int i = 0; i < count && (i + 1) * 4 <= (int)floatData_.size(); ++i)
        {
            const float* d = floatData_.data() + i * 4;
            r.push_back({d[0], d[1], d[2], d[3]});
        }
        return r;
    }
    Vector2 EffectParameter::GetValueVector2() const
    {
        if (floatData_.size() < 2) return {};
        return {floatData_[0], floatData_[1]};
    }
    std::vector<Vector2> EffectParameter::GetValueVector2Array(int count) const
    {
        std::vector<Vector2> r;
        for (int i = 0; i < count && (i + 1) * 2 <= (int)floatData_.size(); ++i)
            r.push_back({floatData_[i * 2], floatData_[i * 2 + 1]});
        return r;
    }
    Vector3 EffectParameter::GetValueVector3() const
    {
        if (floatData_.size() < 3) return {};
        return {floatData_[0], floatData_[1], floatData_[2]};
    }
    std::vector<Vector3> EffectParameter::GetValueVector3Array(int count) const
    {
        std::vector<Vector3> r;
        for (int i = 0; i < count && (i + 1) * 3 <= (int)floatData_.size(); ++i)
            r.push_back({floatData_[i * 3], floatData_[i * 3 + 1], floatData_[i * 3 + 2]});
        return r;
    }
    Vector4 EffectParameter::GetValueVector4() const
    {
        if (floatData_.size() < 4) return {0.0f, 0.0f, 0.0f, 1.0f};
        return {floatData_[0], floatData_[1], floatData_[2], floatData_[3]};
    }
    std::vector<Vector4> EffectParameter::GetValueVector4Array(int count) const
    {
        std::vector<Vector4> r;
        for (int i = 0; i < count && (i + 1) * 4 <= (int)floatData_.size(); ++i)
            r.push_back({floatData_[i * 4], floatData_[i * 4 + 1], floatData_[i * 4 + 2], floatData_[i * 4 + 3]});
        return r;
    }
    Texture2D*   EffectParameter::GetValueTexture2D()   const { return texture2DData_; }
    Texture3D*   EffectParameter::GetValueTexture3D()   const { return texture3DData_; }
    TextureCube* EffectParameter::GetValueTextureCube() const { return textureCubeData_; }

    // --- SetValue ---
    void EffectParameter::SetValue(bool value)      { intData_ = {value ? 1 : 0}; }
    void EffectParameter::SetValue(const std::vector<bool>& v)
    {
        intData_.clear();
        for (bool b : v) intData_.push_back(b ? 1 : 0);
    }
    void EffectParameter::SetValue(int value)       { intData_ = {value}; }
    void EffectParameter::SetValue(const std::vector<int>& v) { intData_ = v; }
    void EffectParameter::SetValue(float value)     { floatData_ = {value}; }
    void EffectParameter::SetValue(const std::vector<float>& v) { floatData_ = v; }
    void EffectParameter::SetValue(const std::string& v) { stringData_ = v; }
    void EffectParameter::SetValue(Texture* v)      { textureData_     = v; }
    void EffectParameter::SetValue(Texture2D* v)    { texture2DData_   = v; }
    void EffectParameter::SetValue(Texture3D* v)    { texture3DData_   = v; }
    void EffectParameter::SetValue(TextureCube* v)  { textureCubeData_ = v; }

    void EffectParameter::SetValue(const Matrix& m)
    {
        floatData_ = {
            m.M11, m.M12, m.M13, m.M14,
            m.M21, m.M22, m.M23, m.M24,
            m.M31, m.M32, m.M33, m.M34,
            m.M41, m.M42, m.M43, m.M44
        };
    }
    void EffectParameter::SetValue(const std::vector<Matrix>& v)
    {
        floatData_.clear();
        for (const auto& m : v)
        {
            floatData_.insert(floatData_.end(),
                {m.M11, m.M12, m.M13, m.M14,
                 m.M21, m.M22, m.M23, m.M24,
                 m.M31, m.M32, m.M33, m.M34,
                 m.M41, m.M42, m.M43, m.M44});
        }
    }
    void EffectParameter::SetValueTranspose(const Matrix& m) { SetValue(Matrix::Transpose(m)); }
    void EffectParameter::SetValueTranspose(const std::vector<Matrix>& v)
    {
        std::vector<Matrix> t;
        t.reserve(v.size());
        for (const auto& m : v) t.push_back(Matrix::Transpose(m));
        SetValue(t);
    }
    void EffectParameter::SetValue(const Quaternion& q) { floatData_ = {q.X, q.Y, q.Z, q.W}; }
    void EffectParameter::SetValue(const std::vector<Quaternion>& v)
    {
        floatData_.clear();
        for (const auto& q : v) { floatData_.push_back(q.X); floatData_.push_back(q.Y); floatData_.push_back(q.Z); floatData_.push_back(q.W); }
    }
    void EffectParameter::SetValue(const Vector2& v) { floatData_ = {v.X, v.Y}; }
    void EffectParameter::SetValue(const std::vector<Vector2>& v)
    {
        floatData_.clear();
        for (const auto& e : v) { floatData_.push_back(e.X); floatData_.push_back(e.Y); }
    }
    void EffectParameter::SetValue(const Vector3& v) { floatData_ = {v.X, v.Y, v.Z}; }
    void EffectParameter::SetValue(const std::vector<Vector3>& v)
    {
        floatData_.clear();
        for (const auto& e : v) { floatData_.push_back(e.X); floatData_.push_back(e.Y); floatData_.push_back(e.Z); }
    }
    void EffectParameter::SetValue(const Vector4& v) { floatData_ = {v.X, v.Y, v.Z, v.W}; }
    void EffectParameter::SetValue(const std::vector<Vector4>& v)
    {
        floatData_.clear();
        for (const auto& e : v) { floatData_.push_back(e.X); floatData_.push_back(e.Y); floatData_.push_back(e.Z); floatData_.push_back(e.W); }
    }
}
