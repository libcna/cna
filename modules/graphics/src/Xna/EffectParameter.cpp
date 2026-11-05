// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidCastException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string_view>
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

    // MTH-SAFE-007: a compiled Single cell read or written as Int32 truncates (SOFTWARE-270; XNA's
    // GetValueInt32 IL hands the cell to the native effect's GetInt, vtable offset 0x6c). NaN and
    // values outside int's range have no truncated value, and converting them in C++ is undefined
    // behaviour; they answer 0x80000000, what the x86 truncating conversion (cvttss2si) gives a
    // native (INT) cast for every such value. Found by the long fuzz campaign (UBSan).
    int TruncateToInt32(float value)
    {
        if (!(value >= -2147483648.0f && value < 2147483648.0f))
            return std::numeric_limits<int>::min();
        return static_cast<int>(value);
    }

    void RequirePositiveArrayCount(int count)
    {
        if (count <= 0)
            throw System::ArgumentOutOfRangeException();
    }

    // XNA EffectParameter.GetValueSingleArray IL_0043..00d1 and SetValue(float[])
    // IL_005b..00e4 traverse logical matrix rows, independently of physical register order.
    bool TryGetCompiledFlatCellOffset(std::size_t logicalIndex, int columnCount,
                                      std::size_t byteSize, std::size_t& offset,
                                      int matrixRowCount = 0)
    {
        const std::size_t columns = static_cast<std::size_t>(std::max(columnCount, 1));
        if (matrixRowCount > 0)
        {
            // XNA EffectParameter.GetValueSingleArray and its numeric siblings
            // flatten matrix components row-first, without register padding.
            const std::size_t rows = static_cast<std::size_t>(matrixRowCount);
            const std::size_t matrix = logicalIndex / (rows * columns);
            const std::size_t row = (logicalIndex / columns) % rows;
            const std::size_t column = logicalIndex % columns;
            offset = (matrix * columns + column) * 16u + row * 4u;
        }
        else
        {
            offset = (logicalIndex / columns) * 16u + (logicalIndex % columns) * 4u;
        }
        return offset <= byteSize && sizeof(std::uint32_t) <= byteSize - offset;
    }
}

namespace Microsoft::Xna::Framework::Graphics
{
    namespace
    {
        Matrix ReadCompiledMatrix(const std::vector<std::uint8_t>& bytes, std::size_t offset,
                                  std::size_t size, int rows, int columns, bool transpose,
                                  bool zeroFill)
        {
            Matrix result = zeroFill ? Matrix() : Matrix::getIdentityProperty();
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
        if (compiledStorage_ || stockParameter_)
            RequireScalarGetterShape();
        if (compiledStorage_)
        {
            return paramType_ == EffectParameterType::Single
                ? ReadCompiledNumericCellAsFloat() != 0.0f
                : ReadCompiledNumericCellAsInt() != 0;
        }
        return !intData_.empty() && intData_[0] != 0;
    }
    std::vector<bool> EffectParameter::GetValueBooleanArray(int count) const
    {
        RequirePositiveArrayCount(count);
        RequireNumericParameter("GetValueBooleanArray");
        if (stockParameter_ && !compiledStorage_ && paramClass_ == EffectParameterClass::Matrix)
        {
            const auto values = GetValueSingleArray(count);
            std::vector<bool> result(values.size());
            for (std::size_t i = 0; i < values.size(); ++i) result[i] = values[i] != 0.0f;
            return result;
        }
        if (compiledStorage_)
        {
            std::vector<bool> result(static_cast<std::size_t>(count), false);
            for (int i = 0; i < count; ++i)
            {
                std::size_t cell = 0;
                if (!TryGetCompiledFlatCellOffset(static_cast<std::size_t>(i),
                        paramClass_ == EffectParameterClass::Matrix ? MatrixRegisterCount() : columnCount_,
                        compiledByteSize_, cell,
                        paramClass_ == EffectParameterClass::Matrix ? MatrixComponentCount() : 0))
                    break;
                result[static_cast<std::size_t>(i)] = paramType_ == EffectParameterType::Single
                    ? ReadCompiledNumericCellAsFloat(cell) != 0.0f
                    : ReadCompiledNumericCellAsInt(cell) != 0;
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
        if (compiledStorage_ || stockParameter_)
            RequireScalarGetterShape();
        if (compiledStorage_)
        {
            return ReadCompiledNumericCellAsInt();
        }
        return intData_.empty() ? 0 : intData_[0];
    }
    std::vector<int> EffectParameter::GetValueInt32Array(int count) const
    {
        RequirePositiveArrayCount(count);
        RequireNumericParameter("GetValueInt32Array");
        if (stockParameter_ && !compiledStorage_ && paramClass_ == EffectParameterClass::Matrix)
        {
            const auto values = GetValueSingleArray(count);
            std::vector<int> result(values.size());
            for (std::size_t i = 0; i < values.size(); ++i) result[i] = TruncateToInt32(values[i]);
            return result;
        }
        if (compiledStorage_)
        {
            std::vector<int> result(static_cast<std::size_t>(count), 0);
            for (int i = 0; i < count; ++i)
            {
                std::size_t cell = 0;
                if (!TryGetCompiledFlatCellOffset(static_cast<std::size_t>(i),
                        paramClass_ == EffectParameterClass::Matrix ? MatrixRegisterCount() : columnCount_,
                        compiledByteSize_, cell,
                        paramClass_ == EffectParameterClass::Matrix ? MatrixComponentCount() : 0))
                    break;
                result[static_cast<std::size_t>(i)] = ReadCompiledNumericCellAsInt(cell);
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
        if (compiledStorage_ || stockParameter_)
            RequireScalarGetterShape();
        if (compiledStorage_)
        {
            return ReadCompiledNumericCellAsFloat();
        }
        return floatData_.empty() ? 0.0f : floatData_[0];
    }
    std::vector<float> EffectParameter::GetValueSingleArray(int count) const
    {
        RequirePositiveArrayCount(count);
        RequireNumericParameter("GetValueSingleArray");
        if (stockParameter_ && !compiledStorage_ && paramClass_ == EffectParameterClass::Matrix)
        {
            // Refresh GetValueSingleArray IL_0043..00d1 traverses the returned Matrix row-first.
            const Matrix m = GetValueMatrix();
            const float cells[] = {m.M11,m.M12,m.M13,m.M14, m.M21,m.M22,m.M23,m.M24,
                                   m.M31,m.M32,m.M33,m.M34, m.M41,m.M42,m.M43,m.M44};
            std::vector<float> result(static_cast<std::size_t>(count), 0.0f);
            const int available = std::min(count, rowCount_ * columnCount_);
            for (int i = 0; i < available; ++i)
                result[static_cast<std::size_t>(i)] = cells[(i / columnCount_) * 4 + i % columnCount_];
            return result;
        }
        if (compiledStorage_)
        {
            std::vector<float> result(static_cast<std::size_t>(count), 0.0f);
            for (int i = 0; i < count; ++i)
            {
                std::size_t cell = 0;
                if (!TryGetCompiledFlatCellOffset(static_cast<std::size_t>(i),
                        paramClass_ == EffectParameterClass::Matrix ? MatrixRegisterCount() : columnCount_,
                        compiledByteSize_, cell,
                        paramClass_ == EffectParameterClass::Matrix ? MatrixComponentCount() : 0))
                    break;
                result[static_cast<std::size_t>(i)] = ReadCompiledNumericCellAsFloat(cell);
            }
            return result;
        }
        const int resultCount = std::clamp(count, 0, static_cast<int>(floatData_.size()));
        std::vector<float> r(floatData_.begin(), floatData_.begin() + resultCount);
        return r;
    }
    std::string EffectParameter::GetValueString() const
    {
        if (stockParameter_) RequireStringParameter("String accessor");
        // Refresh GetValueString IL_0000..0012 rejects non-String reflected parameters.
        // Apply that type gate to compiled and stock declarations. Standalone CNAEXT
        // parameters retain their separate unreflected storage contract.
        if (compiledStorage_)
        {
            RequireStringParameter("GetValueString");
            return compiledStorage_->stringValue;
        }
        return stringData_;
    }

    void EffectParameter::RequireStringParameter(const char* /*operation*/) const
    {
        if (paramType_ == EffectParameterType::String) return;
        // Refresh GetValueString IL_000d..0012 and SetValue(string) IL_000e..0013.
        throw System::InvalidCastException();
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
        // FX-105: the other direction of the type check FX-089 added.
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
        // Refresh EffectParameter.GetValueSingle IL_0019..001e, SetValue(float[])
        // IL_0019..001e, and GetValueMatrixArray IL_000a..0021 use default InvalidCast.
        // Primitive/vector array getters instead delegate to native D3DX; keep their
        // existing diagnostic path separate from these managed class/shape guards.
        const std::string_view accessor(operation);
        if (!accessor.starts_with("GetValue") || !accessor.ends_with("Array") ||
            accessor == "GetValueMatrixArray" || accessor == "GetValueMatrixTransposeArray")
            throw System::InvalidCastException();
        throw System::InvalidCastException(
            std::string("EffectParameter::") + operation + " requires a numeric parameter; '" +
            name_ + "' is an object parameter (" + ObjectParameterTypeName(paramType_) +
            "), whose value storage is an effect object-table index rather than a number.");
    }

    void EffectParameter::RequireScalarValueShape(EffectParameterClass parameterClass,
                                                  int columnCount) const
    {
        if (elements_->getCountProperty() != 0 || paramClass_ != parameterClass)
            throw System::InvalidCastException();
        if (parameterClass == EffectParameterClass::Vector &&
            (rowCount_ != 1 || columnCount_ != columnCount))
            throw System::InvalidCastException();
    }

    void EffectParameter::RequireArrayValueShape(EffectParameterClass parameterClass,
                                                 std::size_t valueCount,
                                                 bool enforceElementCount) const
    {
        const int elementCount = elements_->getCountProperty();
        if (paramClass_ != parameterClass || elementCount == 0 ||
            (enforceElementCount && valueCount > static_cast<std::size_t>(elementCount)))
            throw System::InvalidCastException();
    }

    void EffectParameter::RequireCompiledNumericArrayClass() const
    {
        if (paramClass_ != EffectParameterClass::Scalar &&
            paramClass_ != EffectParameterClass::Vector &&
            paramClass_ != EffectParameterClass::Matrix)
            throw System::InvalidCastException();
    }

    void EffectParameter::RequireScalarGetterShape() const
    {
        if (paramClass_ != EffectParameterClass::Scalar &&
            elements_->getCountProperty() == 0)
            throw System::InvalidCastException();
    }

    int EffectParameter::MatrixRegisterCount() const
    {
        return compiledStorage_->stockMatrixLayout ? columnCount_ : rowCount_;
    }

    int EffectParameter::MatrixComponentCount() const
    {
        return compiledStorage_->stockMatrixLayout ? rowCount_ : columnCount_;
    }

    bool EffectParameter::RequireCompiledVectorGetterShape(int columnCount) const
    {
        if (elements_->getCountProperty() != 0) return false;
        if (paramClass_ == EffectParameterClass::Scalar) return true;
        if (paramClass_ == EffectParameterClass::Vector && rowCount_ == 1 &&
            columnCount_ == columnCount)
            return false;
        throw System::InvalidCastException();
    }

    bool EffectParameter::RequireCompiledMatrixGetterShape() const
    {
        if (elements_->getCountProperty() != 0) return false;
        if (paramClass_ == EffectParameterClass::Scalar) return true;
        if (paramClass_ == EffectParameterClass::Matrix) return false;
        throw System::InvalidCastException();
    }

    int EffectParameter::ReadCompiledNumericCellAsInt(std::size_t relativeOffset) const
    {
        if (paramType_ == EffectParameterType::Single)
            return TruncateToInt32(ReadCell<float>(compiledStorage_->bytes,
                                                   compiledByteOffset_ + relativeOffset));
        if (paramType_ == EffectParameterType::Int32 || paramType_ == EffectParameterType::Bool)
            return ReadCell<int>(compiledStorage_->bytes, compiledByteOffset_ + relativeOffset);
        throw System::InvalidCastException();
    }

    float EffectParameter::ReadCompiledNumericCellAsFloat(std::size_t relativeOffset) const
    {
        if (paramType_ == EffectParameterType::Single)
            return ReadCell<float>(compiledStorage_->bytes,
                                   compiledByteOffset_ + relativeOffset);
        if (paramType_ == EffectParameterType::Int32 || paramType_ == EffectParameterType::Bool)
            return static_cast<float>(ReadCell<int>(compiledStorage_->bytes,
                                                    compiledByteOffset_ + relativeOffset));
        throw System::InvalidCastException();
    }

    void EffectParameter::SetCompiledScalarValue(float floatingValue, int integerValue,
                                                 bool sourceIsInteger)
    {
        if (elements_->getCountProperty() != 0)
            throw System::InvalidCastException();

        int rows = 1;
        int columns = 1;
        if (paramClass_ == EffectParameterClass::Vector)
        {
            if (rowCount_ != 1 || columnCount_ < 2 || columnCount_ > 4)
                throw System::InvalidCastException();
            columns = columnCount_;
        }
        else if (paramClass_ == EffectParameterClass::Matrix)
        {
            if (rowCount_ < 1 || rowCount_ > 4 || columnCount_ < 1 || columnCount_ > 4)
                throw System::InvalidCastException();
            rows = MatrixRegisterCount();
            columns = MatrixComponentCount();
        }
        else if (paramClass_ != EffectParameterClass::Scalar)
        {
            throw System::InvalidCastException();
        }

        const bool storesFloat = paramType_ == EffectParameterType::Single;
        if (!storesFloat && paramType_ != EffectParameterType::Int32 &&
            paramType_ != EffectParameterType::Bool)
            throw System::InvalidCastException();

        // Float storage accepts NaN/infinity; do not convert an unused integer value.
        const int convertedInteger = storesFloat ? 0
            : (sourceIsInteger ? integerValue : TruncateToInt32(floatingValue));
        bool wrote = false;
        for (int row = 0; row < rows; ++row)
        {
            for (int column = 0; column < columns; ++column)
            {
                const std::size_t relativeOffset = static_cast<std::size_t>(row) * 16u +
                    static_cast<std::size_t>(column) * 4u;
                if (relativeOffset + sizeof(std::uint32_t) > compiledByteSize_)
                    continue;
                const std::size_t offset = compiledByteOffset_ + relativeOffset;
                wrote |= storesFloat
                    ? WriteCell(compiledStorage_->bytes, offset, floatingValue)
                    : WriteCell(compiledStorage_->bytes, offset, convertedInteger);
            }
        }
        if (wrote) compiledStorage_->dirty = true;
    }

    void EffectParameter::RequireTextureGetterParameter(EffectParameterType requestedType) const
    {
        if (paramType_ == EffectParameterType::Texture || paramType_ == requestedType) return;
        throw System::InvalidCastException();
    }

    void EffectParameter::RequireTextureSetterParameter() const
    {
        switch (paramType_)
        {
            case EffectParameterType::Texture:
            case EffectParameterType::Texture1D:
            case EffectParameterType::Texture2D:
            case EffectParameterType::Texture3D:
            case EffectParameterType::TextureCube:
                return;
            default:
                throw System::InvalidCastException();
        }
    }

    void EffectParameter::RequireTextureValueUsable(Texture* value) const
    {
        if (value == nullptr) return;
        if (value->getIsDisposedProperty())
            throw System::ObjectDisposedException(value->getNameProperty());

        GraphicsDevice* const device = value->getGraphicsDeviceProperty();
        if (device == nullptr) return;
        for (const auto& binding : device->GetRenderTargets())
        {
            if (binding.getRenderTargetProperty() == value)
                throw System::InvalidOperationException(
                    // Refresh SetValue(Texture) IL_0022..002c, MustResolveRenderTarget.
                    "The render target must not be set on the device when it is used as a texture.");
        }
    }

    Matrix EffectParameter::GetValueMatrix() const
    {
        RequireNumericParameter("GetValueMatrix");
        // XNA matrix getters IL_00d2..00dd reject non-Matrix/non-Scalar leaves.
        if (stockParameter_)
            (void)RequireCompiledMatrixGetterShape();
        if (compiledStorage_)
        {
            if (RequireCompiledMatrixGetterShape())
            {
                const float value = ReadCompiledNumericCellAsFloat();
                return Matrix(value, value, value, value,
                              value, value, value, value,
                              value, value, value, value,
                              value, value, value, value);
            }
            return ReadCompiledMatrix(compiledStorage_->bytes, compiledByteOffset_,
                                      compiledByteSize_, MatrixRegisterCount(), MatrixComponentCount(),
                                      false, compiledStorage_->stockMatrixLayout);
        }
        if (floatData_.size() < 16) return Matrix::getIdentityProperty();
        return Matrix(floatData_[0], floatData_[4], floatData_[8],  floatData_[12],
                      floatData_[1], floatData_[5], floatData_[9],  floatData_[13],
                      floatData_[2], floatData_[6], floatData_[10], floatData_[14],
                      floatData_[3], floatData_[7], floatData_[11], floatData_[15]);
    }
    std::vector<Matrix> EffectParameter::GetValueMatrixArray(int count) const
    {
        RequirePositiveArrayCount(count);
        RequireNumericParameter("GetValueMatrixArray");
        // XNA matrix array getters IL_000a..0021 require Matrix and Elements.
        if (stockParameter_ && (paramClass_ != EffectParameterClass::Matrix ||
                                elements_->getCountProperty() == 0))
            throw System::InvalidCastException();
        if (compiledStorage_)
        {
            if (paramClass_ != EffectParameterClass::Matrix ||
                elements_->getCountProperty() == 0)
                throw System::InvalidCastException();
            std::vector<Matrix> result(static_cast<std::size_t>(count));
            const std::size_t stride = static_cast<std::size_t>(std::max(MatrixRegisterCount(), 1)) * 16;
            const int available = std::min(count, elements_->getCountProperty());
            for (int i = 0; i < available; ++i)
            {
                const std::size_t matrixOffset = static_cast<std::size_t>(i) * stride;
                if (matrixOffset + stride > compiledByteSize_) break;
                result[static_cast<std::size_t>(i)] = ReadCompiledMatrix(
                    compiledStorage_->bytes, compiledByteOffset_ + matrixOffset,
                    stride, MatrixRegisterCount(), MatrixComponentCount(),
                    false, compiledStorage_->stockMatrixLayout);
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
        // XNA matrix getters IL_00d2..00dd reject non-Matrix/non-Scalar leaves.
        if (stockParameter_)
            (void)RequireCompiledMatrixGetterShape();
        if (compiledStorage_)
        {
            if (RequireCompiledMatrixGetterShape())
            {
                const float value = ReadCompiledNumericCellAsFloat();
                return Matrix(value, value, value, value,
                              value, value, value, value,
                              value, value, value, value,
                              value, value, value, value);
            }
            return ReadCompiledMatrix(compiledStorage_->bytes, compiledByteOffset_,
                                      compiledByteSize_, MatrixRegisterCount(), MatrixComponentCount(),
                                      true, compiledStorage_->stockMatrixLayout);
        }
        return Matrix::Transpose(GetValueMatrix());
    }
    std::vector<Matrix> EffectParameter::GetValueMatrixTransposeArray(int count) const
    {
        RequirePositiveArrayCount(count);
        RequireNumericParameter("GetValueMatrixTransposeArray");
        // XNA matrix array getters IL_000a..0021 require Matrix and Elements.
        if (stockParameter_ && (paramClass_ != EffectParameterClass::Matrix ||
                                elements_->getCountProperty() == 0))
            throw System::InvalidCastException();
        if (compiledStorage_)
        {
            if (paramClass_ != EffectParameterClass::Matrix ||
                elements_->getCountProperty() == 0)
                throw System::InvalidCastException();
            std::vector<Matrix> result(static_cast<std::size_t>(count));
            const std::size_t stride = static_cast<std::size_t>(std::max(MatrixRegisterCount(), 1)) * 16;
            const int available = std::min(count, elements_->getCountProperty());
            for (int i = 0; i < available; ++i)
            {
                const std::size_t matrixOffset = static_cast<std::size_t>(i) * stride;
                if (matrixOffset + stride > compiledByteSize_) break;
                result[static_cast<std::size_t>(i)] = ReadCompiledMatrix(
                    compiledStorage_->bytes, compiledByteOffset_ + matrixOffset,
                    stride, MatrixRegisterCount(), MatrixComponentCount(),
                    true, compiledStorage_->stockMatrixLayout);
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
        // XNA GetValueQuaternion uses the reflected shape for stock effects too.
        // Vector2 IL_005c..007f; Vector3 IL_0064..0087; Vector4/Quaternion IL_006f..0092.
        if (stockParameter_)
            (void)RequireCompiledVectorGetterShape(4);
        if (compiledStorage_)
        {
            if (RequireCompiledVectorGetterShape(4))
            {
                const float value = ReadCompiledNumericCellAsFloat();
                return {value, value, value, value};
            }
            return {
                ReadCompiledNumericCellAsFloat(),
                ReadCompiledNumericCellAsFloat(4),
                ReadCompiledNumericCellAsFloat(8),
                ReadCompiledNumericCellAsFloat(12)
            };
        }
        if (floatData_.size() < 4) return {0.0f, 0.0f, 0.0f, 1.0f};
        return {floatData_[0], floatData_[1], floatData_[2], floatData_[3]};
    }
    std::vector<Quaternion> EffectParameter::GetValueQuaternionArray(int count) const
    {
        RequirePositiveArrayCount(count);
        RequireNumericParameter("GetValueQuaternionArray");
        // Refresh GetValueQuaternionArray IL_0051..0062 flattens Matrix through GetValueSingleArray.
        if (paramClass_ == EffectParameterClass::Matrix && (compiledStorage_ || stockParameter_))
        {
            std::vector<Quaternion> result(static_cast<std::size_t>(count));
            const std::size_t available = static_cast<std::size_t>(rowCount_) * columnCount_ *
                static_cast<std::size_t>(std::max(elements_->getCountProperty(), 1));
            const auto flat = GetValueSingleArray(static_cast<int>(std::min(
                static_cast<std::size_t>(count) * 4u, available)));
            const auto cell = [&](std::size_t index) { return index < flat.size() ? flat[index] : 0.0f; };
            for (std::size_t i = 0; i < result.size(); ++i)
                result[i] = Quaternion(cell(i * 4u + 0u), cell(i * 4u + 1u), cell(i * 4u + 2u), cell(i * 4u + 3u));
            return result;
        }
        if (compiledStorage_)
        {
            std::vector<Quaternion> result(static_cast<std::size_t>(count));
            std::size_t logicalIndex = 0;
            for (int i = 0; i < count; ++i)
            {
                float* components[4] = {&result[static_cast<std::size_t>(i)].X,
                                        &result[static_cast<std::size_t>(i)].Y,
                                        &result[static_cast<std::size_t>(i)].Z,
                                        &result[static_cast<std::size_t>(i)].W};
                for (float* component : components)
                {
                    std::size_t cell = 0;
                    if (!TryGetCompiledFlatCellOffset(logicalIndex++, columnCount_,
                                                      compiledByteSize_, cell))
                        return result;
                    *component = ReadCompiledNumericCellAsFloat(cell);
                }
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
        // XNA GetValueVector2 uses the reflected shape for stock effects too.
        // Vector2 IL_005c..007f; Vector3 IL_0064..0087; Vector4/Quaternion IL_006f..0092.
        if (stockParameter_)
            (void)RequireCompiledVectorGetterShape(2);
        if (compiledStorage_)
        {
            if (RequireCompiledVectorGetterShape(2))
            {
                const float value = ReadCompiledNumericCellAsFloat();
                return {value, value};
            }
            return {ReadCompiledNumericCellAsFloat(), ReadCompiledNumericCellAsFloat(4)};
        }
        if (floatData_.size() < 2) return {};
        return {floatData_[0], floatData_[1]};
    }
    std::vector<Vector2> EffectParameter::GetValueVector2Array(int count) const
    {
        RequirePositiveArrayCount(count);
        RequireNumericParameter("GetValueVector2Array");
        // Refresh GetValueVector2Array IL_0051..0062 flattens Matrix through GetValueSingleArray.
        if (paramClass_ == EffectParameterClass::Matrix && (compiledStorage_ || stockParameter_))
        {
            std::vector<Vector2> result(static_cast<std::size_t>(count));
            const std::size_t available = static_cast<std::size_t>(rowCount_) * columnCount_ *
                static_cast<std::size_t>(std::max(elements_->getCountProperty(), 1));
            const auto flat = GetValueSingleArray(static_cast<int>(std::min(
                static_cast<std::size_t>(count) * 2u, available)));
            const auto cell = [&](std::size_t index) { return index < flat.size() ? flat[index] : 0.0f; };
            for (std::size_t i = 0; i < result.size(); ++i)
                result[i] = Vector2(cell(i * 2u + 0u), cell(i * 2u + 1u));
            return result;
        }
        if (compiledStorage_)
        {
            std::vector<Vector2> result(static_cast<std::size_t>(count));
            std::size_t logicalIndex = 0;
            for (int i = 0; i < count; ++i)
            {
                float* components[2] = {&result[static_cast<std::size_t>(i)].X,
                                        &result[static_cast<std::size_t>(i)].Y};
                for (float* component : components)
                {
                    std::size_t cell = 0;
                    if (!TryGetCompiledFlatCellOffset(logicalIndex++, columnCount_,
                                                      compiledByteSize_, cell))
                        return result;
                    *component = ReadCompiledNumericCellAsFloat(cell);
                }
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
        // XNA GetValueVector3 uses the reflected shape for stock effects too.
        // Vector2 IL_005c..007f; Vector3 IL_0064..0087; Vector4/Quaternion IL_006f..0092.
        if (stockParameter_)
            (void)RequireCompiledVectorGetterShape(3);
        if (compiledStorage_)
        {
            if (RequireCompiledVectorGetterShape(3))
            {
                const float value = ReadCompiledNumericCellAsFloat();
                return {value, value, value};
            }
            return {ReadCompiledNumericCellAsFloat(), ReadCompiledNumericCellAsFloat(4),
                    ReadCompiledNumericCellAsFloat(8)};
        }
        if (floatData_.size() < 3) return {};
        return {floatData_[0], floatData_[1], floatData_[2]};
    }
    std::vector<Vector3> EffectParameter::GetValueVector3Array(int count) const
    {
        RequirePositiveArrayCount(count);
        RequireNumericParameter("GetValueVector3Array");
        // Refresh GetValueVector3Array IL_0051..0062 flattens Matrix through GetValueSingleArray.
        if (paramClass_ == EffectParameterClass::Matrix && (compiledStorage_ || stockParameter_))
        {
            std::vector<Vector3> result(static_cast<std::size_t>(count));
            const std::size_t available = static_cast<std::size_t>(rowCount_) * columnCount_ *
                static_cast<std::size_t>(std::max(elements_->getCountProperty(), 1));
            const auto flat = GetValueSingleArray(static_cast<int>(std::min(
                static_cast<std::size_t>(count) * 3u, available)));
            const auto cell = [&](std::size_t index) { return index < flat.size() ? flat[index] : 0.0f; };
            for (std::size_t i = 0; i < result.size(); ++i)
                result[i] = Vector3(cell(i * 3u + 0u), cell(i * 3u + 1u), cell(i * 3u + 2u));
            return result;
        }
        if (compiledStorage_)
        {
            std::vector<Vector3> result(static_cast<std::size_t>(count));
            std::size_t logicalIndex = 0;
            for (int i = 0; i < count; ++i)
            {
                float* components[3] = {&result[static_cast<std::size_t>(i)].X,
                                        &result[static_cast<std::size_t>(i)].Y,
                                        &result[static_cast<std::size_t>(i)].Z};
                for (float* component : components)
                {
                    std::size_t cell = 0;
                    if (!TryGetCompiledFlatCellOffset(logicalIndex++, columnCount_,
                                                      compiledByteSize_, cell))
                        return result;
                    *component = ReadCompiledNumericCellAsFloat(cell);
                }
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
        // XNA GetValueVector4 uses the reflected shape for stock effects too.
        // Vector2 IL_005c..007f; Vector3 IL_0064..0087; Vector4/Quaternion IL_006f..0092.
        if (stockParameter_)
            (void)RequireCompiledVectorGetterShape(4);
        if (compiledStorage_)
        {
            if (RequireCompiledVectorGetterShape(4))
            {
                const float value = ReadCompiledNumericCellAsFloat();
                return {value, value, value, value};
            }
            return {ReadCompiledNumericCellAsFloat(), ReadCompiledNumericCellAsFloat(4),
                    ReadCompiledNumericCellAsFloat(8), ReadCompiledNumericCellAsFloat(12)};
        }
        if (floatData_.size() < 4) return {0.0f, 0.0f, 0.0f, 1.0f};
        return {floatData_[0], floatData_[1], floatData_[2], floatData_[3]};
    }
    std::vector<Vector4> EffectParameter::GetValueVector4Array(int count) const
    {
        RequirePositiveArrayCount(count);
        RequireNumericParameter("GetValueVector4Array");
        // Refresh GetValueVector4Array IL_0051..0062 flattens Matrix through GetValueSingleArray.
        if (paramClass_ == EffectParameterClass::Matrix && (compiledStorage_ || stockParameter_))
        {
            std::vector<Vector4> result(static_cast<std::size_t>(count));
            const std::size_t available = static_cast<std::size_t>(rowCount_) * columnCount_ *
                static_cast<std::size_t>(std::max(elements_->getCountProperty(), 1));
            const auto flat = GetValueSingleArray(static_cast<int>(std::min(
                static_cast<std::size_t>(count) * 4u, available)));
            const auto cell = [&](std::size_t index) { return index < flat.size() ? flat[index] : 0.0f; };
            for (std::size_t i = 0; i < result.size(); ++i)
                result[i] = Vector4(cell(i * 4u + 0u), cell(i * 4u + 1u), cell(i * 4u + 2u), cell(i * 4u + 3u));
            return result;
        }
        if (compiledStorage_)
        {
            std::vector<Vector4> result(static_cast<std::size_t>(count));
            std::size_t logicalIndex = 0;
            for (int i = 0; i < count; ++i)
            {
                float* components[4] = {&result[static_cast<std::size_t>(i)].X,
                                        &result[static_cast<std::size_t>(i)].Y,
                                        &result[static_cast<std::size_t>(i)].Z,
                                        &result[static_cast<std::size_t>(i)].W};
                for (float* component : components)
                {
                    std::size_t cell = 0;
                    if (!TryGetCompiledFlatCellOffset(logicalIndex++, columnCount_,
                                                      compiledByteSize_, cell))
                        return result;
                    *component = ReadCompiledNumericCellAsFloat(cell);
                }
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
        if (!compiledStorage_) return texture2DData_;
        RequireTextureGetterParameter(EffectParameterType::Texture2D);
        return dynamic_cast<Texture2D*>(compiledStorage_->texture);
    }

    // --- SetValue ---
    void EffectParameter::SetValue(bool value)
    {
        RequireNumericParameter("SetValue(bool)");
        if (compiledStorage_)
        {
            const int cell = value ? 1 : 0;
            SetCompiledScalarValue(static_cast<float>(cell), cell, true);
            return;
        }
        intData_ = {value ? 1 : 0};
    }
    void EffectParameter::SetValue(const std::vector<bool>& v)
    {
        RequireNumericParameter("SetValue(bool[])");
        if (stockParameter_ && !compiledStorage_ && paramClass_ == EffectParameterClass::Matrix)
        {
            std::vector<float> values;
            values.reserve(v.size());
            for (bool value : v) values.push_back(value ? 1.0f : 0.0f);
            SetValue(values);
            return;
        }
        if (compiledStorage_)
        {
            RequireCompiledNumericArrayClass();
            for (std::size_t i = 0; i < v.size(); ++i)
            {
                std::size_t cellOffset = 0;
                if (!TryGetCompiledFlatCellOffset(i,
                        paramClass_ == EffectParameterClass::Matrix ? MatrixRegisterCount() : columnCount_,
                        compiledByteSize_, cellOffset,
                        paramClass_ == EffectParameterClass::Matrix ? MatrixComponentCount() : 0))
                    break;
                const int cell = v[i] ? 1 : 0;
                if (paramType_ == EffectParameterType::Single)
                    WriteCell(compiledStorage_->bytes, compiledByteOffset_ + cellOffset,
                              static_cast<float>(cell));
                else if (paramType_ == EffectParameterType::Int32 ||
                         paramType_ == EffectParameterType::Bool)
                    WriteCell(compiledStorage_->bytes, compiledByteOffset_ + cellOffset, cell);
                else
                    throw System::InvalidCastException();
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
            SetCompiledScalarValue(static_cast<float>(value), value, true);
            return;
        }
        intData_ = {value};
    }
    void EffectParameter::SetValue(const std::vector<int>& v)
    {
        RequireNumericParameter("SetValue(int[])");
        if (stockParameter_ && !compiledStorage_ && paramClass_ == EffectParameterClass::Matrix)
        {
            std::vector<float> values;
            values.reserve(v.size());
            for (int value : v) values.push_back(static_cast<float>(value));
            SetValue(values);
            return;
        }
        if (compiledStorage_)
        {
            RequireCompiledNumericArrayClass();
            for (std::size_t i = 0; i < v.size(); ++i)
            {
                std::size_t cellOffset = 0;
                if (!TryGetCompiledFlatCellOffset(i,
                        paramClass_ == EffectParameterClass::Matrix ? MatrixRegisterCount() : columnCount_,
                        compiledByteSize_, cellOffset,
                        paramClass_ == EffectParameterClass::Matrix ? MatrixComponentCount() : 0))
                    break;
                if (paramType_ == EffectParameterType::Single)
                    WriteCell(compiledStorage_->bytes, compiledByteOffset_ + cellOffset,
                              static_cast<float>(v[i]));
                else if (paramType_ == EffectParameterType::Int32 ||
                         paramType_ == EffectParameterType::Bool)
                    WriteCell(compiledStorage_->bytes, compiledByteOffset_ + cellOffset, v[i]);
                else
                    throw System::InvalidCastException();
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
            SetCompiledScalarValue(value, 0, false);
            return;
        }
        floatData_ = {value};
    }
    void EffectParameter::SetValue(const std::vector<float>& v)
    {
        RequireNumericParameter("SetValue(float[])");
        if (stockParameter_ && !compiledStorage_ && paramClass_ == EffectParameterClass::Matrix)
        {
            // Refresh SetValue(float[]) IL_005b..00e4 reads and updates the old Matrix.
            Matrix m = GetValueMatrix();
            float* cells[] = {&m.M11,&m.M12,&m.M13,&m.M14, &m.M21,&m.M22,&m.M23,&m.M24,
                              &m.M31,&m.M32,&m.M33,&m.M34, &m.M41,&m.M42,&m.M43,&m.M44};
            const std::size_t available = std::min(v.size(),
                static_cast<std::size_t>(rowCount_ * columnCount_));
            for (std::size_t i = 0; i < available; ++i)
                *cells[(i / columnCount_) * 4 + i % columnCount_] = v[i];
            if (!v.empty()) SetValue(m);
            return;
        }
        if (compiledStorage_)
        {
            RequireCompiledNumericArrayClass();
            for (std::size_t i = 0; i < v.size(); ++i)
            {
                std::size_t cellOffset = 0;
                if (!TryGetCompiledFlatCellOffset(i,
                        paramClass_ == EffectParameterClass::Matrix ? MatrixRegisterCount() : columnCount_,
                        compiledByteSize_, cellOffset,
                        paramClass_ == EffectParameterClass::Matrix ? MatrixComponentCount() : 0))
                    break;
                if (paramType_ == EffectParameterType::Single)
                    WriteCell(compiledStorage_->bytes, compiledByteOffset_ + cellOffset, v[i]);
                else if (paramType_ == EffectParameterType::Int32 ||
                         paramType_ == EffectParameterType::Bool)
                    WriteCell(compiledStorage_->bytes, compiledByteOffset_ + cellOffset,
                              TruncateToInt32(v[i]));
                else
                    throw System::InvalidCastException();
            }
            compiledStorage_->dirty = true;
            return;
        }
        floatData_ = v;
    }
    void EffectParameter::SetValue(const std::string& v)
    {
        if (stockParameter_) RequireStringParameter("String accessor");
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
            RequireTextureValueUsable(v);
            RequireTextureSetterParameter();
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
        // XNA typed setters validate reflected shape before writing, including stock effects.
        // SetValue(Matrix) IL_0000..0014; SetValue(Vector3) IL_0000..0030.
        // Standalone CNAEXT parameters retain their separate unreflected storage contract.
        RequireNumericParameter("SetValue(Matrix)");
        if (compiledStorage_ || stockParameter_)
            RequireScalarValueShape(EffectParameterClass::Matrix);
        if (compiledStorage_)
        {
            WriteCompiledMatrix(compiledStorage_->bytes, compiledByteOffset_, compiledByteSize_,
                                MatrixRegisterCount(), MatrixComponentCount(), m, false);
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
        // XNA SetValue(Matrix[]) requires a reflected array (IL_0011..001c,
        // Matrix variants IL_0022..002d), including stock parameters.
        if (stockParameter_)
            RequireArrayValueShape(EffectParameterClass::Matrix, v.size(), true);
        if (compiledStorage_)
        {
            RequireArrayValueShape(EffectParameterClass::Matrix, v.size(), true);
            const std::size_t stride = static_cast<std::size_t>(std::max(MatrixRegisterCount(), 1)) * 16;
            for (std::size_t i = 0; i < v.size(); ++i)
            {
                const std::size_t offset = i * stride;
                if (offset + stride > compiledByteSize_) break;
                WriteCompiledMatrix(compiledStorage_->bytes, compiledByteOffset_ + offset,
                                    stride, MatrixRegisterCount(), MatrixComponentCount(), v[i], false);
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
        if (compiledStorage_ || stockParameter_)
            RequireScalarValueShape(EffectParameterClass::Matrix);
        if (compiledStorage_)
        {
            WriteCompiledMatrix(compiledStorage_->bytes, compiledByteOffset_, compiledByteSize_,
                                MatrixRegisterCount(), MatrixComponentCount(), m, true);
            compiledStorage_->dirty = true;
            return;
        }
        SetValue(Matrix::Transpose(m));
    }
    void EffectParameter::SetValueTranspose(const std::vector<Matrix>& v)
    {
        RequireNumericParameter("SetValueTranspose(Matrix[])");
        // XNA SetValueTranspose(Matrix[]) requires a reflected array (IL_0011..001c,
        // Matrix variants IL_0022..002d), including stock parameters.
        if (stockParameter_)
            RequireArrayValueShape(EffectParameterClass::Matrix, v.size(), true);
        if (compiledStorage_)
        {
            RequireArrayValueShape(EffectParameterClass::Matrix, v.size(), true);
            const std::size_t stride = static_cast<std::size_t>(std::max(MatrixRegisterCount(), 1)) * 16;
            for (std::size_t i = 0; i < v.size(); ++i)
            {
                const std::size_t offset = i * stride;
                if (offset + stride > compiledByteSize_) break;
                WriteCompiledMatrix(compiledStorage_->bytes, compiledByteOffset_ + offset,
                                    stride, MatrixRegisterCount(), MatrixComponentCount(), v[i], true);
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
        if (compiledStorage_ || stockParameter_)
            RequireScalarValueShape(EffectParameterClass::Vector, 4);
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
        // XNA SetValue(Quaternion[]) requires a reflected array (IL_0011..001c,
        // Matrix variants IL_0022..002d), including stock parameters.
        if (stockParameter_)
            RequireArrayValueShape(EffectParameterClass::Vector, v.size(), false);
        if (compiledStorage_)
        {
            RequireArrayValueShape(EffectParameterClass::Vector, v.size(), false);
            // XNA EffectParameter.SetValue(Quaternion[]) IL_004c..0054 passes a flat
            // component count to SetFloatArray; destination width controls register packing.
            std::vector<float> components;
            components.reserve(v.size() * 4);
            for (const auto& value : v)
                components.insert(components.end(), {value.X, value.Y, value.Z, value.W});
            SetValue(components);
            return;
        }
        floatData_.clear();
        for (const auto& q : v) { floatData_.push_back(q.X); floatData_.push_back(q.Y); floatData_.push_back(q.Z); floatData_.push_back(q.W); }
    }
    void EffectParameter::SetValue(const Vector2& v)
    {
        RequireNumericParameter("SetValue(Vector2)");
        if (compiledStorage_ || stockParameter_)
            RequireScalarValueShape(EffectParameterClass::Vector, 2);
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
        // XNA SetValue(Vector2[]) requires a reflected array (IL_0011..001c,
        // Matrix variants IL_0022..002d), including stock parameters.
        if (stockParameter_)
            RequireArrayValueShape(EffectParameterClass::Vector, v.size(), false);
        if (compiledStorage_)
        {
            RequireArrayValueShape(EffectParameterClass::Vector, v.size(), false);
            // XNA EffectParameter.SetValue(Vector2[]) IL_004c..0054 passes a flat
            // component count to SetFloatArray; destination width controls register packing.
            std::vector<float> components;
            components.reserve(v.size() * 2);
            for (const auto& value : v)
                components.insert(components.end(), {value.X, value.Y});
            SetValue(components);
            return;
        }
        floatData_.clear();
        for (const auto& e : v) { floatData_.push_back(e.X); floatData_.push_back(e.Y); }
    }
    void EffectParameter::SetValue(const Vector3& v)
    {
        RequireNumericParameter("SetValue(Vector3)");
        if (compiledStorage_ || stockParameter_)
            RequireScalarValueShape(EffectParameterClass::Vector, 3);
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
        // XNA SetValue(Vector3[]) requires a reflected array (IL_0011..001c,
        // Matrix variants IL_0022..002d), including stock parameters.
        if (stockParameter_)
            RequireArrayValueShape(EffectParameterClass::Vector, v.size(), false);
        if (compiledStorage_)
        {
            RequireArrayValueShape(EffectParameterClass::Vector, v.size(), false);
            // XNA EffectParameter.SetValue(Vector3[]) IL_004c..0054 passes a flat
            // component count to SetFloatArray; destination width controls register packing.
            std::vector<float> components;
            components.reserve(v.size() * 3);
            for (const auto& value : v)
                components.insert(components.end(), {value.X, value.Y, value.Z});
            SetValue(components);
            return;
        }
        floatData_.clear();
        for (const auto& e : v) { floatData_.push_back(e.X); floatData_.push_back(e.Y); floatData_.push_back(e.Z); }
    }
    void EffectParameter::SetValue(const Vector4& v)
    {
        RequireNumericParameter("SetValue(Vector4)");
        if (compiledStorage_ || stockParameter_)
            RequireScalarValueShape(EffectParameterClass::Vector, 4);
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
        // XNA SetValue(Vector4[]) requires a reflected array (IL_0011..001c,
        // Matrix variants IL_0022..002d), including stock parameters.
        if (stockParameter_)
            RequireArrayValueShape(EffectParameterClass::Vector, v.size(), false);
        if (compiledStorage_)
        {
            RequireArrayValueShape(EffectParameterClass::Vector, v.size(), false);
            // XNA EffectParameter.SetValue(Vector4[]) IL_004c..0054 passes a flat
            // component count to SetFloatArray; destination width controls register packing.
            std::vector<float> components;
            components.reserve(v.size() * 4);
            for (const auto& value : v)
                components.insert(components.end(), {value.X, value.Y, value.Z, value.W});
            SetValue(components);
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
