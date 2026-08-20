// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/InvalidCastException.hpp"

#include <algorithm>
#include <cstring>
#include <type_traits>

namespace
{
    template<typename T>
    T ReadCell(const std::vector<std::uint8_t>& bytes, std::size_t offset, T fallback = T{})
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (offset > bytes.size() || sizeof(T) > bytes.size() - offset)
        {
            return fallback;
        }
        T value{};
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
        return value;
    }

    template<typename T>
    bool WriteCell(std::vector<std::uint8_t>& bytes, std::size_t offset, const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (offset > bytes.size() || sizeof(T) > bytes.size() - offset)
        {
            return false;
        }
        std::memcpy(bytes.data() + offset, &value, sizeof(T));
        return true;
    }
}

namespace Microsoft::Xna::Framework::Graphics
{
    namespace
    {
        Matrix ReadCompiledMatrix(const std::vector<std::uint8_t>& bytes, std::size_t offset,
                                  std::size_t size, int rows, int columns, bool transpose)
        {
            Matrix result = Matrix::getIdentityProperty();
            float* destination[4] = {
                &result.M11, &result.M21, &result.M31, &result.M41
            };
            const int registerCount = std::clamp(rows, 0, 4);
            const int componentCount = std::clamp(columns, 0, 4);
            for (int reg = 0; reg < registerCount; ++reg)
            {
                for (int component = 0; component < componentCount; ++component)
                {
                    const std::size_t cell = static_cast<std::size_t>(reg * 4 + component) * 4;
                    if (cell + sizeof(float) > size) continue;
                    const float value = ReadCell<float>(bytes, offset + cell);
                    if (transpose)
                        destination[reg][component] = value;
                    else
                        destination[component][reg] = value;
                }
            }
            return result;
        }

        void WriteCompiledMatrix(std::vector<std::uint8_t>& bytes, std::size_t offset,
                                 std::size_t size, int rows, int columns, const Matrix& value,
                                 bool transpose)
        {
            const float* source[4] = { &value.M11, &value.M21, &value.M31, &value.M41 };
            const int registerCount = std::clamp(rows, 0, 4);
            const int componentCount = std::clamp(columns, 0, 4);
            for (int reg = 0; reg < registerCount; ++reg)
            {
                for (int component = 0; component < componentCount; ++component)
                {
                    const std::size_t cell = static_cast<std::size_t>(reg * 4 + component) * 4;
                    if (cell + sizeof(float) > size) continue;
                    const float packed = transpose
                        ? source[reg][component]
                        : source[component][reg];
                    WriteCell(bytes, offset + cell, packed);
                }
            }
        }
    }

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

    EffectParameter::EffectParameter(
        std::string name, std::string semantic, int rowCount, int columnCount, int elementCount,
        EffectParameterClass paramClass, EffectParameterType paramType,
        std::shared_ptr<CompiledStorage> storage, std::size_t byteOffset, std::size_t byteSize)
        : name_(std::move(name)), semantic_(std::move(semantic))
        , rowCount_(rowCount), columnCount_(columnCount)
        , paramClass_(paramClass), paramType_(paramType), elementCount_(elementCount)
        , elements_(std::make_unique<EffectParameterCollection>())
        , members_(std::make_unique<EffectParameterCollection>())
        , compiledStorage_(std::move(storage))
        , compiledByteOffset_(byteOffset), compiledByteSize_(byteSize)
    {
    }

    const std::string& EffectParameter::getNameProperty() const { return name_; }
    const std::string& EffectParameter::getSemanticProperty() const { return semantic_; }
    int EffectParameter::getRowCountProperty() const { return rowCount_; }
    int EffectParameter::getColumnCountProperty() const { return columnCount_; }
    EffectParameterClass EffectParameter::getParameterClassProperty() const { return paramClass_; }
    EffectParameterType EffectParameter::getParameterTypeProperty() const { return paramType_; }
    EffectParameterCollection& EffectParameter::getElementsProperty() { return *elements_; }
    const EffectParameterCollection& EffectParameter::getElementsProperty() const { return *elements_; }
    EffectParameterCollection& EffectParameter::getStructureMembersProperty() { return *members_; }
    const EffectParameterCollection& EffectParameter::getStructureMembersProperty() const { return *members_; }
    EffectAnnotationCollection& EffectParameter::getAnnotationsProperty() { return annotations_; }
    const EffectAnnotationCollection& EffectParameter::getAnnotationsProperty() const { return annotations_; }

    // --- GetValue ---
    bool EffectParameter::GetValueBoolean() const
    {
        RequireNumericParameter("GetValueBoolean");
        if (compiledStorage_)
            return ReadCell<int>(compiledStorage_->bytes, compiledByteOffset_) != 0;
        return !intData_.empty() && intData_[0] != 0;
    }
    std::vector<bool> EffectParameter::GetValueBooleanArray(int count) const
    {
        RequireNumericParameter("GetValueBooleanArray");
        if (compiledStorage_)
        {
            std::vector<bool> result;
            result.reserve(static_cast<std::size_t>(std::max(count, 0)));
            const int columns = std::max(columnCount_, 1);
            for (int i = 0; i < count; ++i)
            {
                const std::size_t cell = static_cast<std::size_t>(i / columns) * 16 +
                                         static_cast<std::size_t>(i % columns) * 4;
                if (cell + sizeof(int) > compiledByteSize_) break;
                result.push_back(ReadCell<int>(compiledStorage_->bytes,
                                                compiledByteOffset_ + cell) != 0);
            }
            return result;
        }
        std::vector<bool> r;
        for (int i = 0; i < count && i < (int)intData_.size(); ++i)
            r.push_back(intData_[i] != 0);
        return r;
    }
    int EffectParameter::GetValueInt32() const
    {
        RequireNumericParameter("GetValueInt32");
        if (compiledStorage_)
            return ReadCell<int>(compiledStorage_->bytes, compiledByteOffset_);
        return intData_.empty() ? 0 : intData_[0];
    }
    std::vector<int> EffectParameter::GetValueInt32Array(int count) const
    {
        RequireNumericParameter("GetValueInt32Array");
        if (compiledStorage_)
        {
            std::vector<int> result;
            result.reserve(static_cast<std::size_t>(std::max(count, 0)));
            const int columns = std::max(columnCount_, 1);
            for (int i = 0; i < count; ++i)
            {
                const std::size_t cell = static_cast<std::size_t>(i / columns) * 16 +
                                         static_cast<std::size_t>(i % columns) * 4;
                if (cell + sizeof(int) > compiledByteSize_) break;
                result.push_back(ReadCell<int>(compiledStorage_->bytes,
                                               compiledByteOffset_ + cell));
            }
            return result;
        }
        const int resultCount = std::clamp(count, 0, static_cast<int>(intData_.size()));
        std::vector<int> r(intData_.begin(), intData_.begin() + resultCount);
        return r;
    }
    float EffectParameter::GetValueSingle() const
    {
        RequireNumericParameter("GetValueSingle");
        if (compiledStorage_)
            return ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_);
        return floatData_.empty() ? 0.0f : floatData_[0];
    }
    std::vector<float> EffectParameter::GetValueSingleArray(int count) const
    {
        RequireNumericParameter("GetValueSingleArray");
        if (compiledStorage_)
        {
            std::vector<float> result;
            result.reserve(static_cast<std::size_t>(std::max(count, 0)));
            const int columns = std::max(columnCount_, 1);
            for (int i = 0; i < count; ++i)
            {
                const std::size_t cell = static_cast<std::size_t>(i / columns) * 16 +
                                         static_cast<std::size_t>(i % columns) * 4;
                if (cell + sizeof(float) > compiledByteSize_) break;
                result.push_back(ReadCell<float>(compiledStorage_->bytes,
                                                 compiledByteOffset_ + cell));
            }
            return result;
        }
        const int resultCount = std::clamp(count, 0, static_cast<int>(floatData_.size()));
        std::vector<float> r(floatData_.begin(), floatData_.begin() + resultCount);
        return r;
    }
    std::string EffectParameter::GetValueString() const
    {
        // XNA's own D3DX-backed getter rejects a parameter whose reflected type is not String
        // (InvalidCastException) rather than returning an empty string. That check is only
        // meaningful where the type came from real reflection, so it applies to a compiled
        // effect's parameters; a CNA-constructed stock/CNAEXT parameter keeps the lenient
        // behaviour it has always had.
        if (compiledStorage_)
        {
            RequireStringParameter("GetValueString");
            return compiledStorage_->stringValue;
        }
        return stringData_;
    }

    void EffectParameter::RequireStringParameter(const char* operation) const
    {
        if (paramType_ == EffectParameterType::String) return;
        throw System::InvalidCastException(
            std::string("EffectParameter::") + operation + " requires a String parameter; '" +
            name_ + "' is not one.");
    }

    namespace
    {
        /// The reflected object type's public XNA name, for FX-105's refusal text.
        [[nodiscard]] const char* ObjectParameterTypeName(EffectParameterType type)
        {
            switch (type)
            {
                case EffectParameterType::String:      return "String";
                case EffectParameterType::Texture:     return "Texture";
                case EffectParameterType::Texture1D:   return "Texture1D";
                case EffectParameterType::Texture2D:   return "Texture2D";
                case EffectParameterType::Texture3D:   return "Texture3D";
                case EffectParameterType::TextureCube: return "TextureCube";
                default:                               return "an object";
            }
        }
    }

    void EffectParameter::RequireNumericParameter(const char* operation) const
    {
        // plans/plan_fx.md FX-105: the other direction of the type check FX-089 added.
        //
        // A compiled effect's OBJECT parameters -- strings, textures, samplers, shaders -- do not
        // store a value in the register file at all. What sits at their byte offset is the effect
        // object table's INDEX, so reading it through a numeric accessor returns that index
        // reinterpreted as a float or an int, and writing through one overwrites the index and
        // detaches the parameter from its object. Both were silent. XNA rejects the same pairings
        // with InvalidCastException.
        //
        // Only the OBJECT class is refused. A `Struct` parameter has real numeric storage behind
        // it and its members are reachable through StructureMembers, so it is left alone; so is
        // every CNA-constructed stock/CNAEXT parameter, which has no compiled storage and keeps
        // the lenient behaviour the C API's standalone-parameter tests rely on (the same carve-out
        // RequireStringParameter makes).
        if (paramClass_ != EffectParameterClass::Object) return;
        throw System::InvalidCastException(
            std::string("EffectParameter::") + operation + " requires a numeric parameter; '" +
            name_ + "' is an object parameter (" + ObjectParameterTypeName(paramType_) +
            "), whose value storage is an effect object-table index rather than a number.");
    }

    Matrix EffectParameter::GetValueMatrix() const
    {
        RequireNumericParameter("GetValueMatrix");
        if (compiledStorage_)
            return ReadCompiledMatrix(compiledStorage_->bytes, compiledByteOffset_,
                                      compiledByteSize_, rowCount_, columnCount_, false);
        if (floatData_.size() < 16) return Matrix::getIdentityProperty();
        return Matrix(floatData_[0], floatData_[4], floatData_[8],  floatData_[12],
                      floatData_[1], floatData_[5], floatData_[9],  floatData_[13],
                      floatData_[2], floatData_[6], floatData_[10], floatData_[14],
                      floatData_[3], floatData_[7], floatData_[11], floatData_[15]);
    }
    std::vector<Matrix> EffectParameter::GetValueMatrixArray(int count) const
    {
        RequireNumericParameter("GetValueMatrixArray");
        if (compiledStorage_)
        {
            std::vector<Matrix> result;
            const std::size_t stride = static_cast<std::size_t>(std::max(rowCount_, 1)) * 16;
            for (int i = 0; i < count; ++i)
            {
                const std::size_t matrixOffset = static_cast<std::size_t>(i) * stride;
                if (matrixOffset + stride > compiledByteSize_) break;
                result.push_back(ReadCompiledMatrix(
                    compiledStorage_->bytes, compiledByteOffset_ + matrixOffset,
                    stride, rowCount_, columnCount_, false));
            }
            return result;
        }
        std::vector<Matrix> r;
        for (int i = 0; i < count && (i + 1) * 16 <= (int)floatData_.size(); ++i)
        {
            const float* d = floatData_.data() + i * 16;
            r.push_back(Matrix(d[0], d[4], d[8],  d[12],
                               d[1], d[5], d[9],  d[13],
                               d[2], d[6], d[10], d[14],
                               d[3], d[7], d[11], d[15]));
        }
        return r;
    }
    Matrix EffectParameter::GetValueMatrixTranspose() const
    {
        RequireNumericParameter("GetValueMatrixTranspose");
        if (compiledStorage_)
            return ReadCompiledMatrix(compiledStorage_->bytes, compiledByteOffset_,
                                      compiledByteSize_, rowCount_, columnCount_, true);
        return Matrix::Transpose(GetValueMatrix());
    }
    std::vector<Matrix> EffectParameter::GetValueMatrixTransposeArray(int count) const
    {
        RequireNumericParameter("GetValueMatrixTransposeArray");
        if (compiledStorage_)
        {
            std::vector<Matrix> result;
            const std::size_t stride = static_cast<std::size_t>(std::max(rowCount_, 1)) * 16;
            for (int i = 0; i < count; ++i)
            {
                const std::size_t matrixOffset = static_cast<std::size_t>(i) * stride;
                if (matrixOffset + stride > compiledByteSize_) break;
                result.push_back(ReadCompiledMatrix(
                    compiledStorage_->bytes, compiledByteOffset_ + matrixOffset,
                    stride, rowCount_, columnCount_, true));
            }
            return result;
        }
        auto result = GetValueMatrixArray(count);
        for (auto& matrix : result) matrix = Matrix::Transpose(matrix);
        return result;
    }
    Quaternion EffectParameter::GetValueQuaternion() const
    {
        RequireNumericParameter("GetValueQuaternion");
        if (compiledStorage_)
        {
            return {
                ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_),
                ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + 4),
                ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + 8),
                ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + 12, 1.0f)
            };
        }
        if (floatData_.size() < 4) return {0.0f, 0.0f, 0.0f, 1.0f};
        return {floatData_[0], floatData_[1], floatData_[2], floatData_[3]};
    }
    std::vector<Quaternion> EffectParameter::GetValueQuaternionArray(int count) const
    {
        RequireNumericParameter("GetValueQuaternionArray");
        if (compiledStorage_)
        {
            std::vector<Quaternion> result;
            for (int i = 0; i < count; ++i)
            {
                const std::size_t offset = static_cast<std::size_t>(i) * 16;
                if (offset + 16 > compiledByteSize_) break;
                result.push_back({
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + offset),
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + offset + 4),
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + offset + 8),
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + offset + 12)
                });
            }
            return result;
        }
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
        RequireNumericParameter("GetValueVector2");
        if (compiledStorage_)
            return {ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_),
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + 4)};
        if (floatData_.size() < 2) return {};
        return {floatData_[0], floatData_[1]};
    }
    std::vector<Vector2> EffectParameter::GetValueVector2Array(int count) const
    {
        RequireNumericParameter("GetValueVector2Array");
        if (compiledStorage_)
        {
            std::vector<Vector2> result;
            for (int i = 0; i < count; ++i)
            {
                const std::size_t offset = static_cast<std::size_t>(i) * 16;
                if (offset + 8 > compiledByteSize_) break;
                result.push_back({
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + offset),
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + offset + 4)
                });
            }
            return result;
        }
        std::vector<Vector2> r;
        for (int i = 0; i < count && (i + 1) * 2 <= (int)floatData_.size(); ++i)
            r.push_back({floatData_[i * 2], floatData_[i * 2 + 1]});
        return r;
    }
    Vector3 EffectParameter::GetValueVector3() const
    {
        RequireNumericParameter("GetValueVector3");
        if (compiledStorage_)
            return {ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_),
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + 4),
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + 8)};
        if (floatData_.size() < 3) return {};
        return {floatData_[0], floatData_[1], floatData_[2]};
    }
    std::vector<Vector3> EffectParameter::GetValueVector3Array(int count) const
    {
        RequireNumericParameter("GetValueVector3Array");
        if (compiledStorage_)
        {
            std::vector<Vector3> result;
            for (int i = 0; i < count; ++i)
            {
                const std::size_t offset = static_cast<std::size_t>(i) * 16;
                if (offset + 12 > compiledByteSize_) break;
                result.push_back({
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + offset),
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + offset + 4),
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + offset + 8)
                });
            }
            return result;
        }
        std::vector<Vector3> r;
        for (int i = 0; i < count && (i + 1) * 3 <= (int)floatData_.size(); ++i)
            r.push_back({floatData_[i * 3], floatData_[i * 3 + 1], floatData_[i * 3 + 2]});
        return r;
    }
    Vector4 EffectParameter::GetValueVector4() const
    {
        RequireNumericParameter("GetValueVector4");
        if (compiledStorage_)
            return {ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_),
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + 4),
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + 8),
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + 12, 1.0f)};
        if (floatData_.size() < 4) return {0.0f, 0.0f, 0.0f, 1.0f};
        return {floatData_[0], floatData_[1], floatData_[2], floatData_[3]};
    }
    std::vector<Vector4> EffectParameter::GetValueVector4Array(int count) const
    {
        RequireNumericParameter("GetValueVector4Array");
        if (compiledStorage_)
        {
            std::vector<Vector4> result;
            for (int i = 0; i < count; ++i)
            {
                const std::size_t offset = static_cast<std::size_t>(i) * 16;
                if (offset + 16 > compiledByteSize_) break;
                result.push_back({
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + offset),
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + offset + 4),
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + offset + 8),
                    ReadCell<float>(compiledStorage_->bytes, compiledByteOffset_ + offset + 12)
                });
            }
            return result;
        }
        std::vector<Vector4> r;
        for (int i = 0; i < count && (i + 1) * 4 <= (int)floatData_.size(); ++i)
            r.push_back({floatData_[i * 4], floatData_[i * 4 + 1], floatData_[i * 4 + 2], floatData_[i * 4 + 3]});
        return r;
    }
    Texture2D* EffectParameter::GetValueTexture2D() const
    {
        return compiledStorage_ ? dynamic_cast<Texture2D*>(compiledStorage_->texture)
                                : texture2DData_;
    }
    Texture3D* EffectParameter::GetValueTexture3D() const
    {
        return compiledStorage_ ? dynamic_cast<Texture3D*>(compiledStorage_->texture)
                                : texture3DData_;
    }
    TextureCube* EffectParameter::GetValueTextureCube() const
    {
        return compiledStorage_ ? dynamic_cast<TextureCube*>(compiledStorage_->texture)
                                : textureCubeData_;
    }

    // --- SetValue ---
    void EffectParameter::SetValue(bool value)
    {
        RequireNumericParameter("SetValue(bool)");
        if (compiledStorage_)
        {
            const int cell = value ? 1 : 0;
            if (WriteCell(compiledStorage_->bytes, compiledByteOffset_, cell))
                compiledStorage_->dirty = true;
            return;
        }
        intData_ = {value ? 1 : 0};
    }
    void EffectParameter::SetValue(const std::vector<bool>& v)
    {
        RequireNumericParameter("SetValue(bool[])");
        if (compiledStorage_)
        {
            const int columns = std::max(columnCount_, 1);
            for (std::size_t i = 0; i < v.size(); ++i)
            {
                const std::size_t cellOffset = (i / static_cast<std::size_t>(columns)) * 16 +
                    (i % static_cast<std::size_t>(columns)) * 4;
                if (cellOffset + sizeof(int) > compiledByteSize_) break;
                const int cell = v[i] ? 1 : 0;
                WriteCell(compiledStorage_->bytes, compiledByteOffset_ + cellOffset, cell);
            }
            compiledStorage_->dirty = true;
            return;
        }
        intData_.clear();
        for (bool b : v) intData_.push_back(b ? 1 : 0);
    }
    void EffectParameter::SetValue(int value)
    {
        RequireNumericParameter("SetValue(int)");
        if (compiledStorage_)
        {
            const bool wrote = paramType_ == EffectParameterType::Single
                ? WriteCell(compiledStorage_->bytes, compiledByteOffset_, static_cast<float>(value))
                : WriteCell(compiledStorage_->bytes, compiledByteOffset_, value);
            if (wrote) compiledStorage_->dirty = true;
            return;
        }
        intData_ = {value};
    }
    void EffectParameter::SetValue(const std::vector<int>& v)
    {
        RequireNumericParameter("SetValue(int[])");
        if (compiledStorage_)
        {
            const int columns = std::max(columnCount_, 1);
            for (std::size_t i = 0; i < v.size(); ++i)
            {
                const std::size_t cellOffset = (i / static_cast<std::size_t>(columns)) * 16 +
                    (i % static_cast<std::size_t>(columns)) * 4;
                if (cellOffset + sizeof(int) > compiledByteSize_) break;
                WriteCell(compiledStorage_->bytes, compiledByteOffset_ + cellOffset, v[i]);
            }
            compiledStorage_->dirty = true;
            return;
        }
        intData_ = v;
    }
    void EffectParameter::SetValue(float value)
    {
        RequireNumericParameter("SetValue(float)");
        if (compiledStorage_)
        {
            if (WriteCell(compiledStorage_->bytes, compiledByteOffset_, value))
                compiledStorage_->dirty = true;
            return;
        }
        floatData_ = {value};
    }
    void EffectParameter::SetValue(const std::vector<float>& v)
    {
        RequireNumericParameter("SetValue(float[])");
        if (compiledStorage_)
        {
            const int columns = std::max(columnCount_, 1);
            for (std::size_t i = 0; i < v.size(); ++i)
            {
                const std::size_t cellOffset = (i / static_cast<std::size_t>(columns)) * 16 +
                    (i % static_cast<std::size_t>(columns)) * 4;
                if (cellOffset + sizeof(float) > compiledByteSize_) break;
                WriteCell(compiledStorage_->bytes, compiledByteOffset_ + cellOffset, v[i]);
            }
            compiledStorage_->dirty = true;
            return;
        }
        floatData_ = v;
    }
    void EffectParameter::SetValue(const std::string& v)
    {
        if (compiledStorage_)
        {
            // XNA 4.0 semantics (Microsoft.Xna.Framework.Graphics.EffectParameter.SetValue(string)):
            // reject a non-String parameter with InvalidCastException, otherwise store the value
            // so GetValueString() reads it back. A compiled effect's string objects are pure CPU
            // reflection data -- no shader stage consumes one -- so CNA owns them per instance
            // instead of mutating MojoShader's shared object table. FNA leaves this unimplemented
            // (`throw new NotImplementedException("effect->objects[?]")`); CNA follows XNA here,
            // which is the specification FNA is itself approximating.
            RequireStringParameter("SetValue");
            compiledStorage_->stringValue = v;
            compiledStorage_->dirty = true;
            return;
        }
        stringData_ = v;
    }
    void EffectParameter::SetValue(Texture* v)
    {
        if (compiledStorage_)
        {
            compiledStorage_->texture = v;
            compiledStorage_->dirty = true;
            return;
        }
        textureData_ = v;
    }
    void EffectParameter::SetValue(Texture2D* v)
    {
        if (compiledStorage_)
        {
            SetValue(static_cast<Texture*>(v));
            return;
        }
        texture2DData_ = v;
    }
    void EffectParameter::SetValue(Texture3D* v)
    {
        if (compiledStorage_)
        {
            SetValue(static_cast<Texture*>(v));
            return;
        }
        texture3DData_ = v;
    }
    void EffectParameter::SetValue(TextureCube* v)
    {
        if (compiledStorage_)
        {
            SetValue(static_cast<Texture*>(v));
            return;
        }
        textureCubeData_ = v;
    }

    void EffectParameter::SetValue(const Matrix& m)
    {
        RequireNumericParameter("SetValue(Matrix)");
        if (compiledStorage_)
        {
            WriteCompiledMatrix(compiledStorage_->bytes, compiledByteOffset_, compiledByteSize_,
                                rowCount_, columnCount_, m, false);
            compiledStorage_->dirty = true;
            return;
        }
        floatData_ = {
            m.M11, m.M21, m.M31, m.M41,
            m.M12, m.M22, m.M32, m.M42,
            m.M13, m.M23, m.M33, m.M43,
            m.M14, m.M24, m.M34, m.M44
        };
    }
    void EffectParameter::SetValue(const std::vector<Matrix>& v)
    {
        RequireNumericParameter("SetValue(Matrix[])");
        if (compiledStorage_)
        {
            const std::size_t stride = static_cast<std::size_t>(std::max(rowCount_, 1)) * 16;
            for (std::size_t i = 0; i < v.size(); ++i)
            {
                const std::size_t offset = i * stride;
                if (offset + stride > compiledByteSize_) break;
                WriteCompiledMatrix(compiledStorage_->bytes, compiledByteOffset_ + offset,
                                    stride, rowCount_, columnCount_, v[i], false);
            }
            compiledStorage_->dirty = true;
            return;
        }
        floatData_.clear();
        for (const auto& m : v)
        {
            floatData_.insert(floatData_.end(),
                {m.M11, m.M21, m.M31, m.M41,
                 m.M12, m.M22, m.M32, m.M42,
                 m.M13, m.M23, m.M33, m.M43,
                 m.M14, m.M24, m.M34, m.M44});
        }
    }
    void EffectParameter::SetValueTranspose(const Matrix& m)
    {
        RequireNumericParameter("SetValueTranspose(Matrix)");
        if (compiledStorage_)
        {
            WriteCompiledMatrix(compiledStorage_->bytes, compiledByteOffset_, compiledByteSize_,
                                rowCount_, columnCount_, m, true);
            compiledStorage_->dirty = true;
            return;
        }
        SetValue(Matrix::Transpose(m));
    }
    void EffectParameter::SetValueTranspose(const std::vector<Matrix>& v)
    {
        RequireNumericParameter("SetValueTranspose(Matrix[])");
        if (compiledStorage_)
        {
            const std::size_t stride = static_cast<std::size_t>(std::max(rowCount_, 1)) * 16;
            for (std::size_t i = 0; i < v.size(); ++i)
            {
                const std::size_t offset = i * stride;
                if (offset + stride > compiledByteSize_) break;
                WriteCompiledMatrix(compiledStorage_->bytes, compiledByteOffset_ + offset,
                                    stride, rowCount_, columnCount_, v[i], true);
            }
            compiledStorage_->dirty = true;
            return;
        }
        std::vector<Matrix> transposed;
        transposed.reserve(v.size());
        for (const auto& matrix : v) transposed.push_back(Matrix::Transpose(matrix));
        SetValue(transposed);
    }
    void EffectParameter::SetValue(const Quaternion& q)
    {
        RequireNumericParameter("SetValue(Quaternion)");
        if (compiledStorage_)
        {
            const float values[4] = {q.X, q.Y, q.Z, q.W};
            if (compiledByteSize_ >= sizeof(values))
            {
                std::memcpy(compiledStorage_->bytes.data() + compiledByteOffset_,
                            values, sizeof(values));
                compiledStorage_->dirty = true;
            }
            return;
        }
        floatData_ = {q.X, q.Y, q.Z, q.W};
    }
    void EffectParameter::SetValue(const std::vector<Quaternion>& v)
    {
        RequireNumericParameter("SetValue(Quaternion[])");
        if (compiledStorage_)
        {
            for (std::size_t i = 0; i < v.size(); ++i)
            {
                const std::size_t offset = i * 16;
                if (offset + 16 > compiledByteSize_) break;
                const float values[4] = {v[i].X, v[i].Y, v[i].Z, v[i].W};
                std::memcpy(compiledStorage_->bytes.data() + compiledByteOffset_ + offset,
                            values, sizeof(values));
            }
            compiledStorage_->dirty = true;
            return;
        }
        floatData_.clear();
        for (const auto& q : v) { floatData_.push_back(q.X); floatData_.push_back(q.Y); floatData_.push_back(q.Z); floatData_.push_back(q.W); }
    }
    void EffectParameter::SetValue(const Vector2& v)
    {
        RequireNumericParameter("SetValue(Vector2)");
        if (compiledStorage_)
        {
            const float values[2] = {v.X, v.Y};
            if (compiledByteSize_ >= sizeof(values))
            {
                std::memcpy(compiledStorage_->bytes.data() + compiledByteOffset_,
                            values, sizeof(values));
                compiledStorage_->dirty = true;
            }
            return;
        }
        floatData_ = {v.X, v.Y};
    }
    void EffectParameter::SetValue(const std::vector<Vector2>& v)
    {
        RequireNumericParameter("SetValue(Vector2[])");
        if (compiledStorage_)
        {
            for (std::size_t i = 0; i < v.size(); ++i)
            {
                const std::size_t offset = i * 16;
                if (offset + 8 > compiledByteSize_) break;
                const float values[2] = {v[i].X, v[i].Y};
                std::memcpy(compiledStorage_->bytes.data() + compiledByteOffset_ + offset,
                            values, sizeof(values));
            }
            compiledStorage_->dirty = true;
            return;
        }
        floatData_.clear();
        for (const auto& e : v) { floatData_.push_back(e.X); floatData_.push_back(e.Y); }
    }
    void EffectParameter::SetValue(const Vector3& v)
    {
        RequireNumericParameter("SetValue(Vector3)");
        if (compiledStorage_)
        {
            const float values[3] = {v.X, v.Y, v.Z};
            if (compiledByteSize_ >= sizeof(values))
            {
                std::memcpy(compiledStorage_->bytes.data() + compiledByteOffset_,
                            values, sizeof(values));
                compiledStorage_->dirty = true;
            }
            return;
        }
        floatData_ = {v.X, v.Y, v.Z};
    }
    void EffectParameter::SetValue(const std::vector<Vector3>& v)
    {
        RequireNumericParameter("SetValue(Vector3[])");
        if (compiledStorage_)
        {
            for (std::size_t i = 0; i < v.size(); ++i)
            {
                const std::size_t offset = i * 16;
                if (offset + 12 > compiledByteSize_) break;
                const float values[3] = {v[i].X, v[i].Y, v[i].Z};
                std::memcpy(compiledStorage_->bytes.data() + compiledByteOffset_ + offset,
                            values, sizeof(values));
            }
            compiledStorage_->dirty = true;
            return;
        }
        floatData_.clear();
        for (const auto& e : v) { floatData_.push_back(e.X); floatData_.push_back(e.Y); floatData_.push_back(e.Z); }
    }
    void EffectParameter::SetValue(const Vector4& v)
    {
        RequireNumericParameter("SetValue(Vector4)");
        if (compiledStorage_)
        {
            const float values[4] = {v.X, v.Y, v.Z, v.W};
            if (compiledByteSize_ >= sizeof(values))
            {
                std::memcpy(compiledStorage_->bytes.data() + compiledByteOffset_,
                            values, sizeof(values));
                compiledStorage_->dirty = true;
            }
            return;
        }
        floatData_ = {v.X, v.Y, v.Z, v.W};
    }
    void EffectParameter::SetValue(const std::vector<Vector4>& v)
    {
        RequireNumericParameter("SetValue(Vector4[])");
        if (compiledStorage_)
        {
            for (std::size_t i = 0; i < v.size(); ++i)
            {
                const std::size_t offset = i * 16;
                if (offset + 16 > compiledByteSize_) break;
                const float values[4] = {v[i].X, v[i].Y, v[i].Z, v[i].W};
                std::memcpy(compiledStorage_->bytes.data() + compiledByteOffset_ + offset,
                            values, sizeof(values));
            }
            compiledStorage_->dirty = true;
            return;
        }
        floatData_.clear();
        for (const auto& e : v) { floatData_.push_back(e.X); floatData_.push_back(e.Y); floatData_.push_back(e.Z); floatData_.push_back(e.W); }
    }

    bool EffectParameter::IsCompiledInternal() const noexcept
    {
        return compiledStorage_ != nullptr;
    }

    const void* EffectParameter::GetRawValueInternal() const noexcept
    {
        if (!compiledStorage_ || compiledByteOffset_ > compiledStorage_->bytes.size()) return nullptr;
        return compiledStorage_->bytes.data() + compiledByteOffset_;
    }

    std::size_t EffectParameter::GetRawValueSizeInternal() const noexcept
    {
        return compiledStorage_ ? compiledByteSize_ : 0;
    }

    Texture* EffectParameter::GetTextureInternal() const noexcept
    {
        return compiledStorage_ ? compiledStorage_->texture : textureData_;
    }

    std::uint32_t EffectParameter::GetRuntimeIndexInternal() const noexcept
    {
        return compiledStorage_ ? compiledStorage_->runtimeIndex : 0;
    }

    bool EffectParameter::IsDirtyInternal() const noexcept
    {
        return compiledStorage_ && compiledStorage_->dirty;
    }

    void EffectParameter::MarkCleanInternal() noexcept
    {
        if (compiledStorage_) compiledStorage_->dirty = false;
    }

    void EffectParameter::CopyMutableValueFromInternal(const EffectParameter& source)
    {
        if (!compiledStorage_ || !source.compiledStorage_) return;
        const std::size_t writable = std::min(compiledStorage_->bytes.size(),
                                              source.compiledStorage_->bytes.size());
        std::copy_n(source.compiledStorage_->bytes.begin(), writable,
                    compiledStorage_->bytes.begin());
        compiledStorage_->stringValue = source.compiledStorage_->stringValue;
        compiledStorage_->texture = source.compiledStorage_->texture;
        compiledStorage_->dirty = true;
    }
}
