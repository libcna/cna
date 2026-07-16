// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterType.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

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

    EnvironmentMapEffect::EnvironmentMapEffect(GraphicsDevice& device)
        : Effect(device)
        , dirtyFlags_(DirtyAll)
    {
        CacheEffectParameters();

        DirectionalLight0.setEnabledProperty(true);
        setEnvironmentMapAmountProperty(1.0f);
        setEnvironmentMapSpecularProperty(Vector3{0.0f, 0.0f, 0.0f});
        setFresnelFactorProperty(1.0f);
    }

    EnvironmentMapEffect::EnvironmentMapEffect(const EnvironmentMapEffect& src)
        : Effect(*src.device_)
        , dirtyFlags_(DirtyAll)
    {
        CacheEffectParameters();

        fogEnabled_      = src.fogEnabled_;
        fresnelEnabled_  = src.fresnelEnabled_;
        specularEnabled_ = src.specularEnabled_;

        world_      = src.world_;
        view_       = src.view_;
        projection_ = src.projection_;

        diffuseColor_      = src.diffuseColor_;
        emissiveColor_     = src.emissiveColor_;
        ambientLightColor_ = src.ambientLightColor_;
        alpha_             = src.alpha_;

        fogStart_ = src.fogStart_;
        fogEnd_   = src.fogEnd_;
        if (fogColorParam_) fogColorParam_->SetValue(src.getFogColorProperty());

        environmentMapAmount_   = src.environmentMapAmount_;
        environmentMapSpecular_ = src.environmentMapSpecular_;
        fresnelFactor_          = src.fresnelFactor_;

        texture_        = src.texture_;
        environmentMap_ = src.environmentMap_;
        ownedTexture_        = src.ownedTexture_;
        ownedEnvironmentMap_ = src.ownedEnvironmentMap_;

        DirectionalLight0 = src.DirectionalLight0;
        DirectionalLight1 = src.DirectionalLight1;
        DirectionalLight2 = src.DirectionalLight2;
    }

    Effect* EnvironmentMapEffect::Clone()
    {
        return new EnvironmentMapEffect(*this);
    }

    void EnvironmentMapEffect::CacheEffectParameters()
    {
        auto& params = getParametersProperty();
        AddParam(params, "EnvironmentMapAmount",   1, 1, EffectParameterClass::Scalar, EffectParameterType::Single);
        AddParam(params, "EnvironmentMapSpecular", 1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "FresnelFactor",          1, 1, EffectParameterClass::Scalar, EffectParameterType::Single);
        AddParam(params, "DiffuseColor",           1, 4, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "EmissiveColor",          1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "EyePosition",            1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "FogColor",               1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "FogVector",              1, 4, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "World",                  4, 4, EffectParameterClass::Matrix, EffectParameterType::Single);
        AddParam(params, "WorldInverseTranspose",  4, 4, EffectParameterClass::Matrix, EffectParameterType::Single);
        AddParam(params, "WorldViewProj",          4, 4, EffectParameterClass::Matrix, EffectParameterType::Single);
        AddParam(params, "ShaderIndex",            1, 1, EffectParameterClass::Scalar, EffectParameterType::Int32);

        environmentMapAmountParam_   = params["EnvironmentMapAmount"];
        environmentMapSpecularParam_ = params["EnvironmentMapSpecular"];
        fresnelFactorParam_          = params["FresnelFactor"];
        diffuseColorParam_           = params["DiffuseColor"];
        emissiveColorParam_          = params["EmissiveColor"];
        eyePositionParam_            = params["EyePosition"];
        fogColorParam_               = params["FogColor"];
        fogVectorParam_              = params["FogVector"];
        worldParam_                  = params["World"];
        worldInverseTransposeParam_  = params["WorldInverseTranspose"];
        worldViewProjParam_          = params["WorldViewProj"];
        shaderIndexParam_            = params["ShaderIndex"];
    }

    // IEffectMatrices
    Matrix EnvironmentMapEffect::getWorldProperty() const      { return world_; }
    void   EnvironmentMapEffect::setWorldProperty(const Matrix& v)
    {
        world_ = v;
        dirtyFlags_ |= DirtyWorld | DirtyWorldViewProj | DirtyFog;
    }

    Matrix EnvironmentMapEffect::getViewProperty() const       { return view_; }
    void   EnvironmentMapEffect::setViewProperty(const Matrix& v)
    {
        view_ = v;
        dirtyFlags_ |= DirtyWorldViewProj | DirtyEyePosition | DirtyFog;
    }

    Matrix EnvironmentMapEffect::getProjectionProperty() const { return projection_; }
    void   EnvironmentMapEffect::setProjectionProperty(const Matrix& v)
    {
        projection_ = v;
        dirtyFlags_ |= DirtyWorldViewProj;
    }

    Vector3 EnvironmentMapEffect::getDiffuseColorProperty() const     { return diffuseColor_; }
    void    EnvironmentMapEffect::setDiffuseColorProperty(const Vector3& v)
    {
        diffuseColor_ = v;
        dirtyFlags_ |= DirtyMaterialColor;
    }

    Vector3 EnvironmentMapEffect::getEmissiveColorProperty() const    { return emissiveColor_; }
    void    EnvironmentMapEffect::setEmissiveColorProperty(const Vector3& v)
    {
        emissiveColor_ = v;
        dirtyFlags_ |= DirtyMaterialColor;
    }

    float EnvironmentMapEffect::getAlphaProperty() const  { return alpha_; }
    void  EnvironmentMapEffect::setAlphaProperty(float v)
    {
        alpha_ = v;
        dirtyFlags_ |= DirtyMaterialColor;
    }

    // IEffectLights
    Vector3 EnvironmentMapEffect::getAmbientLightColorProperty() const    { return ambientLightColor_; }
    void    EnvironmentMapEffect::setAmbientLightColorProperty(const Vector3& v)
    {
        ambientLightColor_ = v;
        dirtyFlags_ |= DirtyMaterialColor;
    }

    bool EnvironmentMapEffect::getLightingEnabledProperty() const { return true; }
    void EnvironmentMapEffect::setLightingEnabledProperty(bool value)
    {
        if (!value)
            throw std::runtime_error("EnvironmentMapEffect does not support setting LightingEnabled to false.");
    }

    DirectionalLight& EnvironmentMapEffect::getDirectionalLight0Property() { return DirectionalLight0; }
    DirectionalLight& EnvironmentMapEffect::getDirectionalLight1Property() { return DirectionalLight1; }
    DirectionalLight& EnvironmentMapEffect::getDirectionalLight2Property() { return DirectionalLight2; }

    void EnvironmentMapEffect::EnableDefaultLighting()
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
    Vector3 EnvironmentMapEffect::getFogColorProperty() const
    {
        return fogColorParam_ ? fogColorParam_->GetValueVector3() : Vector3{0.0f, 0.0f, 0.0f};
    }
    void EnvironmentMapEffect::setFogColorProperty(const Vector3& v)
    {
        if (fogColorParam_) fogColorParam_->SetValue(v);
    }

    bool EnvironmentMapEffect::getFogEnabledProperty() const { return fogEnabled_; }
    void EnvironmentMapEffect::setFogEnabledProperty(bool v)
    {
        if (fogEnabled_ != v)
        {
            fogEnabled_ = v;
            dirtyFlags_ |= DirtyShaderIndex | DirtyFogEnable;
        }
    }

    float EnvironmentMapEffect::getFogStartProperty() const { return fogStart_; }
    void  EnvironmentMapEffect::setFogStartProperty(float v)
    {
        fogStart_ = v;
        dirtyFlags_ |= DirtyFog;
    }

    float EnvironmentMapEffect::getFogEndProperty() const { return fogEnd_; }
    void  EnvironmentMapEffect::setFogEndProperty(float v)
    {
        fogEnd_ = v;
        dirtyFlags_ |= DirtyFog;
    }

    Texture2D*   EnvironmentMapEffect::getTextureProperty() const        { return texture_; }
    void         EnvironmentMapEffect::setTextureProperty(Texture2D* v)  { texture_ = v; }
    void EnvironmentMapEffect::SetOwnedTexture(std::shared_ptr<Texture2D> texture)
    {
        ownedTexture_ = std::move(texture);
        texture_ = ownedTexture_.get();
    }

    TextureCube* EnvironmentMapEffect::getEnvironmentMapProperty() const         { return environmentMap_; }
    void         EnvironmentMapEffect::setEnvironmentMapProperty(TextureCube* v) { environmentMap_ = v; }
    void EnvironmentMapEffect::SetOwnedEnvironmentMap(std::shared_ptr<TextureCube> cubeMap)
    {
        ownedEnvironmentMap_ = std::move(cubeMap);
        environmentMap_ = ownedEnvironmentMap_.get();
    }

    float EnvironmentMapEffect::getEnvironmentMapAmountProperty() const { return environmentMapAmount_; }
    void  EnvironmentMapEffect::setEnvironmentMapAmountProperty(float v)
    {
        environmentMapAmount_ = v;
        if (environmentMapAmountParam_) environmentMapAmountParam_->SetValue(v);
    }

    Vector3 EnvironmentMapEffect::getEnvironmentMapSpecularProperty() const { return environmentMapSpecular_; }
    void    EnvironmentMapEffect::setEnvironmentMapSpecularProperty(const Vector3& v)
    {
        environmentMapSpecular_ = v;
        if (environmentMapSpecularParam_) environmentMapSpecularParam_->SetValue(v);

        bool enabled = (v.X != 0.0f || v.Y != 0.0f || v.Z != 0.0f);
        if (specularEnabled_ != enabled)
        {
            specularEnabled_ = enabled;
            dirtyFlags_ |= DirtyShaderIndex;
        }
    }

    float EnvironmentMapEffect::getFresnelFactorProperty() const { return fresnelFactor_; }
    void  EnvironmentMapEffect::setFresnelFactorProperty(float v)
    {
        fresnelFactor_ = v;
        if (fresnelFactorParam_) fresnelFactorParam_->SetValue(v);

        bool enabled = (v != 0.0f);
        if (fresnelEnabled_ != enabled)
        {
            fresnelEnabled_ = enabled;
            dirtyFlags_ |= DirtyShaderIndex;
        }
    }

    void EnvironmentMapEffect::OnApply()
    {
        // World * View * Projection and fog vector
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

        // Diffuse + emissive + ambient + alpha (lighting always enabled)
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
            if (!fogEnabled_)     shaderIndex += 1;
            if (fresnelEnabled_)  shaderIndex += 2;
            if (specularEnabled_) shaderIndex += 4;
            if (oneLight_)        shaderIndex += 8;

            if (shaderIndexParam_) shaderIndexParam_->SetValue(shaderIndex);
            dirtyFlags_ &= ~DirtyShaderIndex;
        }
    }

    void EnvironmentMapEffect::FillGpuDrawParams(CNA::Internal::Backends::GpuDrawParams& p) const
    {
        using namespace CNA::Internal::Backends;

        p.envMapping         = true;
        p.textureEnabled     = true;
        p.lightingEnabled    = true;
        p.vertexColorEnabled = false;

        if (texture_)        p.texture0 = &texture_->GetBackend();
        if (environmentMap_) p.envMap   = &environmentMap_->GetBackend();

        p.envMapAmount      = environmentMapAmount_;
        p.envMapSpecular[0] = environmentMapSpecular_.X;
        p.envMapSpecular[1] = environmentMapSpecular_.Y;
        p.envMapSpecular[2] = environmentMapSpecular_.Z;
        p.specularEnabled   = specularEnabled_;
        p.fresnelEnabled    = fresnelEnabled_;
        p.fresnelFactor     = fresnelFactor_;

        p.diffuseColor[0] = diffuseColor_.X * alpha_;
        p.diffuseColor[1] = diffuseColor_.Y * alpha_;
        p.diffuseColor[2] = diffuseColor_.Z * alpha_;
        p.diffuseColor[3] = alpha_;

        // emissive + ambient * diffuse, pre-combined and pre-multiplied by alpha (matches FNA)
        p.emissiveColor[0] = (emissiveColor_.X + ambientLightColor_.X * diffuseColor_.X) * alpha_;
        p.emissiveColor[1] = (emissiveColor_.Y + ambientLightColor_.Y * diffuseColor_.Y) * alpha_;
        p.emissiveColor[2] = (emissiveColor_.Z + ambientLightColor_.Z * diffuseColor_.Z) * alpha_;

        // Task 890 fix: FNA's real EnvironmentMapEffect.fx (via the shared Lighting.fxh
        // ComputeLights()) sums all 3 directional lights' diffuse contribution, not just
        // DirectionalLight0 -- matches FNA's DirectionalLight.Enabled setter zeroing the
        // GPU-facing diffuse when disabled (the same gating BasicEffect::FillGpuDrawParams
        // already applies).
        const bool    light0On = DirectionalLight0.getEnabledProperty();
        const Vector3 ld  = light0On ? DirectionalLight0.getDiffuseColorProperty() : Vector3::Zero;
        const Vector3 dir = DirectionalLight0.getDirectionProperty();
        p.light0Dir[0]    = dir.X; p.light0Dir[1]    = dir.Y; p.light0Dir[2]    = dir.Z;
        p.light0Diffuse[0]= ld.X;  p.light0Diffuse[1]= ld.Y;  p.light0Diffuse[2]= ld.Z;

        const bool    light1On = DirectionalLight1.getEnabledProperty();
        const Vector3 ld1  = light1On ? DirectionalLight1.getDiffuseColorProperty() : Vector3::Zero;
        const Vector3 dir1 = DirectionalLight1.getDirectionProperty();
        p.light1Dir[0]     = dir1.X; p.light1Dir[1]     = dir1.Y; p.light1Dir[2]     = dir1.Z;
        p.light1Diffuse[0] = ld1.X;  p.light1Diffuse[1] = ld1.Y;  p.light1Diffuse[2] = ld1.Z;

        const bool    light2On = DirectionalLight2.getEnabledProperty();
        const Vector3 ld2  = light2On ? DirectionalLight2.getDiffuseColorProperty() : Vector3::Zero;
        const Vector3 dir2 = DirectionalLight2.getDirectionProperty();
        p.light2Dir[0]     = dir2.X; p.light2Dir[1]     = dir2.Y; p.light2Dir[2]     = dir2.Z;
        p.light2Diffuse[0] = ld2.X;  p.light2Diffuse[1] = ld2.Y;  p.light2Diffuse[2] = ld2.Z;

        // Eye world position from the inverse view matrix
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
        p.fogStart    = fogStart_;
        p.fogEnd      = fogEnd_;
    }

    const std::string& EnvironmentMapEffect::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.EnvironmentMapEffect";
        return name;
    }
}
