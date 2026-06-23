// SPDX-License-Identifier: MS-PL
#include "CNA/NOXNA/PbrMaterial.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#ifdef CNA_NOXNA

using Tex2D = Microsoft::Xna::Framework::Graphics::Texture2D;
using Color = Microsoft::Xna::Framework::Color;

namespace CNA {

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

    // ── Scalar / colour factors ──────────────────────────────────────────────

    Color  PbrMaterial::getAlbedoColor()               const { return albedoColor_; }
    void   PbrMaterial::setAlbedoColor(Color c)              { albedoColor_ = c; }

    float  PbrMaterial::getMetallicFactor()            const { return metallicFactor_; }
    void   PbrMaterial::setMetallicFactor(float v)           { metallicFactor_ = v; }

    float  PbrMaterial::getRoughnessFactor()           const { return roughnessFactor_; }
    void   PbrMaterial::setRoughnessFactor(float v)          { roughnessFactor_ = v; }

    Color  PbrMaterial::getEmissiveColor()             const { return emissiveColor_; }
    void   PbrMaterial::setEmissiveColor(Color c)            { emissiveColor_ = c; }

    float  PbrMaterial::getNormalScale()               const { return normalScale_; }
    void   PbrMaterial::setNormalScale(float v)              { normalScale_ = v; }

    float  PbrMaterial::getOcclusionStrength()         const { return occlusionStrength_; }
    void   PbrMaterial::setOcclusionStrength(float v)        { occlusionStrength_ = v; }

    bool   PbrMaterial::isAlphaBlendEnabled()          const { return alphaBlend_; }
    void   PbrMaterial::setAlphaBlendEnabled(bool v)         { alphaBlend_ = v; }

    float  PbrMaterial::getAlphaCutoff()               const { return alphaCutoff_; }
    void   PbrMaterial::setAlphaCutoff(float v)              { alphaCutoff_ = v; }

} // namespace CNA

#endif // CNA_NOXNA
