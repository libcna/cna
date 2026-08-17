// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterType.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "CNA/Internal/Graphics/AlphaCoverageEXT.hpp"
#include "CNA/Internal/Graphics/PbrFresnelEXT.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

namespace Microsoft::Xna::Framework::Graphics
{
    namespace
    {
        constexpr int DirtyWorldViewProj = 1;
        constexpr int DirtyMaterialColor = 8;
        constexpr int DirtyFog          = 16;
        constexpr int DirtyFogEnable    = 32;
        constexpr int DirtyAll          = -1;

        void AddParam(EffectParameterCollection& params, const std::string& name,
                      int rows, int cols,
                      EffectParameterClass pc, EffectParameterType pt)
        {
            params.Add(EffectParameter(name, "", rows, cols, pc, pt));
        }
    }

    PbrEffect::PbrEffect(GraphicsDevice& device)
        : Effect(device)
        , dirtyFlags_(DirtyAll)
    {
        CacheEffectParameters();
    }

    PbrEffect::PbrEffect(const PbrEffect& src)
        : Effect(*src.device_)
        , dirtyFlags_(DirtyAll)
    {
        CacheEffectParameters();

        fogEnabled_ = src.fogEnabled_;

        world_      = src.world_;
        view_       = src.view_;
        projection_ = src.projection_;

        diffuseColor_      = src.diffuseColor_;
        alpha_             = src.alpha_;
        ambientLightColor_ = src.ambientLightColor_;
        emissiveFactor_         = src.emissiveFactor_;
        metallicFactor_         = src.metallicFactor_;
        roughnessFactor_        = src.roughnessFactor_;
        iorEXT_                 = src.iorEXT_;
        specularFactorEXT_      = src.specularFactorEXT_;
        specularColorFactorEXT_ = src.specularColorFactorEXT_;
        normalScale_            = src.normalScale_;
        occlusionStrength_      = src.occlusionStrength_;
        baseColorTextureIsSrgb_ = src.baseColorTextureIsSrgb_;
        emissiveTextureIsSrgb_  = src.emissiveTextureIsSrgb_;
        specularColorTextureIsSrgbEXT_ = src.specularColorTextureIsSrgbEXT_;
        encodeOutputToSrgb_     = src.encodeOutputToSrgb_;
        textureCoordinateSetsEXT_ = src.textureCoordinateSetsEXT_;
        textureTransformsEXT_ = src.textureTransformsEXT_;
        specularTextureCoordinateSetEXT_ = src.specularTextureCoordinateSetEXT_;
        specularColorTextureCoordinateSetEXT_ = src.specularColorTextureCoordinateSetEXT_;
        specularTextureTransformEXT_ = src.specularTextureTransformEXT_;
        specularColorTextureTransformEXT_ = src.specularColorTextureTransformEXT_;

        DirectionalLight0 = src.DirectionalLight0;
        DirectionalLight1 = src.DirectionalLight1;
        DirectionalLight2 = src.DirectionalLight2;

        fogStart_ = src.fogStart_;
        fogEnd_   = src.fogEnd_;
        if (fogColorParam_) fogColorParam_->SetValue(src.getFogColorProperty());

        texture_                = src.texture_;
        normalMap_              = src.normalMap_;
        metallicRoughnessMap_   = src.metallicRoughnessMap_;
        emissiveMap_            = src.emissiveMap_;
        occlusionMap_           = src.occlusionMap_;
        specularMapEXT_          = src.specularMapEXT_;
        specularColorMapEXT_     = src.specularColorMapEXT_;
        ownedTexture_               = src.ownedTexture_;
        ownedNormalMap_             = src.ownedNormalMap_;
        ownedMetallicRoughnessMap_  = src.ownedMetallicRoughnessMap_;
        ownedEmissiveMap_           = src.ownedEmissiveMap_;
        ownedOcclusionMap_          = src.ownedOcclusionMap_;
        ownedSpecularMapEXT_         = src.ownedSpecularMapEXT_;
        ownedSpecularColorMapEXT_    = src.ownedSpecularColorMapEXT_;
    }

    Effect* PbrEffect::Clone()
    {
        return new PbrEffect(*this);
    }

    void PbrEffect::CacheEffectParameters()
    {
        auto& params = getParametersProperty();
        AddParam(params, "DiffuseColor",  1, 4, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "FogColor",      1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "FogVector",     1, 4, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "WorldViewProj", 4, 4, EffectParameterClass::Matrix, EffectParameterType::Single);

        diffuseColorParam_  = params["DiffuseColor"];
        fogColorParam_      = params["FogColor"];
        fogVectorParam_     = params["FogVector"];
        worldViewProjParam_ = params["WorldViewProj"];
    }

    // IEffectMatrices
    Matrix PbrEffect::getWorldProperty() const      { return world_; }
    void   PbrEffect::setWorldProperty(const Matrix& v)
    {
        world_ = v;
        dirtyFlags_ |= DirtyWorldViewProj | DirtyFog;
    }

    Matrix PbrEffect::getViewProperty() const       { return view_; }
    void   PbrEffect::setViewProperty(const Matrix& v)
    {
        view_ = v;
        dirtyFlags_ |= DirtyWorldViewProj | DirtyFog;
    }

    Matrix PbrEffect::getProjectionProperty() const { return projection_; }
    void   PbrEffect::setProjectionProperty(const Matrix& v)
    {
        projection_ = v;
        dirtyFlags_ |= DirtyWorldViewProj;
    }

    Vector3 PbrEffect::getDiffuseColorProperty() const { return diffuseColor_; }
    void    PbrEffect::setDiffuseColorProperty(const Vector3& v)
    {
        diffuseColor_ = v;
        dirtyFlags_ |= DirtyMaterialColor;
    }

    float PbrEffect::getAlphaProperty() const { return alpha_; }
    void  PbrEffect::setAlphaProperty(float v)
    {
        alpha_ = v;
        dirtyFlags_ |= DirtyMaterialColor;
    }

    // IEffectLights
    Vector3 PbrEffect::getAmbientLightColorProperty() const { return ambientLightColor_; }
    void    PbrEffect::setAmbientLightColorProperty(const Vector3& v) { ambientLightColor_ = v; }

    bool PbrEffect::getLightingEnabledProperty() const { return true; }
    void PbrEffect::setLightingEnabledProperty(bool value)
    {
        if (!value)
            throw std::runtime_error("PbrEffect does not support setting LightingEnabled to false.");
    }

    DirectionalLight& PbrEffect::getDirectionalLight0Property() { return DirectionalLight0; }
    DirectionalLight& PbrEffect::getDirectionalLight1Property() { return DirectionalLight1; }
    DirectionalLight& PbrEffect::getDirectionalLight2Property() { return DirectionalLight2; }

    void PbrEffect::EnableDefaultLighting()
    {
        // Same key/fill/back light rig as BasicEffect/SkinnedEffect's own EnableDefaultLighting().
        DirectionalLight0.setDirectionProperty(Vector3{-0.5265408f, -0.5735765f, -0.6275069f});
        DirectionalLight0.setDiffuseColorProperty(Vector3{1.0f, 0.9607844f, 0.8078432f});
        DirectionalLight0.setEnabledProperty(true);
        DirectionalLight1.setDirectionProperty(Vector3{0.7198464f, 0.3420201f, 0.6040227f});
        DirectionalLight1.setDiffuseColorProperty(Vector3{0.9647059f, 0.7607844f, 0.4078432f});
        DirectionalLight1.setEnabledProperty(true);
        DirectionalLight2.setDirectionProperty(Vector3{0.4545195f, -0.7660444f, 0.4545195f});
        DirectionalLight2.setDiffuseColorProperty(Vector3{0.3231373f, 0.3607844f, 0.3937255f});
        DirectionalLight2.setEnabledProperty(true);

        setAmbientLightColorProperty(Vector3{0.05333332f, 0.09882354f, 0.1819608f});
    }

    // IEffectFog
    Vector3 PbrEffect::getFogColorProperty() const
    {
        return fogColorParam_ ? fogColorParam_->GetValueVector3() : Vector3{0.0f, 0.0f, 0.0f};
    }
    void PbrEffect::setFogColorProperty(const Vector3& v)
    {
        if (fogColorParam_) fogColorParam_->SetValue(v);
    }

    bool PbrEffect::getFogEnabledProperty() const { return fogEnabled_; }
    void PbrEffect::setFogEnabledProperty(bool v)
    {
        if (fogEnabled_ != v)
        {
            fogEnabled_ = v;
            dirtyFlags_ |= DirtyFogEnable;
        }
    }

    float PbrEffect::getFogStartProperty() const { return fogStart_; }
    void  PbrEffect::setFogStartProperty(float v)
    {
        fogStart_ = v;
        dirtyFlags_ |= DirtyFog;
    }

    float PbrEffect::getFogEndProperty() const { return fogEnd_; }
    void  PbrEffect::setFogEndProperty(float v)
    {
        fogEnd_ = v;
        dirtyFlags_ |= DirtyFog;
    }

    Texture2D* PbrEffect::getTextureProperty() const       { return texture_; }
    void       PbrEffect::setTextureProperty(Texture2D* v) { texture_ = v; }
    void PbrEffect::SetOwnedTexture(std::shared_ptr<Texture2D> texture)
    {
        ownedTexture_ = std::move(texture);
        texture_ = ownedTexture_.get();
    }

    Texture2D* PbrEffect::getNormalMapProperty() const       { return normalMap_; }
    void       PbrEffect::setNormalMapProperty(Texture2D* v) { normalMap_ = v; }
    void PbrEffect::SetOwnedNormalMap(std::shared_ptr<Texture2D> texture)
    {
        ownedNormalMap_ = std::move(texture);
        normalMap_ = ownedNormalMap_.get();
    }

    Texture2D* PbrEffect::getMetallicRoughnessMapProperty() const       { return metallicRoughnessMap_; }
    void       PbrEffect::setMetallicRoughnessMapProperty(Texture2D* v) { metallicRoughnessMap_ = v; }
    void PbrEffect::SetOwnedMetallicRoughnessMap(std::shared_ptr<Texture2D> texture)
    {
        ownedMetallicRoughnessMap_ = std::move(texture);
        metallicRoughnessMap_ = ownedMetallicRoughnessMap_.get();
    }

    Texture2D* PbrEffect::getEmissiveMapProperty() const       { return emissiveMap_; }
    void       PbrEffect::setEmissiveMapProperty(Texture2D* v) { emissiveMap_ = v; }
    void PbrEffect::SetOwnedEmissiveMap(std::shared_ptr<Texture2D> texture)
    {
        ownedEmissiveMap_ = std::move(texture);
        emissiveMap_ = ownedEmissiveMap_.get();
    }

    Texture2D* PbrEffect::getOcclusionMapProperty() const       { return occlusionMap_; }
    void       PbrEffect::setOcclusionMapProperty(Texture2D* v) { occlusionMap_ = v; }
    void PbrEffect::SetOwnedOcclusionMap(std::shared_ptr<Texture2D> texture)
    {
        ownedOcclusionMap_ = std::move(texture);
        occlusionMap_ = ownedOcclusionMap_.get();
    }

    float PbrEffect::getMetallicFactorProperty() const  { return metallicFactor_; }
    void  PbrEffect::setMetallicFactorProperty(float v) { metallicFactor_ = v; }

    float PbrEffect::getRoughnessFactorProperty() const  { return roughnessFactor_; }
    void  PbrEffect::setRoughnessFactorProperty(float v) { roughnessFactor_ = v; }

    float PbrEffect::getIorEXTProperty() const { return iorEXT_; }
    void PbrEffect::setIorEXTProperty(float v) { iorEXT_ = v; }
    float PbrEffect::getSpecularFactorEXTProperty() const { return specularFactorEXT_; }
    void PbrEffect::setSpecularFactorEXTProperty(float v) { specularFactorEXT_ = v; }
    Vector3 PbrEffect::getSpecularColorFactorEXTProperty() const
    {
        return specularColorFactorEXT_;
    }
    void PbrEffect::setSpecularColorFactorEXTProperty(const Vector3& v)
    {
        specularColorFactorEXT_ = v;
    }
    Texture2D* PbrEffect::getSpecularMapEXTProperty() const { return specularMapEXT_; }
    void PbrEffect::setSpecularMapEXTProperty(Texture2D* v) { specularMapEXT_ = v; }
    void PbrEffect::SetOwnedSpecularMapEXT(std::shared_ptr<Texture2D> texture)
    {
        ownedSpecularMapEXT_ = std::move(texture);
        specularMapEXT_ = ownedSpecularMapEXT_.get();
    }
    Texture2D* PbrEffect::getSpecularColorMapEXTProperty() const { return specularColorMapEXT_; }
    void PbrEffect::setSpecularColorMapEXTProperty(Texture2D* v) { specularColorMapEXT_ = v; }
    void PbrEffect::SetOwnedSpecularColorMapEXT(std::shared_ptr<Texture2D> texture)
    {
        ownedSpecularColorMapEXT_ = std::move(texture);
        specularColorMapEXT_ = ownedSpecularColorMapEXT_.get();
    }
    int PbrEffect::getSpecularTextureCoordinateSetEXTProperty() const
    {
        return specularTextureCoordinateSetEXT_;
    }
    void PbrEffect::setSpecularTextureCoordinateSetEXTProperty(int set)
    {
        if (set < 0 || set > 1)
            throw std::out_of_range("PBR packed texture-coordinate set must be 0 or 1.");
        specularTextureCoordinateSetEXT_ = set;
    }
    int PbrEffect::getSpecularColorTextureCoordinateSetEXTProperty() const
    {
        return specularColorTextureCoordinateSetEXT_;
    }
    void PbrEffect::setSpecularColorTextureCoordinateSetEXTProperty(int set)
    {
        if (set < 0 || set > 1)
            throw std::out_of_range("PBR packed texture-coordinate set must be 0 or 1.");
        specularColorTextureCoordinateSetEXT_ = set;
    }
    TextureTransformEXT PbrEffect::getSpecularTextureTransformEXTProperty() const
    {
        return specularTextureTransformEXT_;
    }
    void PbrEffect::setSpecularTextureTransformEXTProperty(const TextureTransformEXT& value)
    {
        specularTextureTransformEXT_ = value;
    }
    TextureTransformEXT PbrEffect::getSpecularColorTextureTransformEXTProperty() const
    {
        return specularColorTextureTransformEXT_;
    }
    void PbrEffect::setSpecularColorTextureTransformEXTProperty(const TextureTransformEXT& value)
    {
        specularColorTextureTransformEXT_ = value;
    }
    bool PbrEffect::getSpecularColorTextureIsSrgbEXTProperty() const
    {
        return specularColorTextureIsSrgbEXT_;
    }
    void PbrEffect::setSpecularColorTextureIsSrgbEXTProperty(bool v)
    {
        specularColorTextureIsSrgbEXT_ = v;
    }

    Vector3 PbrEffect::getEmissiveFactorProperty() const { return emissiveFactor_; }
    void    PbrEffect::setEmissiveFactorProperty(const Vector3& v) { emissiveFactor_ = v; }

    float   PbrEffect::getNormalScaleEXTProperty() const { return normalScale_; }
    void    PbrEffect::setNormalScaleEXTProperty(float v) { normalScale_ = v; }
    float   PbrEffect::getOcclusionStrengthEXTProperty() const { return occlusionStrength_; }
    void    PbrEffect::setOcclusionStrengthEXTProperty(float v) { occlusionStrength_ = v; }
    const std::array<int, 5>& PbrEffect::getTextureCoordinateSetsEXTProperty() const
    {
        return textureCoordinateSetsEXT_;
    }
    void PbrEffect::setTextureCoordinateSetEXTProperty(int slot, int set)
    {
        if (slot < 0 || slot >= static_cast<int>(textureCoordinateSetsEXT_.size()))
            throw std::out_of_range("PBR texture-coordinate slot must be in range [0, 4].");
        if (set < 0 || set > 1)
            throw std::out_of_range("PBR packed texture-coordinate set must be 0 or 1.");
        textureCoordinateSetsEXT_[static_cast<std::size_t>(slot)] = set;
    }
    const std::array<TextureTransformEXT, 5>& PbrEffect::getTextureTransformsEXTProperty() const
    {
        return textureTransformsEXT_;
    }
    void PbrEffect::setTextureTransformEXTProperty(int slot, const TextureTransformEXT& value)
    {
        if (slot < 0 || slot >= static_cast<int>(textureTransformsEXT_.size()))
            throw std::out_of_range("PBR texture-transform slot must be in range [0, 4].");
        textureTransformsEXT_[static_cast<std::size_t>(slot)] = value;
    }
    bool    PbrEffect::getBaseColorTextureIsSrgbEXTProperty() const { return baseColorTextureIsSrgb_; }
    void    PbrEffect::setBaseColorTextureIsSrgbEXTProperty(bool v) { baseColorTextureIsSrgb_ = v; }
    bool    PbrEffect::getEmissiveTextureIsSrgbEXTProperty() const { return emissiveTextureIsSrgb_; }
    void    PbrEffect::setEmissiveTextureIsSrgbEXTProperty(bool v) { emissiveTextureIsSrgb_ = v; }
    bool    PbrEffect::getEncodeOutputToSrgbEXTProperty() const { return encodeOutputToSrgb_; }
    void    PbrEffect::setEncodeOutputToSrgbEXTProperty(bool v) { encodeOutputToSrgb_ = v; }

    // plan_gltf.md GLTF-228/GLTF-229/GLTF-231. Plain carried state: nothing here touches the
    // device, and the renderer reads it through FillGpuDrawParams like every other material value.
    AlphaModeEXT PbrEffect::getAlphaModeEXTProperty() const { return alphaMode_; }
    void PbrEffect::setAlphaModeEXTProperty(AlphaModeEXT v) { alphaMode_ = v; }
    float PbrEffect::getAlphaCutoffEXTProperty() const { return alphaCutoff_; }
    void PbrEffect::setAlphaCutoffEXTProperty(float v) { alphaCutoff_ = v; }
    bool PbrEffect::getDoubleSidedEXTProperty() const { return doubleSided_; }
    void PbrEffect::setDoubleSidedEXTProperty(bool v) { doubleSided_ = v; }


    void PbrEffect::OnApply()
    {
        if ((dirtyFlags_ & DirtyWorldViewProj) != 0)
        {
            Matrix worldViewProj;
            Matrix::Multiply(world_, view_, worldView_);
            Matrix::Multiply(worldView_, projection_, worldViewProj);
            if (worldViewProjParam_) worldViewProjParam_->SetValue(worldViewProj);
            dirtyFlags_ &= ~DirtyWorldViewProj;
        }

        if (fogEnabled_)
        {
            if ((dirtyFlags_ & (DirtyFog | DirtyFogEnable)) != 0)
            {
                if (fogVectorParam_)
                {
                    if (fogStart_ == fogEnd_)
                    {
                        fogVectorParam_->SetValue(Vector4{0.0f, 0.0f, 0.0f, 1.0f});
                    }
                    else
                    {
                        float scale = 1.0f / (fogStart_ - fogEnd_);
                        fogVectorParam_->SetValue(Vector4{
                            worldView_.M13 * scale,
                            worldView_.M23 * scale,
                            worldView_.M33 * scale,
                            (worldView_.M43 + fogStart_) * scale});
                    }
                }
                dirtyFlags_ &= ~(DirtyFog | DirtyFogEnable);
            }
        }
        else if ((dirtyFlags_ & DirtyFogEnable) != 0)
        {
            if (fogVectorParam_) fogVectorParam_->SetValue(Vector4::Zero);
            dirtyFlags_ &= ~DirtyFogEnable;
        }

        if ((dirtyFlags_ & DirtyMaterialColor) != 0)
        {
            if (diffuseColorParam_)
                diffuseColorParam_->SetValue(Vector4{
                    diffuseColor_.X, diffuseColor_.Y, diffuseColor_.Z, alpha_});
            dirtyFlags_ &= ~DirtyMaterialColor;
        }
    }

    void PbrEffect::FillGpuDrawParams(CNA::Internal::Renderers::GpuDrawParams& p) const
    {
        // MOD-821: accepted-and-ignored on renderers without a shadow-sampling variant, exactly
        // like the PBR fields beside them.
        p.shadowsEnabled  = shadowsEnabledEXT_ && shadowMapEXT_ != nullptr;
        p.shadowDepthBias = shadowDepthBiasEXT_;
        if (p.shadowsEnabled)
        {
            p.shadowMap = &shadowMapEXT_->GetRenderer();
            const float* lightMatrix = &lightViewProjectionEXT_.M11;
            for (int i = 0; i < 16; ++i) p.lightViewProjColMajor[i] = lightMatrix[i];
        }

        using namespace CNA::Internal::Renderers;

        p.pbr             = true;
        p.textureEnabled  = true;
        p.lightingEnabled = true;

        if (texture_)              p.texture0 = &texture_->GetRenderer();
        if (normalMap_)             p.pbrNormalMap = &normalMap_->GetRenderer();
        if (metallicRoughnessMap_)  p.pbrMetallicRoughnessMap = &metallicRoughnessMap_->GetRenderer();
        if (emissiveMap_)           p.pbrEmissiveMap = &emissiveMap_->GetRenderer();
        if (occlusionMap_)          p.pbrOcclusionMap = &occlusionMap_->GetRenderer();
        if (specularMapEXT_)        p.pbrSpecularMap = &specularMapEXT_->GetRenderer();
        if (specularColorMapEXT_)   p.pbrSpecularColorMap = &specularColorMapEXT_->GetRenderer();

        // Base color factor is NOT premultiplied by alpha here (unlike most other CNA stock
        // effects' DiffuseColor) -- the PBR BRDF keeps albedo and alpha as independent
        // quantities (glTF's own baseColorFactor is RGBA but alpha only ever affects blending,
        // never the lit RGB response), matching the real glTF metallic-roughness model.
        p.diffuseColor[0] = diffuseColor_.X;
        p.diffuseColor[1] = diffuseColor_.Y;
        p.diffuseColor[2] = diffuseColor_.Z;
        p.diffuseColor[3] = alpha_;

        p.ambientColor[0] = ambientLightColor_.X;
        p.ambientColor[1] = ambientLightColor_.Y;
        p.ambientColor[2] = ambientLightColor_.Z;

        p.emissiveColor[0] = emissiveFactor_.X;
        p.emissiveColor[1] = emissiveFactor_.Y;
        p.emissiveColor[2] = emissiveFactor_.Z;

        // plan_gltf.md GLTF-210/GLTF-212: which bound textures are sRGB-encoded, and whether the
        // lit result is encoded back. Carried as three separate facts because they are three
        // separate decisions -- two about what a texture contains, one about where the fragment
        // is going.
        p.pbrNormalScale       = normalScale_;
        p.pbrOcclusionStrength = occlusionStrength_;
        p.pbrTextureCoordinateSetMask = 0;
        for (std::size_t i = 0; i < textureCoordinateSetsEXT_.size(); ++i)
            if (textureCoordinateSetsEXT_[i] == 1)
                p.pbrTextureCoordinateSetMask |= std::uint32_t{1} << i;
        for (std::size_t i = 0; i < textureTransformsEXT_.size(); ++i)
        {
            const TextureTransformEXT& transform = textureTransformsEXT_[i];
            const float cosine = std::cos(transform.Rotation);
            const float sine = std::sin(transform.Rotation);
            float* row0 = p.pbrTextureTransformRows[i * 2];
            float* row1 = p.pbrTextureTransformRows[i * 2 + 1];
            row0[0] = cosine * transform.Scale.X;
            row0[1] = -sine * transform.Scale.Y;
            row0[2] = transform.Offset.X;
            row0[3] = 0.0f;
            row1[0] = sine * transform.Scale.X;
            row1[1] = cosine * transform.Scale.Y;
            row1[2] = transform.Offset.Y;
            row1[3] = 0.0f;
        }
        if (specularTextureCoordinateSetEXT_ == 1)
            p.pbrTextureCoordinateSetMask |= std::uint32_t{1} << 5;
        if (specularColorTextureCoordinateSetEXT_ == 1)
            p.pbrTextureCoordinateSetMask |= std::uint32_t{1} << 6;
        const auto fillSpecularTransform = [&](std::size_t slot,
                                               const TextureTransformEXT& transform)
        {
            const float cosine = std::cos(transform.Rotation);
            const float sine = std::sin(transform.Rotation);
            float* row0 = p.pbrSpecularTextureTransformRows[slot * 2];
            float* row1 = p.pbrSpecularTextureTransformRows[slot * 2 + 1];
            row0[0] = cosine * transform.Scale.X;
            row0[1] = -sine * transform.Scale.Y;
            row0[2] = transform.Offset.X;
            row0[3] = 0.0f;
            row1[0] = sine * transform.Scale.X;
            row1[1] = cosine * transform.Scale.Y;
            row1[2] = transform.Offset.Y;
            row1[3] = 0.0f;
        };
        fillSpecularTransform(0, specularTextureTransformEXT_);
        fillSpecularTransform(1, specularColorTextureTransformEXT_);
        p.pbrBaseColorTextureIsSrgb = baseColorTextureIsSrgb_;
        p.pbrEmissiveTextureIsSrgb  = emissiveTextureIsSrgb_;
        p.pbrSpecularColorTextureIsSrgb = specularColorTextureIsSrgbEXT_;
        p.pbrEncodeOutputToSrgb     = encodeOutputToSrgb_;

        p.pbrMetallicFactor  = metallicFactor_;
        p.pbrRoughnessFactor = roughnessFactor_;
        const CNA::Internal::Graphics::PbrDielectricFresnelEXT dielectricFresnel =
            CNA::Internal::Graphics::ComputePbrDielectricFresnelEXT(
                iorEXT_, specularFactorEXT_,
                {specularColorFactorEXT_.X, specularColorFactorEXT_.Y,
                 specularColorFactorEXT_.Z});
        p.pbrDielectricF0[0] = dielectricFresnel.f0[0];
        p.pbrDielectricF0[1] = dielectricFresnel.f0[1];
        p.pbrDielectricF0[2] = dielectricFresnel.f0[2];
        p.pbrDielectricF90   = dielectricFresnel.f90;
        p.pbrDielectricF0Unclamped[0] = dielectricFresnel.unclampedF0[0];
        p.pbrDielectricF0Unclamped[1] = dielectricFresnel.unclampedF0[1];
        p.pbrDielectricF0Unclamped[2] = dielectricFresnel.unclampedF0[2];
        p.pbrSpecularFactor = dielectricFresnel.specularFactor;

        // plan_gltf.md GLTF-372: a MASK material's cutoff is the one piece of glTF alpha coverage
        // that is fragment-program work rather than device state, and every PBR shader already
        // discards on this vector -- it was simply never filled in, so a mask rendered opaque.
        const std::array<float, 4> alphaTest =
            CNA::Internal::Graphics::AlphaTestVectorForAlphaModeEXT(alphaMode_, alphaCutoff_);
        p.alphaTest[0] = alphaTest[0];
        p.alphaTest[1] = alphaTest[1];
        p.alphaTest[2] = alphaTest[2];
        p.alphaTest[3] = alphaTest[3];

        const bool    light0On = DirectionalLight0.getEnabledProperty();
        const Vector3 ld0  = light0On ? DirectionalLight0.getDiffuseColorProperty() : Vector3::Zero;
        const Vector3 dir0 = DirectionalLight0.getDirectionProperty();
        p.light0Dir[0] = dir0.X; p.light0Dir[1] = dir0.Y; p.light0Dir[2] = dir0.Z;
        p.light0Diffuse[0] = ld0.X; p.light0Diffuse[1] = ld0.Y; p.light0Diffuse[2] = ld0.Z;

        const bool    light1On = DirectionalLight1.getEnabledProperty();
        const Vector3 ld1  = light1On ? DirectionalLight1.getDiffuseColorProperty() : Vector3::Zero;
        const Vector3 dir1 = DirectionalLight1.getDirectionProperty();
        p.light1Dir[0] = dir1.X; p.light1Dir[1] = dir1.Y; p.light1Dir[2] = dir1.Z;
        p.light1Diffuse[0] = ld1.X; p.light1Diffuse[1] = ld1.Y; p.light1Diffuse[2] = ld1.Z;

        const bool    light2On = DirectionalLight2.getEnabledProperty();
        const Vector3 ld2  = light2On ? DirectionalLight2.getDiffuseColorProperty() : Vector3::Zero;
        const Vector3 dir2 = DirectionalLight2.getDirectionProperty();
        p.light2Dir[0] = dir2.X; p.light2Dir[1] = dir2.Y; p.light2Dir[2] = dir2.Z;
        p.light2Diffuse[0] = ld2.X; p.light2Diffuse[1] = ld2.Y; p.light2Diffuse[2] = ld2.Z;

        const Matrix viewInverse = Matrix::Invert(view_);
        const Vector3 eyePos     = viewInverse.getTranslationProperty();
        p.eyePositionWorld[0] = eyePos.X;
        p.eyePositionWorld[1] = eyePos.Y;
        p.eyePositionWorld[2] = eyePos.Z;

        world_.ToColumnMajor(p.worldColMajor);

        p.fogEnabled = fogEnabled_;
        const Vector3 fogColor = getFogColorProperty();
        p.fogColor[0] = fogColor.X;
        p.fogColor[1] = fogColor.Y;
        p.fogColor[2] = fogColor.Z;
        // REMED-GFX-010: FNA EffectHelpers.SetFogVector — view-space Z fog dotted with the
        // object-space vertex position in the shader; the vector bakes World*View's third column.
        Matrix fogWorldView; Matrix::Multiply(world_, view_, fogWorldView);
        if (!fogEnabled_)
        {
            p.fogVector[0] = p.fogVector[1] = p.fogVector[2] = p.fogVector[3] = 0.0f;
        }
        else if (fogStart_ == fogEnd_)
        {
            p.fogVector[0] = p.fogVector[1] = p.fogVector[2] = 0.0f;
            p.fogVector[3] = 1.0f;
        }
        else
        {
            const float s = 1.0f / (fogStart_ - fogEnd_);
            p.fogVector[0] = fogWorldView.M13 * s;
            p.fogVector[1] = fogWorldView.M23 * s;
            p.fogVector[2] = fogWorldView.M33 * s;
            p.fogVector[3] = (fogWorldView.M43 + fogStart_) * s;
        }
    }

    const std::string& PbrEffect::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.PbrEffect";
        return name;
    }

    void PbrEffect::setShadowMapEXT(Texture2D* shadowMap) { shadowMapEXT_ = shadowMap; }

    Texture2D* PbrEffect::getShadowMapEXT() const { return shadowMapEXT_; }

    void PbrEffect::setLightViewProjectionEXT(const Matrix& lightViewProjection)
    {
        lightViewProjectionEXT_ = lightViewProjection;
    }

    Matrix PbrEffect::getLightViewProjectionEXT() const { return lightViewProjectionEXT_; }

    void PbrEffect::setShadowsEnabledEXT(bool enabled) { shadowsEnabledEXT_ = enabled; }

    bool PbrEffect::isShadowsEnabledEXT() const { return shadowsEnabledEXT_; }

    void PbrEffect::setShadowDepthBiasEXT(float bias) { shadowDepthBiasEXT_ = bias; }

    float PbrEffect::getShadowDepthBiasEXT() const { return shadowDepthBiasEXT_; }
}
