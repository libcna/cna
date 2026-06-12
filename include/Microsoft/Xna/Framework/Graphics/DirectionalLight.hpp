// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Represents a directional light source used by BasicEffect.
    class DirectionalLight
    {
    public:
        DirectionalLight();

        [[nodiscard]] Vector3 getDiffuseColorProperty() const;
        void setDiffuseColorProperty(const Vector3& value);

        [[nodiscard]] Vector3 getDirectionProperty() const;
        void setDirectionProperty(const Vector3& value);

        [[nodiscard]] Vector3 getSpecularColorProperty() const;
        void setSpecularColorProperty(const Vector3& value);

        [[nodiscard]] bool getEnabledProperty() const;
        void setEnabledProperty(bool value);

    private:
        Vector3 diffuseColor_;
        Vector3 direction_;
        Vector3 specularColor_;
        bool enabled_ = false;
    };
}
