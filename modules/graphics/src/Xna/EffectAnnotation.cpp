// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/EffectAnnotation.hpp"
#include "System/InvalidCastException.hpp"
#include <bit>
#include <limits>

namespace Microsoft::Xna::Framework::Graphics
{
    EffectAnnotation::EffectAnnotation(std::string name, std::string semantic,
                                       int rowCount, int columnCount,
                                       EffectParameterClass paramClass,
                                       EffectParameterType paramType,
                                       std::vector<float> data,
                                       std::string cachedString)
        : name_(std::move(name)), semantic_(std::move(semantic))
        , rowCount_(rowCount), columnCount_(columnCount)
        , paramClass_(paramClass), paramType_(paramType)
        , data_(std::move(data))
        , cachedString_(std::move(cachedString))
    {
    }

    const std::string& EffectAnnotation::getNameProperty() const { return name_; }
    const std::string& EffectAnnotation::getSemanticProperty() const { return semantic_; }
    int EffectAnnotation::getRowCountProperty() const { return rowCount_; }
    int EffectAnnotation::getColumnCountProperty() const { return columnCount_; }
    EffectParameterClass EffectAnnotation::getParameterClassProperty() const { return paramClass_; }
    EffectParameterType EffectAnnotation::getParameterTypeProperty() const { return paramType_; }

    bool EffectAnnotation::GetValueBoolean() const
    {
        // XNA EffectAnnotation.GetValue* IL_000e..0013 delegates to EffectParameter;
        // its scalar getters IL_0000..001e reject non-Scalar annotations.
        if (paramClass_ != EffectParameterClass::Scalar)
            throw System::InvalidCastException();
        if (data_.empty()) return false;
        // XNA EffectAnnotation.GetValueBoolean IL_0013 delegates to the numeric getter,
        // EffectParameter.GetValueBoolean IL_0035 calls native GetBool (not a bit test).
        return paramType_ == EffectParameterType::Single
            ? data_[0] != 0.0f : std::bit_cast<int>(data_[0]) != 0;
    }

    int EffectAnnotation::GetValueInt32() const
    {
        // XNA EffectAnnotation.GetValue* IL_000e..0013 delegates to EffectParameter;
        // its scalar getters IL_0000..001e reject non-Scalar annotations.
        if (paramClass_ != EffectParameterClass::Scalar)
            throw System::InvalidCastException();
        if (data_.empty()) return 0;
        // XNA EffectAnnotation.GetValueInt32 IL_0013 delegates to GetInt via
        // EffectParameter.GetValueInt32 IL_0035. Single converts with truncation toward zero.
        if (paramType_ == EffectParameterType::Single)
        {
            const float value = data_[0];
            if (!(value >= -2147483648.0f && value < 2147483648.0f))
                return std::numeric_limits<int>::min();
            return static_cast<int>(value);
        }
        return std::bit_cast<int>(data_[0]);
    }

    float EffectAnnotation::GetValueSingle() const
    {
        // XNA EffectAnnotation.GetValue* IL_000e..0013 delegates to EffectParameter;
        // its scalar getters IL_0000..001e reject non-Scalar annotations.
        if (paramClass_ != EffectParameterClass::Scalar)
            throw System::InvalidCastException();
        // XNA EffectAnnotation.GetValueSingle IL_0013 delegates to GetFloat via
        // EffectParameter.GetValueSingle IL_0035; integer storage is numerically converted.
        if (data_.empty()) return 0.0f;
        return paramType_ == EffectParameterType::Single
            ? data_[0] : static_cast<float>(std::bit_cast<int>(data_[0]));
    }

    std::string EffectAnnotation::GetValueString() const
    {
        // XNA EffectAnnotation.GetValueString IL_000e..0013 delegates to
        // EffectParameter.GetValueString IL_0000..0012 (String type required).
        if (paramType_ != EffectParameterType::String)
            throw System::InvalidCastException();
        return cachedString_;
    }

    Vector2 EffectAnnotation::GetValueVector2() const
    {
        // XNA EffectParameter.GetValueVector2: Scalar broadcasts; otherwise width must match.
        // EffectAnnotation.GetValueVector2 IL_000e..0013 uses that same getter.
        if (paramClass_ == EffectParameterClass::Scalar)
            return Vector2(GetValueSingle());
        if (paramClass_ != EffectParameterClass::Vector || rowCount_ != 1 || columnCount_ != 2)
            throw System::InvalidCastException();
        if (data_.size() < 2) return {};
        return {data_[0], data_[1]};
    }

    Vector3 EffectAnnotation::GetValueVector3() const
    {
        // XNA EffectParameter.GetValueVector3: Scalar broadcasts; otherwise width must match.
        // EffectAnnotation.GetValueVector3 IL_000e..0013 uses that same getter.
        if (paramClass_ == EffectParameterClass::Scalar)
            return Vector3(GetValueSingle());
        if (paramClass_ != EffectParameterClass::Vector || rowCount_ != 1 || columnCount_ != 3)
            throw System::InvalidCastException();
        if (data_.size() < 3) return {};
        return {data_[0], data_[1], data_[2]};
    }

    Vector4 EffectAnnotation::GetValueVector4() const
    {
        // XNA EffectParameter.GetValueVector4: Scalar broadcasts; otherwise width must match.
        // EffectAnnotation.GetValueVector4 IL_000e..0013 uses that same getter.
        if (paramClass_ == EffectParameterClass::Scalar)
            return Vector4(GetValueSingle());
        if (paramClass_ != EffectParameterClass::Vector || rowCount_ != 1 || columnCount_ != 4)
            throw System::InvalidCastException();
        if (data_.size() < 4) return {};
        return {data_[0], data_[1], data_[2], data_[3]};
    }

    Matrix EffectAnnotation::GetValueMatrix() const
    {
        // XNA EffectAnnotation.GetValueMatrix IL_000e..0013 delegates to
        // EffectParameter.GetValueMatrix IL_0019..00dd: broadcast Scalar, else require Matrix.
        if (paramClass_ == EffectParameterClass::Scalar)
        {
            const float value = GetValueSingle();
            return Matrix(value,value,value,value, value,value,value,value,
                          value,value,value,value, value,value,value,value);
        }
        if (paramClass_ != EffectParameterClass::Matrix)
            throw System::InvalidCastException();
        if (data_.size() < 16) return Matrix::getIdentityProperty();
        return Matrix(
            data_[0],  data_[4],  data_[8],  data_[12],
            data_[1],  data_[5],  data_[9],  data_[13],
            data_[2],  data_[6],  data_[10], data_[14],
            data_[3],  data_[7],  data_[11], data_[15]
        );
    }
}
