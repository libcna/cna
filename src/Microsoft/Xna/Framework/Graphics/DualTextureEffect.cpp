// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterType.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    namespace
    {
        constexpr int DirtyWorldViewProj = 1;
        constexpr int DirtyMaterialColor = 8;
        constexpr int DirtyFog          = 16;
        constexpr int DirtyFogEnable    = 32;
        constexpr int DirtyShaderIndex  = 128;
        constexpr int DirtyAll          = -1;

        static void AddParam(EffectParameterCollection& params, const std::string& name,
                             int rows, int cols,
                             EffectParameterClass pc, EffectParameterType pt)
        {
            params.Add(EffectParameter(name, "", rows, cols, pc, pt));
        }
    }

    DualTextureEffect::DualTextureEffect(GraphicsDevice& device)
        : Effect(device)
        , dirtyFlags_(DirtyAll)
    {
        CacheEffectParameters();
    }

    DualTextureEffect::DualTextureEffect(const DualTextureEffect& src)
        : Effect(*src.device_)
        , dirtyFlags_(DirtyAll)
    {
        CacheEffectParameters();

        fogEnabled_         = src.fogEnabled_;
        vertexColorEnabled_ = src.vertexColorEnabled_;

        world_      = src.world_;
        view_       = src.view_;
        projection_ = src.projection_;

        diffuseColor_ = src.diffuseColor_;
        alpha_        = src.alpha_;

        fogStart_ = src.fogStart_;
        fogEnd_   = src.fogEnd_;
        if (fogColorParam_) fogColorParam_->SetValue(src.getFogColorProperty());

        texture_  = src.texture_;
        texture2_ = src.texture2_;
        ownedTexture_  = src.ownedTexture_;
        ownedTexture2_ = src.ownedTexture2_;
    }

    Effect* DualTextureEffect::Clone()
    {
        return new DualTextureEffect(*this);
    }

    void DualTextureEffect::CacheEffectParameters()
    {
        auto& params = getParametersProperty();
        AddParam(params, "DiffuseColor",  1, 4, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "FogColor",      1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "FogVector",     1, 4, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "WorldViewProj", 4, 4, EffectParameterClass::Matrix, EffectParameterType::Single);
        AddParam(params, "ShaderIndex",   1, 1, EffectParameterClass::Scalar, EffectParameterType::Int32);

        diffuseColorParam_  = params["DiffuseColor"];
        fogColorParam_      = params["FogColor"];
        fogVectorParam_     = params["FogVector"];
        worldViewProjParam_ = params["WorldViewProj"];
        shaderIndexParam_   = params["ShaderIndex"];
    }

    // IEffectMatrices
    Matrix DualTextureEffect::getWorldProperty() const      { return world_; }
    void   DualTextureEffect::setWorldProperty(const Matrix& v)
    {
        world_ = v;
        dirtyFlags_ |= DirtyWorldViewProj | DirtyFog;
    }

    Matrix DualTextureEffect::getViewProperty() const       { return view_; }
    void   DualTextureEffect::setViewProperty(const Matrix& v)
    {
        view_ = v;
        dirtyFlags_ |= DirtyWorldViewProj | DirtyFog;
    }

    Matrix DualTextureEffect::getProjectionProperty() const { return projection_; }
    void   DualTextureEffect::setProjectionProperty(const Matrix& v)
    {
        projection_ = v;
        dirtyFlags_ |= DirtyWorldViewProj;
    }

    Vector3 DualTextureEffect::getDiffuseColorProperty() const     { return diffuseColor_; }
    void    DualTextureEffect::setDiffuseColorProperty(const Vector3& v)
    {
        diffuseColor_ = v;
        dirtyFlags_ |= DirtyMaterialColor;
    }

    float DualTextureEffect::getAlphaProperty() const  { return alpha_; }
    void  DualTextureEffect::setAlphaProperty(float v)
    {
        alpha_ = v;
        dirtyFlags_ |= DirtyMaterialColor;
    }

    // IEffectFog
    Vector3 DualTextureEffect::getFogColorProperty() const
    {
        return fogColorParam_ ? fogColorParam_->GetValueVector3() : Vector3{0.0f, 0.0f, 0.0f};
    }
    void DualTextureEffect::setFogColorProperty(const Vector3& v)
    {
        if (fogColorParam_) fogColorParam_->SetValue(v);
    }

    bool DualTextureEffect::getFogEnabledProperty() const { return fogEnabled_; }
    void DualTextureEffect::setFogEnabledProperty(bool v)
    {
        if (fogEnabled_ != v)
        {
            fogEnabled_ = v;
            dirtyFlags_ |= DirtyShaderIndex | DirtyFogEnable;
        }
    }

    float DualTextureEffect::getFogStartProperty() const { return fogStart_; }
    void  DualTextureEffect::setFogStartProperty(float v)
    {
        fogStart_ = v;
        dirtyFlags_ |= DirtyFog;
    }

    float DualTextureEffect::getFogEndProperty() const { return fogEnd_; }
    void  DualTextureEffect::setFogEndProperty(float v)
    {
        fogEnd_ = v;
        dirtyFlags_ |= DirtyFog;
    }

    Texture2D* DualTextureEffect::getTextureProperty() const       { return texture_; }
    void       DualTextureEffect::setTextureProperty(Texture2D* v) { texture_ = v; }
    void DualTextureEffect::SetOwnedTexture(std::shared_ptr<Texture2D> texture)
    {
        ownedTexture_ = std::move(texture);
        texture_ = ownedTexture_.get();
    }

    Texture2D* DualTextureEffect::getTexture2Property() const       { return texture2_; }
    void       DualTextureEffect::setTexture2Property(Texture2D* v) { texture2_ = v; }
    void DualTextureEffect::SetOwnedTexture2(std::shared_ptr<Texture2D> texture)
    {
        ownedTexture2_ = std::move(texture);
        texture2_ = ownedTexture2_.get();
    }

    bool DualTextureEffect::getVertexColorEnabledProperty() const { return vertexColorEnabled_; }
    void DualTextureEffect::setVertexColorEnabledProperty(bool v)
    {
        if (vertexColorEnabled_ != v)
        {
            vertexColorEnabled_ = v;
            dirtyFlags_ |= DirtyShaderIndex;
        }
    }

    void DualTextureEffect::OnApply()
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

        // Diffuse color + alpha
        if ((dirtyFlags_ & DirtyMaterialColor) != 0)
        {
            if (diffuseColorParam_)
                diffuseColorParam_->SetValue(Vector4{
                    diffuseColor_.X * alpha_,
                    diffuseColor_.Y * alpha_,
                    diffuseColor_.Z * alpha_,
                    alpha_});
            dirtyFlags_ &= ~DirtyMaterialColor;
        }

        // Shader variant index
        if ((dirtyFlags_ & DirtyShaderIndex) != 0)
        {
            int shaderIndex = 0;
            if (!fogEnabled_)        shaderIndex += 1;
            if (vertexColorEnabled_) shaderIndex += 2;

            if (shaderIndexParam_) shaderIndexParam_->SetValue(shaderIndex);
            dirtyFlags_ &= ~DirtyShaderIndex;
        }
    }

    void DualTextureEffect::FillGpuDrawParams(CNA::Internal::Backends::GpuDrawParams& p) const
    {
        using namespace CNA::Internal::Backends;

        p.dualTexture        = true;
        p.textureEnabled     = true;
        p.vertexColorEnabled = vertexColorEnabled_;
        p.lightingEnabled    = false;

        if (texture_)  p.texture0 = &texture_->GetBackend();
        if (texture2_) p.texture1 = &texture2_->GetBackend();

        p.diffuseColor[0] = diffuseColor_.X * alpha_;
        p.diffuseColor[1] = diffuseColor_.Y * alpha_;
        p.diffuseColor[2] = diffuseColor_.Z * alpha_;
        p.diffuseColor[3] = alpha_;

        world_.ToColumnMajor(p.worldColMajor);
        // alphaTest stays at default {0,0,1,1} (Always pass — DualTextureEffect has no alpha test)

        const Vector3 fogColor = getFogColorProperty();
        p.fogEnabled  = fogEnabled_;
        p.fogColor[0] = fogColor.X;
        p.fogColor[1] = fogColor.Y;
        p.fogColor[2] = fogColor.Z;
        p.fogStart    = fogStart_;
        p.fogEnd      = fogEnd_;
    }

    const std::string& DualTextureEffect::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.DualTextureEffect";
        return name;
    }
}
