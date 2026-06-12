// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/EffectAnnotation.hpp"
#include <stdexcept>

namespace Microsoft::Xna::Framework::Graphics
{
    EffectAnnotation::EffectAnnotation(std::string name, std::string semantic,
                                       int rowCount, int columnCount,
                                       EffectParameterClass paramClass,
                                       EffectParameterType paramType,
                                       std::vector<float> data)
        : name_(std::move(name)), semantic_(std::move(semantic))
        , rowCount_(rowCount), columnCount_(columnCount)
        , paramClass_(paramClass), paramType_(paramType)
        , data_(std::move(data))
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
        if (data_.empty()) return false;
        return *reinterpret_cast<const int*>(data_.data()) != 0;
    }

    int EffectAnnotation::GetValueInt32() const
    {
        if (data_.empty()) return 0;
        return *reinterpret_cast<const int*>(data_.data());
    }

    float EffectAnnotation::GetValueSingle() const
    {
        return data_.empty() ? 0.0f : data_[0];
    }

    std::string EffectAnnotation::GetValueString() const { return cachedString_; }

    Vector2 EffectAnnotation::GetValueVector2() const
    {
        if (data_.size() < 2) return {};
        return {data_[0], data_[1]};
    }

    Vector3 EffectAnnotation::GetValueVector3() const
    {
        if (data_.size() < 3) return {};
        return {data_[0], data_[1], data_[2]};
    }

    Vector4 EffectAnnotation::GetValueVector4() const
    {
        if (data_.size() < 4) return {};
        return {data_[0], data_[1], data_[2], data_[3]};
    }

    Matrix EffectAnnotation::GetValueMatrix() const
    {
        if (data_.size() < 16) return Matrix::getIdentityProperty();
        return Matrix(
            data_[0],  data_[4],  data_[8],  data_[12],
            data_[1],  data_[5],  data_[9],  data_[13],
            data_[2],  data_[6],  data_[10], data_[14],
            data_[3],  data_[7],  data_[11], data_[15]
        );
    }
}
