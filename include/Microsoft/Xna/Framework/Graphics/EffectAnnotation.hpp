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
    class EffectAnnotation
    {
    public:
        EffectAnnotation(std::string name, std::string semantic,
                         int rowCount, int columnCount,
                         EffectParameterClass paramClass,
                         EffectParameterType paramType,
                         std::vector<float> data = {});

        [[nodiscard]] const std::string& getNameProperty() const;
        [[nodiscard]] const std::string& getSemanticProperty() const;
        [[nodiscard]] int getRowCountProperty() const;
        [[nodiscard]] int getColumnCountProperty() const;
        [[nodiscard]] EffectParameterClass getParameterClassProperty() const;
        [[nodiscard]] EffectParameterType getParameterTypeProperty() const;

        [[nodiscard]] bool GetValueBoolean() const;
        [[nodiscard]] int GetValueInt32() const;
        [[nodiscard]] float GetValueSingle() const;
        [[nodiscard]] std::string GetValueString() const;
        [[nodiscard]] Vector2 GetValueVector2() const;
        [[nodiscard]] Vector3 GetValueVector3() const;
        [[nodiscard]] Vector4 GetValueVector4() const;
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
