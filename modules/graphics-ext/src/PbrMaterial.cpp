// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/PbrMaterial.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#ifdef CNA_CNAEXT

#include <functional>
#include <sstream>

using Tex2D = Microsoft::Xna::Framework::Graphics::Texture2D;
using Color = Microsoft::Xna::Framework::Color;
using Vector3 = Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::AlphaModeEXT;
using Microsoft::Xna::Framework::Graphics::TextureTransformEXT;

namespace CNA::Graphics {

    namespace {
        /// Slot enum to array index, with the out-of-range case folded onto base colour rather
        /// than left to index past the end -- the enum is the only legal input, and a caller that
        /// casts an integer into it gets a defined answer instead of memory it does not own.
        [[nodiscard]] std::size_t IndexOf(const PbrTextureSlot slot)
        {
            const auto index = static_cast<std::size_t>(slot);
            return index < static_cast<std::size_t>(kPbrTextureSlotCount) ? index : 0U;
        }

        void HashCombine(std::size_t& seed, const std::size_t value)
        {
            seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        }

        [[nodiscard]] const char* NameOf(const AlphaModeEXT mode)
        {
            switch (mode)
            {
            case AlphaModeEXT::Mask:  return "Mask";
            case AlphaModeEXT::Blend: return "Blend";
            case AlphaModeEXT::Opaque:
            default:                  return "Opaque";
            }
        }
    }

    PbrMaterial::PbrMaterial() = default;

    // ── Texture slots ────────────────────────────────────────────────────────

    Tex2D* PbrMaterial::getAlbedoTexture()             const { return albedoTexture_; }
    void   PbrMaterial::setAlbedoTexture(Tex2D* t)           { albedoTexture_ = t; }

    Tex2D* PbrMaterial::getNormalTexture()             const { return normalTexture_; }
    void   PbrMaterial::setNormalTexture(Tex2D* t)           { normalTexture_ = t; }

    Tex2D* PbrMaterial::getMetallicRoughnessTexture()  const { return metallicRoughnessTexture_; }
    void   PbrMaterial::setMetallicRoughnessTexture(Tex2D* t){ metallicRoughnessTexture_ = t; }

    Tex2D* PbrMaterial::getAmbientOcclusionTexture()   const { return ambientOcclusionTexture_; }
    void   PbrMaterial::setAmbientOcclusionTexture(Tex2D* t) { ambientOcclusionTexture_ = t; }

    Tex2D* PbrMaterial::getEmissiveTexture()           const { return emissiveTexture_; }
    void   PbrMaterial::setEmissiveTexture(Tex2D* t)         { emissiveTexture_ = t; }

    Tex2D* PbrMaterial::getSpecularTexture()           const { return specularTexture_; }
    void   PbrMaterial::setSpecularTexture(Tex2D* t)         { specularTexture_ = t; }

    Tex2D* PbrMaterial::getSpecularColorTexture()      const { return specularColorTexture_; }
    void   PbrMaterial::setSpecularColorTexture(Tex2D* t)    { specularColorTexture_ = t; }

    // ── Scalar / colour factors ──────────────────────────────────────────────

    Color  PbrMaterial::getAlbedoColor()               const { return albedoColor_; }
    void   PbrMaterial::setAlbedoColor(Color c)              { albedoColor_ = c; }

    float  PbrMaterial::getMetallicFactor()            const { return metallicFactor_; }
    void   PbrMaterial::setMetallicFactor(float v)           { metallicFactor_ = v; }

    float  PbrMaterial::getRoughnessFactor()           const { return roughnessFactor_; }
    void   PbrMaterial::setRoughnessFactor(float v)          { roughnessFactor_ = v; }

    Vector3 PbrMaterial::getEmissiveFactor()           const { return emissiveFactor_; }
    void    PbrMaterial::setEmissiveFactor(const Vector3& v) { emissiveFactor_ = v; }

    float  PbrMaterial::getNormalScale()               const { return normalScale_; }
    void   PbrMaterial::setNormalScale(float v)              { normalScale_ = v; }

    float  PbrMaterial::getOcclusionStrength()         const { return occlusionStrength_; }
    void   PbrMaterial::setOcclusionStrength(float v)        { occlusionStrength_ = v; }

    float  PbrMaterial::getIor()                       const { return ior_; }
    void   PbrMaterial::setIor(float v)                      { ior_ = v; }

    float  PbrMaterial::getSpecularFactor()            const { return specularFactor_; }
    void   PbrMaterial::setSpecularFactor(float v)           { specularFactor_ = v; }

    Vector3 PbrMaterial::getSpecularColorFactor()      const { return specularColorFactor_; }
    void    PbrMaterial::setSpecularColorFactor(const Vector3& v) { specularColorFactor_ = v; }

    // ── Coverage and sidedness ───────────────────────────────────────────────

    AlphaModeEXT PbrMaterial::getAlphaMode()           const { return alphaMode_; }
    void   PbrMaterial::setAlphaMode(AlphaModeEXT v)         { alphaMode_ = v; }

    float  PbrMaterial::getAlphaCutoff()               const { return alphaCutoff_; }
    void   PbrMaterial::setAlphaCutoff(float v)              { alphaCutoff_ = v; }

    bool   PbrMaterial::isDoubleSided()                const { return doubleSided_; }
    void   PbrMaterial::setDoubleSided(bool v)               { doubleSided_ = v; }

    // ── Per-slot texture settings ────────────────────────────────────────────

    int PbrMaterial::getTextureCoordinateSet(const PbrTextureSlot slot) const
    {
        return textureCoordinateSets_[IndexOf(slot)];
    }

    void PbrMaterial::setTextureCoordinateSet(const PbrTextureSlot slot, const int value)
    {
        textureCoordinateSets_[IndexOf(slot)] = value;
    }

    TextureTransformEXT PbrMaterial::getTextureTransform(const PbrTextureSlot slot) const
    {
        return textureTransforms_[IndexOf(slot)];
    }

    void PbrMaterial::setTextureTransform(const PbrTextureSlot slot,
                                          const TextureTransformEXT& value)
    {
        textureTransforms_[IndexOf(slot)] = value;
    }

    // ── Colour management ────────────────────────────────────────────────────

    bool PbrMaterial::isBaseColorTextureSrgb()         const { return baseColorTextureSrgb_; }
    void PbrMaterial::setBaseColorTextureSrgb(bool v)        { baseColorTextureSrgb_ = v; }

    bool PbrMaterial::isEmissiveTextureSrgb()          const { return emissiveTextureSrgb_; }
    void PbrMaterial::setEmissiveTextureSrgb(bool v)         { emissiveTextureSrgb_ = v; }

    bool PbrMaterial::isSpecularColorTextureSrgb()     const { return specularColorTextureSrgb_; }
    void PbrMaterial::setSpecularColorTextureSrgb(bool v)    { specularColorTextureSrgb_ = v; }

    bool PbrMaterial::isOutputEncodedToSrgb()          const { return outputEncodedToSrgb_; }
    void PbrMaterial::setOutputEncodedToSrgb(bool v)         { outputEncodedToSrgb_ = v; }

    // ── Value semantics ──────────────────────────────────────────────────────

    bool PbrMaterial::operator==(const PbrMaterial& other) const
    {
        return albedoTexture_            == other.albedoTexture_
            && normalTexture_            == other.normalTexture_
            && metallicRoughnessTexture_ == other.metallicRoughnessTexture_
            && ambientOcclusionTexture_  == other.ambientOcclusionTexture_
            && emissiveTexture_          == other.emissiveTexture_
            && specularTexture_          == other.specularTexture_
            && specularColorTexture_     == other.specularColorTexture_
            && albedoColor_              == other.albedoColor_
            && metallicFactor_           == other.metallicFactor_
            && roughnessFactor_          == other.roughnessFactor_
            && emissiveFactor_           == other.emissiveFactor_
            && normalScale_              == other.normalScale_
            && occlusionStrength_        == other.occlusionStrength_
            && ior_                      == other.ior_
            && specularFactor_           == other.specularFactor_
            && specularColorFactor_      == other.specularColorFactor_
            && alphaMode_                == other.alphaMode_
            && alphaCutoff_              == other.alphaCutoff_
            && doubleSided_              == other.doubleSided_
            && textureCoordinateSets_    == other.textureCoordinateSets_
            && textureTransforms_        == other.textureTransforms_
            && baseColorTextureSrgb_     == other.baseColorTextureSrgb_
            && emissiveTextureSrgb_      == other.emissiveTextureSrgb_
            && specularColorTextureSrgb_ == other.specularColorTextureSrgb_
            && outputEncodedToSrgb_      == other.outputEncodedToSrgb_;
    }

    bool PbrMaterial::operator!=(const PbrMaterial& other) const { return !(*this == other); }

    std::size_t PbrMaterial::GetHashCode() const
    {
        std::size_t seed = 0;
        const auto hashPointer = [&seed](const Tex2D* texture) {
            HashCombine(seed, std::hash<const void*>{}(texture));
        };
        const auto hashFloat = [&seed](const float value) {
            HashCombine(seed, std::hash<float>{}(value));
        };

        hashPointer(albedoTexture_);
        hashPointer(normalTexture_);
        hashPointer(metallicRoughnessTexture_);
        hashPointer(ambientOcclusionTexture_);
        hashPointer(emissiveTexture_);
        hashPointer(specularTexture_);
        hashPointer(specularColorTexture_);
        HashCombine(seed, std::hash<std::uint32_t>{}(albedoColor_.getPackedValueProperty()));
        hashFloat(metallicFactor_);
        hashFloat(roughnessFactor_);
        hashFloat(emissiveFactor_.X);
        hashFloat(emissiveFactor_.Y);
        hashFloat(emissiveFactor_.Z);
        hashFloat(normalScale_);
        hashFloat(occlusionStrength_);
        hashFloat(ior_);
        hashFloat(specularFactor_);
        hashFloat(specularColorFactor_.X);
        hashFloat(specularColorFactor_.Y);
        hashFloat(specularColorFactor_.Z);
        HashCombine(seed, std::hash<int>{}(static_cast<int>(alphaMode_)));
        hashFloat(alphaCutoff_);
        HashCombine(seed, std::hash<bool>{}(doubleSided_));
        for (const int set : textureCoordinateSets_)
            HashCombine(seed, std::hash<int>{}(set));
        for (const TextureTransformEXT& transform : textureTransforms_)
        {
            hashFloat(transform.Offset.X);
            hashFloat(transform.Offset.Y);
            hashFloat(transform.Scale.X);
            hashFloat(transform.Scale.Y);
            hashFloat(transform.Rotation);
        }
        HashCombine(seed, std::hash<bool>{}(baseColorTextureSrgb_));
        HashCombine(seed, std::hash<bool>{}(emissiveTextureSrgb_));
        HashCombine(seed, std::hash<bool>{}(specularColorTextureSrgb_));
        HashCombine(seed, std::hash<bool>{}(outputEncodedToSrgb_));
        return seed;
    }

    std::string PbrMaterial::ToString() const
    {
        int boundTextures = 0;
        for (const Tex2D* texture : {albedoTexture_, normalTexture_, metallicRoughnessTexture_,
                                     ambientOcclusionTexture_, emissiveTexture_, specularTexture_,
                                     specularColorTexture_})
            if (texture != nullptr) ++boundTextures;

        std::ostringstream out;
        out << "{Albedo:" << albedoColor_.ToString()
            << " Metallic:" << metallicFactor_
            << " Roughness:" << roughnessFactor_
            << " Emissive:" << emissiveFactor_.ToString()
            << " AlphaMode:" << NameOf(alphaMode_)
            << " DoubleSided:" << (doubleSided_ ? "True" : "False")
            << " Textures:" << boundTextures << "}";
        return out.str();
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
