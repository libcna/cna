// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterType.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    // Dirty flag bit positions (mirrors FNA EffectDirtyFlags)
    namespace
    {
        constexpr int DirtyWorldViewProj = 1;
        constexpr int DirtyMaterialColor = 8;
        constexpr int DirtyFog          = 16;
        constexpr int DirtyFogEnable    = 32;
        constexpr int DirtyAlphaTest    = 64;
        constexpr int DirtyShaderIndex  = 128;
        constexpr int DirtyAll          = -1;
    }

    static void AddParam(EffectParameterCollection& params, const std::string& name,
                         int rows, int cols,
                         EffectParameterClass pc, EffectParameterType pt)
    {
        params.Add(EffectParameter(name, "", rows, cols, pc, pt));
    }

    AlphaTestEffect::AlphaTestEffect(GraphicsDevice& device)
        : Effect(device)
        , dirtyFlags_(DirtyAll)
    {
        CacheEffectParameters();
    }

    AlphaTestEffect::AlphaTestEffect(const AlphaTestEffect& src)
        : Effect(*src.device_)
        , dirtyFlags_(DirtyAll)
    {
        CacheEffectParameters();

        fogEnabled_         = src.fogEnabled_;
        vertexColorEnabled_ = src.vertexColorEnabled_;
        world_              = src.world_;
        view_               = src.view_;
        projection_         = src.projection_;
        diffuseColor_       = src.diffuseColor_;
        alpha_              = src.alpha_;
        fogStart_           = src.fogStart_;
        fogEnd_             = src.fogEnd_;
        if (fogColorParam_) fogColorParam_->SetValue(src.getFogColorProperty());
        alphaFunction_      = src.alphaFunction_;
        referenceAlpha_     = src.referenceAlpha_;
        texture_            = src.texture_;
        ownedTexture_       = src.ownedTexture_;
    }

    Effect* AlphaTestEffect::Clone()
    {
        return new AlphaTestEffect(*this);
    }

    void AlphaTestEffect::CacheEffectParameters()
    {
        auto& params = getParametersProperty();
        AddParam(params, "DiffuseColor",   1, 4, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "AlphaTest",      1, 4, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "FogColor",       1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "FogVector",      1, 4, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "WorldViewProj",  4, 4, EffectParameterClass::Matrix, EffectParameterType::Single);
        AddParam(params, "ShaderIndex",    1, 1, EffectParameterClass::Scalar, EffectParameterType::Int32);

        diffuseColorParam_  = params["DiffuseColor"];
        alphaTestParam_     = params["AlphaTest"];
        fogColorParam_      = params["FogColor"];
        fogVectorParam_     = params["FogVector"];
        worldViewProjParam_ = params["WorldViewProj"];
        shaderIndexParam_   = params["ShaderIndex"];
    }

    // IEffectMatrices
    Matrix AlphaTestEffect::getWorldProperty() const      { return world_; }
    void   AlphaTestEffect::setWorldProperty(const Matrix& v)
    {
        world_ = v;
        dirtyFlags_ |= DirtyWorldViewProj | DirtyFog;
    }

    Matrix AlphaTestEffect::getViewProperty() const       { return view_; }
    void   AlphaTestEffect::setViewProperty(const Matrix& v)
    {
        view_ = v;
        dirtyFlags_ |= DirtyWorldViewProj | DirtyFog;
    }

    Matrix AlphaTestEffect::getProjectionProperty() const { return projection_; }
    void   AlphaTestEffect::setProjectionProperty(const Matrix& v)
    {
        projection_ = v;
        dirtyFlags_ |= DirtyWorldViewProj;
    }

    Vector3 AlphaTestEffect::getDiffuseColorProperty() const  { return diffuseColor_; }
    void    AlphaTestEffect::setDiffuseColorProperty(const Vector3& v)
    {
        diffuseColor_ = v;
        dirtyFlags_ |= DirtyMaterialColor;
    }

    float AlphaTestEffect::getAlphaProperty() const     { return alpha_; }
    void  AlphaTestEffect::setAlphaProperty(float v)
    {
        alpha_ = v;
        dirtyFlags_ |= DirtyMaterialColor;
    }

    // IEffectFog
    Vector3 AlphaTestEffect::getFogColorProperty() const
    {
        return fogColorParam_ ? fogColorParam_->GetValueVector3() : Vector3::Zero;
    }
    void AlphaTestEffect::setFogColorProperty(const Vector3& v)
    {
        if (fogColorParam_) fogColorParam_->SetValue(v);
    }

    bool AlphaTestEffect::getFogEnabledProperty() const { return fogEnabled_; }
    void AlphaTestEffect::setFogEnabledProperty(bool v)
    {
        if (fogEnabled_ != v)
        {
            fogEnabled_ = v;
            dirtyFlags_ |= DirtyShaderIndex | DirtyFogEnable;
        }
    }

    float AlphaTestEffect::getFogStartProperty() const { return fogStart_; }
    void  AlphaTestEffect::setFogStartProperty(float v)
    {
        fogStart_ = v;
        dirtyFlags_ |= DirtyFog;
    }

    float AlphaTestEffect::getFogEndProperty() const { return fogEnd_; }
    void  AlphaTestEffect::setFogEndProperty(float v)
    {
        fogEnd_ = v;
        dirtyFlags_ |= DirtyFog;
    }

    Texture2D* AlphaTestEffect::getTextureProperty() const { return texture_; }
    void       AlphaTestEffect::setTextureProperty(Texture2D* v) { texture_ = v; }
    void AlphaTestEffect::SetOwnedTexture(std::shared_ptr<Texture2D> texture)
    {
        ownedTexture_ = std::move(texture);
        texture_ = ownedTexture_.get();
    }

    bool AlphaTestEffect::getVertexColorEnabledProperty() const { return vertexColorEnabled_; }
    void AlphaTestEffect::setVertexColorEnabledProperty(bool v)
    {
        if (vertexColorEnabled_ != v)
        {
            vertexColorEnabled_ = v;
            dirtyFlags_ |= DirtyShaderIndex;
        }
    }

    CompareFunction AlphaTestEffect::getAlphaFunctionProperty() const { return alphaFunction_; }
    void AlphaTestEffect::setAlphaFunctionProperty(CompareFunction v)
    {
        alphaFunction_ = v;
        dirtyFlags_ |= DirtyAlphaTest;
    }

    int  AlphaTestEffect::getReferenceAlphaProperty() const { return referenceAlpha_; }
    void AlphaTestEffect::setReferenceAlphaProperty(int v)
    {
        referenceAlpha_ = v;
        dirtyFlags_ |= DirtyAlphaTest;
    }

    void AlphaTestEffect::OnApply()
    {
        // Recompute world*view*projection and fog vector
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

        // Alpha test parameters
        if ((dirtyFlags_ & DirtyAlphaTest) != 0)
        {
            Vector4 alphaTest{0.0f, 0.0f, 0.0f, 0.0f};
            bool eqNe = false;

            const float reference = static_cast<float>(referenceAlpha_) / 255.0f;
            constexpr float threshold = 0.5f / 255.0f;

            switch (alphaFunction_)
            {
                case CompareFunction::Less:
                    alphaTest.X = reference - threshold;
                    alphaTest.Z = 1.0f; alphaTest.W = -1.0f;
                    break;
                case CompareFunction::LessEqual:
                    alphaTest.X = reference + threshold;
                    alphaTest.Z = 1.0f; alphaTest.W = -1.0f;
                    break;
                case CompareFunction::GreaterEqual:
                    alphaTest.X = reference - threshold;
                    alphaTest.Z = -1.0f; alphaTest.W = 1.0f;
                    break;
                case CompareFunction::Greater:
                    alphaTest.X = reference + threshold;
                    alphaTest.Z = -1.0f; alphaTest.W = 1.0f;
                    break;
                case CompareFunction::Equal:
                    alphaTest.X = reference;
                    alphaTest.Y = threshold;
                    alphaTest.Z = 1.0f; alphaTest.W = -1.0f;
                    eqNe = true;
                    break;
                case CompareFunction::NotEqual:
                    alphaTest.X = reference;
                    alphaTest.Y = threshold;
                    alphaTest.Z = -1.0f; alphaTest.W = 1.0f;
                    eqNe = true;
                    break;
                case CompareFunction::Never:
                    alphaTest.Z = -1.0f; alphaTest.W = -1.0f;
                    break;
                case CompareFunction::Always:
                default:
                    alphaTest.Z = 1.0f; alphaTest.W = 1.0f;
                    break;
            }

            if (alphaTestParam_) alphaTestParam_->SetValue(alphaTest);
            dirtyFlags_ &= ~DirtyAlphaTest;

            if (isEqNe_ != eqNe)
            {
                isEqNe_ = eqNe;
                dirtyFlags_ |= DirtyShaderIndex;
            }
        }

        // Shader index (variant selection)
        if ((dirtyFlags_ & DirtyShaderIndex) != 0)
        {
            int shaderIndex = 0;
            if (!fogEnabled_)        shaderIndex += 1;
            if (vertexColorEnabled_) shaderIndex += 2;
            if (isEqNe_)             shaderIndex += 4;

            if (shaderIndexParam_) shaderIndexParam_->SetValue(shaderIndex);
            dirtyFlags_ &= ~DirtyShaderIndex;
        }
    }

    void AlphaTestEffect::FillGpuDrawParams(CNA::Internal::Renderers::GpuDrawParams& p) const
    {
        using namespace CNA::Internal::Renderers;

        p.alphaTestEffect   = true;
        p.textureEnabled     = (texture_ != nullptr);
        p.vertexColorEnabled = vertexColorEnabled_;
        p.lightingEnabled    = false;

        if (p.textureEnabled)
            p.texture0 = &texture_->GetRenderer();

        p.diffuseColor[0] = diffuseColor_.X * alpha_;
        p.diffuseColor[1] = diffuseColor_.Y * alpha_;
        p.diffuseColor[2] = diffuseColor_.Z * alpha_;
        p.diffuseColor[3] = alpha_;

        const Vector3 fogColor = getFogColorProperty();
        p.fogEnabled  = fogEnabled_;
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

        world_.ToColumnMajor(p.worldColMajor);

        // Compute alphaTest vec4 from alphaFunction + referenceAlpha.
        // Shader evaluates: if ((y>0) ? (|a-x|<y) : (a<x)) ? z : w < 0 → discard.
        const float reference  = static_cast<float>(referenceAlpha_) / 255.0f;
        constexpr float kThresh = 0.5f / 255.0f;

        p.alphaTest[0] = 0.0f; p.alphaTest[1] = 0.0f;
        p.alphaTest[2] = 1.0f; p.alphaTest[3] = 1.0f;   // default: Always

        switch (alphaFunction_)
        {
            case CompareFunction::Less:
                p.alphaTest[0] = reference - kThresh;
                p.alphaTest[2] = 1.0f;  p.alphaTest[3] = -1.0f;
                break;
            case CompareFunction::LessEqual:
                p.alphaTest[0] = reference + kThresh;
                p.alphaTest[2] = 1.0f;  p.alphaTest[3] = -1.0f;
                break;
            case CompareFunction::GreaterEqual:
                p.alphaTest[0] = reference - kThresh;
                p.alphaTest[2] = -1.0f; p.alphaTest[3] = 1.0f;
                break;
            case CompareFunction::Greater:
                p.alphaTest[0] = reference + kThresh;
                p.alphaTest[2] = -1.0f; p.alphaTest[3] = 1.0f;
                break;
            case CompareFunction::Equal:
                p.alphaTest[0] = reference; p.alphaTest[1] = kThresh;
                p.alphaTest[2] = 1.0f;  p.alphaTest[3] = -1.0f;
                break;
            case CompareFunction::NotEqual:
                p.alphaTest[0] = reference; p.alphaTest[1] = kThresh;
                p.alphaTest[2] = -1.0f; p.alphaTest[3] = 1.0f;
                break;
            case CompareFunction::Never:
                p.alphaTest[2] = -1.0f; p.alphaTest[3] = -1.0f;
                break;
            case CompareFunction::Always:
            default:
                break; // already {0,0,1,1}
        }
    }

    const std::string& AlphaTestEffect::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.AlphaTestEffect";
        return name;
    }
}
