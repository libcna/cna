// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/PbrMaterialExtensions.hpp"

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <algorithm>
#include <functional>
#include <sstream>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Vector3;

    namespace {

        void Combine(std::size_t& seed, const std::size_t value)
        {
            seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        }

        std::size_t HashFloat(const float value)
        {
            return std::hash<float>{}(value == 0.0f ? 0.0f : value);   // -0 and +0 hash alike
        }

        std::string Trimmed(const float value)
        {
            std::ostringstream stream;
            stream << value;
            return stream.str();
        }

    } // namespace

    PbrMaterialExtensions::PbrMaterialExtensions() = default;

    float PbrMaterialExtensions::getClearcoatFactor() const { return clearcoatFactor_; }
    void  PbrMaterialExtensions::setClearcoatFactor(const float value)
    {
        clearcoatFactor_ = std::clamp(value, 0.0f, 1.0f);
    }

    float PbrMaterialExtensions::getClearcoatRoughness() const { return clearcoatRoughness_; }
    void  PbrMaterialExtensions::setClearcoatRoughness(const float value)
    {
        clearcoatRoughness_ = std::clamp(value, 0.0f, 1.0f);
    }

    Microsoft::Xna::Framework::Graphics::Texture2D*
    PbrMaterialExtensions::getClearcoatTexture() const { return clearcoatTexture_; }
    void PbrMaterialExtensions::setClearcoatTexture(Tex2D* texture) { clearcoatTexture_ = texture; }

    Microsoft::Xna::Framework::Graphics::Texture2D*
    PbrMaterialExtensions::getClearcoatRoughnessTexture() const
    {
        return clearcoatRoughnessTexture_;
    }
    void PbrMaterialExtensions::setClearcoatRoughnessTexture(Tex2D* texture)
    {
        clearcoatRoughnessTexture_ = texture;
    }

    Microsoft::Xna::Framework::Graphics::Texture2D*
    PbrMaterialExtensions::getClearcoatNormalTexture() const { return clearcoatNormalTexture_; }
    void PbrMaterialExtensions::setClearcoatNormalTexture(Tex2D* texture)
    {
        clearcoatNormalTexture_ = texture;
    }

    float PbrMaterialExtensions::getClearcoatNormalScale() const { return clearcoatNormalScale_; }
    void  PbrMaterialExtensions::setClearcoatNormalScale(const float value)
    {
        if (value >= 0.0f) clearcoatNormalScale_ = value;
    }

    float PbrMaterialExtensions::getIridescenceFactor() const { return iridescenceFactor_; }
    void  PbrMaterialExtensions::setIridescenceFactor(const float value)
    {
        iridescenceFactor_ = std::clamp(value, 0.0f, 1.0f);
    }

    float PbrMaterialExtensions::getIridescenceIor() const { return iridescenceIor_; }
    void  PbrMaterialExtensions::setIridescenceIor(const float value)
    {
        if (value >= 1.0f) iridescenceIor_ = value;
    }

    float PbrMaterialExtensions::getIridescenceThicknessMinimum() const
    {
        return iridescenceThicknessMinimum_;
    }
    void PbrMaterialExtensions::setIridescenceThicknessMinimum(const float value)
    {
        if (value >= 0.0f) iridescenceThicknessMinimum_ = value;
    }

    float PbrMaterialExtensions::getIridescenceThicknessMaximum() const
    {
        return iridescenceThicknessMaximum_;
    }
    void PbrMaterialExtensions::setIridescenceThicknessMaximum(const float value)
    {
        if (value >= 0.0f) iridescenceThicknessMaximum_ = value;
    }

    Microsoft::Xna::Framework::Graphics::Texture2D*
    PbrMaterialExtensions::getIridescenceTexture() const { return iridescenceTexture_; }
    void PbrMaterialExtensions::setIridescenceTexture(Tex2D* texture)
    {
        iridescenceTexture_ = texture;
    }

    Microsoft::Xna::Framework::Graphics::Texture2D*
    PbrMaterialExtensions::getIridescenceThicknessTexture() const
    {
        return iridescenceThicknessTexture_;
    }
    void PbrMaterialExtensions::setIridescenceThicknessTexture(Tex2D* texture)
    {
        iridescenceThicknessTexture_ = texture;
    }

    float PbrMaterialExtensions::getTransmissionFactor() const { return transmissionFactor_; }
    void  PbrMaterialExtensions::setTransmissionFactor(const float value)
    {
        transmissionFactor_ = std::clamp(value, 0.0f, 1.0f);
    }

    Microsoft::Xna::Framework::Graphics::Texture2D*
    PbrMaterialExtensions::getTransmissionTexture() const { return transmissionTexture_; }
    void PbrMaterialExtensions::setTransmissionTexture(Tex2D* texture)
    {
        transmissionTexture_ = texture;
    }

    float PbrMaterialExtensions::getThicknessFactor() const { return thicknessFactor_; }
    void  PbrMaterialExtensions::setThicknessFactor(const float value)
    {
        if (value >= 0.0f) thicknessFactor_ = value;
    }

    Microsoft::Xna::Framework::Graphics::Texture2D*
    PbrMaterialExtensions::getThicknessTexture() const { return thicknessTexture_; }
    void PbrMaterialExtensions::setThicknessTexture(Tex2D* texture)
    {
        thicknessTexture_ = texture;
    }

    float PbrMaterialExtensions::getAttenuationDistance() const { return attenuationDistance_; }
    void  PbrMaterialExtensions::setAttenuationDistance(const float value)
    {
        attenuationDistance_ = value > 0.0f ? value : 0.0f;
    }

    Vector3 PbrMaterialExtensions::getAttenuationColor() const { return attenuationColor_; }
    void    PbrMaterialExtensions::setAttenuationColor(const Vector3& value)
    {
        attenuationColor_ = Vector3(std::clamp(value.X, 0.0f, 1.0f),
                                    std::clamp(value.Y, 0.0f, 1.0f),
                                    std::clamp(value.Z, 0.0f, 1.0f));
    }

    Vector3 PbrMaterialExtensions::getSheenColorFactor() const { return sheenColorFactor_; }
    void    PbrMaterialExtensions::setSheenColorFactor(const Vector3& value)
    {
        sheenColorFactor_ = Vector3(std::clamp(value.X, 0.0f, 1.0f),
                                    std::clamp(value.Y, 0.0f, 1.0f),
                                    std::clamp(value.Z, 0.0f, 1.0f));
    }

    float PbrMaterialExtensions::getSheenRoughness() const { return sheenRoughness_; }
    void  PbrMaterialExtensions::setSheenRoughness(const float value)
    {
        sheenRoughness_ = std::clamp(value, 0.0f, 1.0f);
    }

    Microsoft::Xna::Framework::Graphics::Texture2D*
    PbrMaterialExtensions::getSheenColorTexture() const { return sheenColorTexture_; }
    void PbrMaterialExtensions::setSheenColorTexture(Tex2D* texture)
    {
        sheenColorTexture_ = texture;
    }

    Microsoft::Xna::Framework::Graphics::Texture2D*
    PbrMaterialExtensions::getSheenRoughnessTexture() const { return sheenRoughnessTexture_; }
    void PbrMaterialExtensions::setSheenRoughnessTexture(Tex2D* texture)
    {
        sheenRoughnessTexture_ = texture;
    }

    bool PbrMaterialExtensions::operator==(const PbrMaterialExtensions& other) const
    {
        return iridescenceFactor_ == other.iridescenceFactor_ &&
               iridescenceIor_ == other.iridescenceIor_ &&
               iridescenceThicknessMinimum_ == other.iridescenceThicknessMinimum_ &&
               iridescenceThicknessMaximum_ == other.iridescenceThicknessMaximum_ &&
               iridescenceTexture_ == other.iridescenceTexture_ &&
               iridescenceThicknessTexture_ == other.iridescenceThicknessTexture_ &&
               transmissionFactor_ == other.transmissionFactor_ &&
               thicknessFactor_ == other.thicknessFactor_ &&
               attenuationDistance_ == other.attenuationDistance_ &&
               attenuationColor_.X == other.attenuationColor_.X &&
               attenuationColor_.Y == other.attenuationColor_.Y &&
               attenuationColor_.Z == other.attenuationColor_.Z &&
               transmissionTexture_ == other.transmissionTexture_ &&
               thicknessTexture_ == other.thicknessTexture_ &&
               sheenColorFactor_.X == other.sheenColorFactor_.X &&
               sheenColorFactor_.Y == other.sheenColorFactor_.Y &&
               sheenColorFactor_.Z == other.sheenColorFactor_.Z &&
               sheenRoughness_ == other.sheenRoughness_ &&
               sheenColorTexture_ == other.sheenColorTexture_ &&
               sheenRoughnessTexture_ == other.sheenRoughnessTexture_ &&
               clearcoatFactor_ == other.clearcoatFactor_ &&
               clearcoatRoughness_ == other.clearcoatRoughness_ &&
               clearcoatNormalScale_ == other.clearcoatNormalScale_ &&
               clearcoatTexture_ == other.clearcoatTexture_ &&
               clearcoatRoughnessTexture_ == other.clearcoatRoughnessTexture_ &&
               clearcoatNormalTexture_ == other.clearcoatNormalTexture_;
    }

    bool PbrMaterialExtensions::operator!=(const PbrMaterialExtensions& other) const
    {
        return !(*this == other);
    }

    std::size_t PbrMaterialExtensions::GetHashCode() const
    {
        std::size_t seed = 0;
        Combine(seed, HashFloat(iridescenceFactor_));
        Combine(seed, HashFloat(iridescenceIor_));
        Combine(seed, HashFloat(iridescenceThicknessMinimum_));
        Combine(seed, HashFloat(iridescenceThicknessMaximum_));
        Combine(seed, std::hash<const void*>{}(iridescenceTexture_));
        Combine(seed, std::hash<const void*>{}(iridescenceThicknessTexture_));
        Combine(seed, HashFloat(transmissionFactor_));
        Combine(seed, HashFloat(thicknessFactor_));
        Combine(seed, HashFloat(attenuationDistance_));
        Combine(seed, HashFloat(attenuationColor_.X));
        Combine(seed, HashFloat(attenuationColor_.Y));
        Combine(seed, HashFloat(attenuationColor_.Z));
        Combine(seed, std::hash<const void*>{}(transmissionTexture_));
        Combine(seed, std::hash<const void*>{}(thicknessTexture_));
        Combine(seed, HashFloat(sheenColorFactor_.X));
        Combine(seed, HashFloat(sheenColorFactor_.Y));
        Combine(seed, HashFloat(sheenColorFactor_.Z));
        Combine(seed, HashFloat(sheenRoughness_));
        Combine(seed, std::hash<const void*>{}(sheenColorTexture_));
        Combine(seed, std::hash<const void*>{}(sheenRoughnessTexture_));
        Combine(seed, HashFloat(clearcoatFactor_));
        Combine(seed, HashFloat(clearcoatRoughness_));
        Combine(seed, HashFloat(clearcoatNormalScale_));
        Combine(seed, std::hash<const void*>{}(clearcoatTexture_));
        Combine(seed, std::hash<const void*>{}(clearcoatRoughnessTexture_));
        Combine(seed, std::hash<const void*>{}(clearcoatNormalTexture_));
        return seed;
    }

    bool PbrMaterialExtensions::isSheenEnabled() const
    {
        return sheenColorFactor_.X > 0.0f || sheenColorFactor_.Y > 0.0f ||
               sheenColorFactor_.Z > 0.0f;
    }

    bool PbrMaterialExtensions::isIridescenceEnabled() const { return iridescenceFactor_ > 0.0f; }

    bool PbrMaterialExtensions::isTransmissionEnabled() const
    {
        return transmissionFactor_ > 0.0f;
    }

    bool PbrMaterialExtensions::isNeutral() const
    {
        return clearcoatFactor_ <= 0.0f && !isSheenEnabled() && !isTransmissionEnabled() &&
               !isIridescenceEnabled();
    }

    std::string PbrMaterialExtensions::ToString() const
    {
        std::string text = "{";
        if (isIridescenceEnabled())
        {
            int maps = 0;
            if (iridescenceTexture_ != nullptr) ++maps;
            if (iridescenceThicknessTexture_ != nullptr) ++maps;
            text += "Iridescence:{Factor:" + Trimmed(iridescenceFactor_) + " Ior:" +
                    Trimmed(iridescenceIor_) + " Thickness:" +
                    Trimmed(iridescenceThicknessMinimum_) + ".." +
                    Trimmed(iridescenceThicknessMaximum_) + " Textures:" +
                    std::to_string(maps) + "}";
        }
        if (isTransmissionEnabled())
        {
            if (text.size() > 1) text += " ";
            int maps = 0;
            if (transmissionTexture_ != nullptr) ++maps;
            if (thicknessTexture_ != nullptr) ++maps;
            text += "Transmission:{Factor:" + Trimmed(transmissionFactor_) +
                    " Thickness:" + Trimmed(thicknessFactor_) +
                    " AttenuationDistance:" + Trimmed(attenuationDistance_) +
                    " Textures:" + std::to_string(maps) + "}";
        }
        if (isSheenEnabled())
        {
            if (text.size() > 1) text += " ";
            int maps = 0;
            if (sheenColorTexture_ != nullptr) ++maps;
            if (sheenRoughnessTexture_ != nullptr) ++maps;
            text += "Sheen:{Color:{X:" + Trimmed(sheenColorFactor_.X) + " Y:" +
                    Trimmed(sheenColorFactor_.Y) + " Z:" + Trimmed(sheenColorFactor_.Z) +
                    "} Roughness:" + Trimmed(sheenRoughness_) + " Textures:" +
                    std::to_string(maps) + "}";
        }
        if (clearcoatFactor_ > 0.0f)
        {
            if (text.size() > 1) text += " ";
            int maps = 0;
            if (clearcoatTexture_ != nullptr) ++maps;
            if (clearcoatRoughnessTexture_ != nullptr) ++maps;
            if (clearcoatNormalTexture_ != nullptr) ++maps;
            text += "Clearcoat:{Factor:" + Trimmed(clearcoatFactor_) +
                    " Roughness:" + Trimmed(clearcoatRoughness_) +
                    " Textures:" + std::to_string(maps) + "}";
        }
        text += "}";
        return text;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
