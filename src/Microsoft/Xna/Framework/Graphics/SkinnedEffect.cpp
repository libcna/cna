#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterType.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Graphics
{
    namespace
    {
        constexpr int DirtyWorldViewProj = 1;
        constexpr int DirtyWorld        = 2;
        constexpr int DirtyEyePosition  = 4;
        constexpr int DirtyMaterialColor= 8;
        constexpr int DirtyFog         = 16;
        constexpr int DirtyFogEnable   = 32;
        constexpr int DirtyShaderIndex = 128;
        constexpr int DirtyAll         = -1;

        static void AddParam(EffectParameterCollection& params, const std::string& name,
                             int rows, int cols,
                             EffectParameterClass pc, EffectParameterType pt)
        {
            params.Add(EffectParameter(name, "", rows, cols, pc, pt));
        }
    }

    SkinnedEffect::SkinnedEffect(GraphicsDevice& device)
        : Effect(device)
        , dirtyFlags_(DirtyAll)
    {
        CacheEffectParameters();

        DirectionalLight0.setEnabledProperty(true);

        setSpecularColorProperty(Vector3{1.0f, 1.0f, 1.0f});
        setSpecularPowerProperty(16.0f);

        // Initialise bone transforms to identity
        std::vector<Matrix> identityBones(MaxBones, Matrix::getIdentityProperty());
        SetBoneTransforms(identityBones);
    }

    SkinnedEffect::SkinnedEffect(const SkinnedEffect& src)
        : Effect(*src.device_)
        , dirtyFlags_(DirtyAll)
    {
        CacheEffectParameters();

        preferPerPixelLighting_ = src.preferPerPixelLighting_;
        fogEnabled_             = src.fogEnabled_;

        world_      = src.world_;
        view_       = src.view_;
        projection_ = src.projection_;

        diffuseColor_      = src.diffuseColor_;
        emissiveColor_     = src.emissiveColor_;
        ambientLightColor_ = src.ambientLightColor_;
        alpha_             = src.alpha_;

        specularColor_ = src.specularColor_;
        specularPower_ = src.specularPower_;

        fogStart_ = src.fogStart_;
        fogEnd_   = src.fogEnd_;

        weightsPerVertex_ = src.weightsPerVertex_;
        texture_          = src.texture_;

        DirectionalLight0 = src.DirectionalLight0;
        DirectionalLight1 = src.DirectionalLight1;
        DirectionalLight2 = src.DirectionalLight2;

        // Copy bone transforms from source parameter storage
        if (src.bonesParam_ && bonesParam_)
        {
            auto bones = src.bonesParam_->GetValueMatrixArray(MaxBones);
            bonesParam_->SetValue(bones);
        }
    }

    Effect* SkinnedEffect::Clone()
    {
        return new SkinnedEffect(*this);
    }

    void SkinnedEffect::CacheEffectParameters()
    {
        auto& params = getParametersProperty();
        AddParam(params, "DiffuseColor",          1,        4, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "EmissiveColor",          1,        3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "SpecularColor",          1,        3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "SpecularPower",          1,        1, EffectParameterClass::Scalar, EffectParameterType::Single);
        AddParam(params, "EyePosition",            1,        3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "FogColor",               1,        3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "FogVector",              1,        4, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "World",                  4,        4, EffectParameterClass::Matrix, EffectParameterType::Single);
        AddParam(params, "WorldInverseTranspose",  4,        4, EffectParameterClass::Matrix, EffectParameterType::Single);
        AddParam(params, "WorldViewProj",          4,        4, EffectParameterClass::Matrix, EffectParameterType::Single);
        AddParam(params, "Bones",                  MaxBones, 4, EffectParameterClass::Matrix, EffectParameterType::Single);
        AddParam(params, "ShaderIndex",            1,        1, EffectParameterClass::Scalar, EffectParameterType::Int32);

        diffuseColorParam_          = params["DiffuseColor"];
        emissiveColorParam_         = params["EmissiveColor"];
        specularColorParam_         = params["SpecularColor"];
        specularPowerParam_         = params["SpecularPower"];
        eyePositionParam_           = params["EyePosition"];
        fogColorParam_              = params["FogColor"];
        fogVectorParam_             = params["FogVector"];
        worldParam_                 = params["World"];
        worldInverseTransposeParam_ = params["WorldInverseTranspose"];
        worldViewProjParam_         = params["WorldViewProj"];
        bonesParam_                 = params["Bones"];
        shaderIndexParam_           = params["ShaderIndex"];
    }

    // IEffectMatrices
    Matrix SkinnedEffect::getWorldProperty() const      { return world_; }
    void   SkinnedEffect::setWorldProperty(const Matrix& v)
    {
        world_ = v;
        dirtyFlags_ |= DirtyWorld | DirtyWorldViewProj | DirtyFog;
    }

    Matrix SkinnedEffect::getViewProperty() const       { return view_; }
    void   SkinnedEffect::setViewProperty(const Matrix& v)
    {
        view_ = v;
        dirtyFlags_ |= DirtyWorldViewProj | DirtyEyePosition | DirtyFog;
    }

    Matrix SkinnedEffect::getProjectionProperty() const { return projection_; }
    void   SkinnedEffect::setProjectionProperty(const Matrix& v)
    {
        projection_ = v;
        dirtyFlags_ |= DirtyWorldViewProj;
    }

    Vector3 SkinnedEffect::getDiffuseColorProperty() const     { return diffuseColor_; }
    void    SkinnedEffect::setDiffuseColorProperty(const Vector3& v)
    {
        diffuseColor_ = v;
        dirtyFlags_ |= DirtyMaterialColor;
    }

    Vector3 SkinnedEffect::getEmissiveColorProperty() const    { return emissiveColor_; }
    void    SkinnedEffect::setEmissiveColorProperty(const Vector3& v)
    {
        emissiveColor_ = v;
        dirtyFlags_ |= DirtyMaterialColor;
    }

    Vector3 SkinnedEffect::getSpecularColorProperty() const
    {
        return specularColorParam_ ? specularColorParam_->GetValueVector3() : specularColor_;
    }
    void SkinnedEffect::setSpecularColorProperty(const Vector3& v)
    {
        specularColor_ = v;
        if (specularColorParam_) specularColorParam_->SetValue(v);
    }

    float SkinnedEffect::getSpecularPowerProperty() const
    {
        return specularPowerParam_ ? specularPowerParam_->GetValueSingle() : specularPower_;
    }
    void SkinnedEffect::setSpecularPowerProperty(float v)
    {
        specularPower_ = v;
        if (specularPowerParam_) specularPowerParam_->SetValue(v);
    }

    float SkinnedEffect::getAlphaProperty() const  { return alpha_; }
    void  SkinnedEffect::setAlphaProperty(float v)
    {
        alpha_ = v;
        dirtyFlags_ |= DirtyMaterialColor;
    }

    bool SkinnedEffect::getPreferPerPixelLightingProperty() const { return preferPerPixelLighting_; }
    void SkinnedEffect::setPreferPerPixelLightingProperty(bool v)
    {
        if (preferPerPixelLighting_ != v)
        {
            preferPerPixelLighting_ = v;
            dirtyFlags_ |= DirtyShaderIndex;
        }
    }

    // IEffectLights
    Vector3 SkinnedEffect::getAmbientLightColorProperty() const    { return ambientLightColor_; }
    void    SkinnedEffect::setAmbientLightColorProperty(const Vector3& v)
    {
        ambientLightColor_ = v;
        dirtyFlags_ |= DirtyMaterialColor;
    }

    bool SkinnedEffect::getLightingEnabledProperty() const { return true; }
    void SkinnedEffect::setLightingEnabledProperty(bool value)
    {
        if (!value)
            throw std::runtime_error("SkinnedEffect does not support setting LightingEnabled to false.");
    }

    DirectionalLight& SkinnedEffect::getDirectionalLight0Property() { return DirectionalLight0; }
    DirectionalLight& SkinnedEffect::getDirectionalLight1Property() { return DirectionalLight1; }
    DirectionalLight& SkinnedEffect::getDirectionalLight2Property() { return DirectionalLight2; }

    void SkinnedEffect::EnableDefaultLighting()
    {
        // Key light
        DirectionalLight0.setDirectionProperty(Vector3{-0.5265408f, -0.5735765f, -0.6275069f});
        DirectionalLight0.setDiffuseColorProperty(Vector3{1.0f, 0.9607844f, 0.8078432f});
        DirectionalLight0.setSpecularColorProperty(Vector3{1.0f, 0.9607844f, 0.8078432f});
        DirectionalLight0.setEnabledProperty(true);
        // Fill light
        DirectionalLight1.setDirectionProperty(Vector3{0.7198464f, 0.3420201f, 0.6040227f});
        DirectionalLight1.setDiffuseColorProperty(Vector3{0.9647059f, 0.7607844f, 0.4078432f});
        DirectionalLight1.setSpecularColorProperty(Vector3{0.0f, 0.0f, 0.0f});
        DirectionalLight1.setEnabledProperty(true);
        // Back light
        DirectionalLight2.setDirectionProperty(Vector3{0.4545195f, -0.7660444f, 0.4545195f});
        DirectionalLight2.setDiffuseColorProperty(Vector3{0.3231373f, 0.3607844f, 0.3937255f});
        DirectionalLight2.setSpecularColorProperty(Vector3{0.3231373f, 0.3607844f, 0.3937255f});
        DirectionalLight2.setEnabledProperty(true);

        setAmbientLightColorProperty(Vector3{0.05333332f, 0.09882354f, 0.1819608f});
    }

    // IEffectFog
    Vector3 SkinnedEffect::getFogColorProperty() const
    {
        return fogColorParam_ ? fogColorParam_->GetValueVector3() : Vector3{0.0f, 0.0f, 0.0f};
    }
    void SkinnedEffect::setFogColorProperty(const Vector3& v)
    {
        if (fogColorParam_) fogColorParam_->SetValue(v);
    }

    bool SkinnedEffect::getFogEnabledProperty() const { return fogEnabled_; }
    void SkinnedEffect::setFogEnabledProperty(bool v)
    {
        if (fogEnabled_ != v)
        {
            fogEnabled_ = v;
            dirtyFlags_ |= DirtyShaderIndex | DirtyFogEnable;
        }
    }

    float SkinnedEffect::getFogStartProperty() const { return fogStart_; }
    void  SkinnedEffect::setFogStartProperty(float v)
    {
        fogStart_ = v;
        dirtyFlags_ |= DirtyFog;
    }

    float SkinnedEffect::getFogEndProperty() const { return fogEnd_; }
    void  SkinnedEffect::setFogEndProperty(float v)
    {
        fogEnd_ = v;
        dirtyFlags_ |= DirtyFog;
    }

    Texture2D* SkinnedEffect::getTextureProperty() const       { return texture_; }
    void       SkinnedEffect::setTextureProperty(Texture2D* v) { texture_ = v; }

    int  SkinnedEffect::getWeightsPerVertexProperty() const { return weightsPerVertex_; }
    void SkinnedEffect::setWeightsPerVertexProperty(int v)
    {
        if (v != 1 && v != 2 && v != 4)
            throw std::out_of_range("WeightsPerVertex must be 1, 2, or 4.");
        weightsPerVertex_ = v;
        dirtyFlags_ |= DirtyShaderIndex;
    }

    void SkinnedEffect::SetBoneTransforms(const std::vector<Matrix>& boneTransforms)
    {
        if (boneTransforms.empty())
            throw std::invalid_argument("boneTransforms must not be empty.");
        if (static_cast<int>(boneTransforms.size()) > MaxBones)
            throw std::invalid_argument("boneTransforms exceeds MaxBones.");

        if (bonesParam_) bonesParam_->SetValue(boneTransforms);
    }

    std::vector<Matrix> SkinnedEffect::GetBoneTransforms(int count) const
    {
        if (count <= 0 || count > MaxBones)
            throw std::out_of_range("count must be in range [1, MaxBones].");

        std::vector<Matrix> bones = bonesParam_
            ? bonesParam_->GetValueMatrixArray(count)
            : std::vector<Matrix>(count, Matrix::getIdentityProperty());

        // Mirror FNA: restore M44 = 1 (bones may be stored in 4x3 form)
        for (auto& m : bones)
            m.M44 = 1.0f;

        return bones;
    }

    void SkinnedEffect::OnApply()
    {
        // WorldViewProj and fog vector
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
        else
        {
            if ((dirtyFlags_ & DirtyFogEnable) != 0)
            {
                if (fogVectorParam_) fogVectorParam_->SetValue(Vector4::Zero);
                dirtyFlags_ &= ~DirtyFogEnable;
            }
        }

        // World matrix and world inverse transpose for lighting
        if ((dirtyFlags_ & DirtyWorld) != 0)
        {
            if (worldParam_) worldParam_->SetValue(world_);

            if (worldInverseTransposeParam_)
            {
                Matrix worldTranspose;
                Matrix worldInverseTranspose;
                Matrix::Invert(world_, worldTranspose);
                Matrix::Transpose(worldTranspose, worldInverseTranspose);
                worldInverseTransposeParam_->SetValue(worldInverseTranspose);
            }
            dirtyFlags_ &= ~DirtyWorld;
        }

        // Eye position from inverse view
        if ((dirtyFlags_ & DirtyEyePosition) != 0)
        {
            if (eyePositionParam_)
            {
                Matrix viewInverse = Matrix::Invert(view_);
                eyePositionParam_->SetValue(viewInverse.getTranslationProperty());
            }
            dirtyFlags_ &= ~DirtyEyePosition;
        }

        // Diffuse + emissive + ambient + alpha (lighting always on)
        if ((dirtyFlags_ & DirtyMaterialColor) != 0)
        {
            if (diffuseColorParam_)
                diffuseColorParam_->SetValue(Vector4{
                    diffuseColor_.X * alpha_,
                    diffuseColor_.Y * alpha_,
                    diffuseColor_.Z * alpha_,
                    alpha_});

            if (emissiveColorParam_)
                emissiveColorParam_->SetValue(Vector3{
                    (emissiveColor_.X + ambientLightColor_.X * diffuseColor_.X) * alpha_,
                    (emissiveColor_.Y + ambientLightColor_.Y * diffuseColor_.Y) * alpha_,
                    (emissiveColor_.Z + ambientLightColor_.Z * diffuseColor_.Z) * alpha_});

            dirtyFlags_ &= ~DirtyMaterialColor;
        }

        // One-light optimisation check
        bool newOneLight = !DirectionalLight1.getEnabledProperty() && !DirectionalLight2.getEnabledProperty();
        if (oneLight_ != newOneLight)
        {
            oneLight_ = newOneLight;
            dirtyFlags_ |= DirtyShaderIndex;
        }

        // Shader index / variant
        if ((dirtyFlags_ & DirtyShaderIndex) != 0)
        {
            int shaderIndex = 0;
            if (!fogEnabled_)           shaderIndex += 1;
            if (weightsPerVertex_ == 2) shaderIndex += 2;
            else if (weightsPerVertex_ == 4) shaderIndex += 4;

            if (preferPerPixelLighting_) shaderIndex += 12;
            else if (oneLight_)          shaderIndex += 6;

            if (shaderIndexParam_) shaderIndexParam_->SetValue(shaderIndex);
            dirtyFlags_ &= ~DirtyShaderIndex;
        }
    }
}
