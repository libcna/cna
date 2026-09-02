// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include <cmath>
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterType.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    namespace
    {
        void AddParam(EffectParameterCollection& params, const std::string& name,
                      int rows, int columns,
                      EffectParameterClass parameterClass, EffectParameterType parameterType)
        {
            params.Add(EffectParameter(
                name, "", rows, columns, parameterClass, parameterType));
        }
    }

    BasicEffect::BasicEffect(GraphicsDevice& device)
        : Effect(device, "BasicEffect")
    {
        CacheEffectParameters();
        DirectionalLight0.setEnabledProperty(true);
        setSpecularColorProperty(Vector3::One);
        setSpecularPowerProperty(16.0f);
        setFogColorProperty(Vector3::Zero);
        setTextureProperty(nullptr);
    }

    BasicEffect::BasicEffect(const BasicEffect& cloneSource)
        : Effect(*cloneSource.device_, "BasicEffect")
        , World(cloneSource.World)
        , View(cloneSource.View)
        , Projection(cloneSource.Projection)
        , VertexColorEnabled(cloneSource.VertexColorEnabled)
        , DirectionalLight0(cloneSource.DirectionalLight0)
        , DirectionalLight1(cloneSource.DirectionalLight1)
        , DirectionalLight2(cloneSource.DirectionalLight2)
        , diffuseColor_(cloneSource.diffuseColor_)
        , emissiveColor_(cloneSource.emissiveColor_)
        , specularColor_(cloneSource.specularColor_)
        , specularPower_(cloneSource.specularPower_)
        , ambientLightColor_(cloneSource.ambientLightColor_)
        , alpha_(cloneSource.alpha_)
        , lightingEnabled_(cloneSource.lightingEnabled_)
        , preferPerPixelLighting_(cloneSource.preferPerPixelLighting_)
        , textureEnabled_(cloneSource.textureEnabled_)
        , texture_(cloneSource.texture_)
        , ownedTexture_(cloneSource.ownedTexture_)
        , fogEnabled_(cloneSource.fogEnabled_)
        , fogColor_(cloneSource.fogColor_)
        , fogStart_(cloneSource.fogStart_)
        , fogEnd_(cloneSource.fogEnd_)
    {
        CacheEffectParameters();
        setSpecularColorProperty(cloneSource.getSpecularColorProperty());
        setSpecularPowerProperty(cloneSource.getSpecularPowerProperty());
        setFogColorProperty(cloneSource.getFogColorProperty());
        setTextureProperty(cloneSource.getTextureProperty());
    }

    void BasicEffect::CacheEffectParameters()
    {
        auto& params = getParametersProperty();
        AddParam(params, "Texture", 0, 0,
                 EffectParameterClass::Object, EffectParameterType::Texture2D);
        AddParam(params, "DiffuseColor", 1, 4, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "EmissiveColor", 1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "SpecularColor", 1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "SpecularPower", 1, 1, EffectParameterClass::Scalar, EffectParameterType::Single);
        AddParam(params, "DirLight0Direction", 1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "DirLight0DiffuseColor", 1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "DirLight0SpecularColor", 1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "DirLight1Direction", 1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "DirLight1DiffuseColor", 1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "DirLight1SpecularColor", 1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "DirLight2Direction", 1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "DirLight2DiffuseColor", 1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "DirLight2SpecularColor", 1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "EyePosition", 1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "FogColor", 1, 3, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "FogVector", 1, 4, EffectParameterClass::Vector, EffectParameterType::Single);
        AddParam(params, "World", 4, 4, EffectParameterClass::Matrix, EffectParameterType::Single);
        AddParam(params, "WorldInverseTranspose", 3, 3, EffectParameterClass::Matrix, EffectParameterType::Single);
        AddParam(params, "WorldViewProj", 4, 4, EffectParameterClass::Matrix, EffectParameterType::Single);
        AddParam(params, "ShaderIndex", 1, 1, EffectParameterClass::Scalar, EffectParameterType::Int32);

        textureParam_ = params["Texture"];
        diffuseColorParam_ = params["DiffuseColor"];
        emissiveColorParam_ = params["EmissiveColor"];
        specularColorParam_ = params["SpecularColor"];
        specularPowerParam_ = params["SpecularPower"];
        dirLight0DirectionParam_ = params["DirLight0Direction"];
        dirLight0DiffuseColorParam_ = params["DirLight0DiffuseColor"];
        dirLight0SpecularColorParam_ = params["DirLight0SpecularColor"];
        dirLight1DirectionParam_ = params["DirLight1Direction"];
        dirLight1DiffuseColorParam_ = params["DirLight1DiffuseColor"];
        dirLight1SpecularColorParam_ = params["DirLight1SpecularColor"];
        dirLight2DirectionParam_ = params["DirLight2Direction"];
        dirLight2DiffuseColorParam_ = params["DirLight2DiffuseColor"];
        dirLight2SpecularColorParam_ = params["DirLight2SpecularColor"];
        eyePositionParam_ = params["EyePosition"];
        fogColorParam_ = params["FogColor"];
        fogVectorParam_ = params["FogVector"];
        worldParam_ = params["World"];
        worldInverseTransposeParam_ = params["WorldInverseTranspose"];
        worldViewProjParam_ = params["WorldViewProj"];
        shaderIndexParam_ = params["ShaderIndex"];
    }

    Effect* BasicEffect::Clone()
    {
        return new BasicEffect(*this);
    }

    void BasicEffect::OnApply()
    {
        Matrix worldView;
        Matrix worldViewProjection;
        Matrix::Multiply(World, View, worldView);
        Matrix::Multiply(worldView, Projection, worldViewProjection);
        worldViewProjParam_->SetValue(worldViewProjection);

        if (!fogEnabled_)
        {
            fogVectorParam_->SetValue(Vector4::Zero);
        }
        else if (fogStart_ == fogEnd_)
        {
            fogVectorParam_->SetValue(Vector4{0.0f, 0.0f, 0.0f, 1.0f});
        }
        else
        {
            const float scale = 1.0f / (fogStart_ - fogEnd_);
            fogVectorParam_->SetValue(Vector4{
                worldView.M13 * scale,
                worldView.M23 * scale,
                worldView.M33 * scale,
                (worldView.M43 + fogStart_) * scale});
        }

        if (lightingEnabled_)
        {
            diffuseColorParam_->SetValue(Vector4{
                diffuseColor_.X * alpha_,
                diffuseColor_.Y * alpha_,
                diffuseColor_.Z * alpha_,
                alpha_});
            emissiveColorParam_->SetValue(Vector3{
                (emissiveColor_.X + ambientLightColor_.X * diffuseColor_.X) * alpha_,
                (emissiveColor_.Y + ambientLightColor_.Y * diffuseColor_.Y) * alpha_,
                (emissiveColor_.Z + ambientLightColor_.Z * diffuseColor_.Z) * alpha_});

            worldParam_->SetValue(World);
            Matrix inverseWorld;
            Matrix inverseTransposeWorld;
            Matrix::Invert(World, inverseWorld);
            Matrix::Transpose(inverseWorld, inverseTransposeWorld);
            worldInverseTransposeParam_->SetValue(inverseTransposeWorld);

            const Matrix inverseView = Matrix::Invert(View);
            eyePositionParam_->SetValue(inverseView.getTranslationProperty());
        }
        else
        {
            diffuseColorParam_->SetValue(Vector4{
                (diffuseColor_.X + emissiveColor_.X) * alpha_,
                (diffuseColor_.Y + emissiveColor_.Y) * alpha_,
                (diffuseColor_.Z + emissiveColor_.Z) * alpha_,
                alpha_});
        }

        const auto setLightParameters = [](
            const DirectionalLight& light,
            EffectParameter& direction,
            EffectParameter& diffuse,
            EffectParameter& specular)
        {
            direction.SetValue(light.getDirectionProperty());
            diffuse.SetValue(light.getEnabledProperty()
                ? light.getDiffuseColorProperty()
                : Vector3::Zero);
            specular.SetValue(light.getEnabledProperty()
                ? light.getSpecularColorProperty()
                : Vector3::Zero);
        };
        setLightParameters(DirectionalLight0, *dirLight0DirectionParam_,
                           *dirLight0DiffuseColorParam_, *dirLight0SpecularColorParam_);
        setLightParameters(DirectionalLight1, *dirLight1DirectionParam_,
                           *dirLight1DiffuseColorParam_, *dirLight1SpecularColorParam_);
        setLightParameters(DirectionalLight2, *dirLight2DirectionParam_,
                           *dirLight2DiffuseColorParam_, *dirLight2SpecularColorParam_);

        int shaderIndex = 0;
        if (!fogEnabled_) shaderIndex += 1;
        if (VertexColorEnabled) shaderIndex += 2;
        if (textureEnabled_) shaderIndex += 4;
        if (lightingEnabled_)
        {
            if (preferPerPixelLighting_)
                shaderIndex += 24;
            else if (!DirectionalLight1.getEnabledProperty() && !DirectionalLight2.getEnabledProperty())
                shaderIndex += 16;
            else
                shaderIndex += 8;
        }
        shaderIndexParam_->SetValue(shaderIndex);
    }

    void BasicEffect::FillGpuDrawParams(CNA::Internal::Renderers::GpuDrawParams& p) const
    {
        using namespace CNA::Internal::Renderers;

        // MOD-821: carried to the renderer whether or not it has a shadow-sampling variant --
        // the same accepted-and-ignored convention the PBR fields use.
        p.shadowsEnabled  = shadowsEnabledEXT_ && shadowMapEXT_ != nullptr;
        p.shadowDepthBias = shadowDepthBiasEXT_;
        p.shadowPcfRadius = shadowFilterRadiusEXT_;
        if (punctualLightEXT_.Kind != PunctualLightKindEXT::None)
        {
            // MOD-1005. The cosines are precomputed here rather than in the shader: a cone test
            // needs cos(angle), and six transcendental calls per fragment to recover what the CPU
            // already knows is a poor trade.
            p.punctualKind = punctualLightEXT_.Kind == PunctualLightKindEXT::Point ? 1 : 2;
            p.punctualPosition[0] = punctualLightEXT_.Position.X;
            p.punctualPosition[1] = punctualLightEXT_.Position.Y;
            p.punctualPosition[2] = punctualLightEXT_.Position.Z;
            p.punctualDirection[0] = punctualLightEXT_.Direction.X;
            p.punctualDirection[1] = punctualLightEXT_.Direction.Y;
            p.punctualDirection[2] = punctualLightEXT_.Direction.Z;
            p.punctualDiffuse[0] = punctualLightEXT_.DiffuseColor.X;
            p.punctualDiffuse[1] = punctualLightEXT_.DiffuseColor.Y;
            p.punctualDiffuse[2] = punctualLightEXT_.DiffuseColor.Z;
            p.punctualRange      = punctualLightEXT_.Range;
            p.punctualCosInner   = std::cos(punctualLightEXT_.InnerAngle);
            p.punctualCosOuter   = std::cos(punctualLightEXT_.OuterAngle);
            p.punctualShadowBias = punctualLightEXT_.ShadowDepthBias;
            if (punctualLightEXT_.Kind == PunctualLightKindEXT::Point &&
                punctualLightEXT_.ShadowCube != nullptr)
            {
                p.punctualShadowCube = &punctualLightEXT_.ShadowCube->GetRenderer();
            }
            else if (punctualLightEXT_.Kind == PunctualLightKindEXT::Spot &&
                     punctualLightEXT_.ShadowMap != nullptr)
            {
                p.punctualShadowMap = &punctualLightEXT_.ShadowMap->GetRenderer();
                const float* m = &punctualLightEXT_.ShadowViewProjection.M11;
                for (int i = 0; i < 16; ++i) p.punctualViewProjColMajor[i] = m[i];
            }
        }
        if (p.shadowsEnabled && shadowCascadesEXT_.Count > 0)
        {
            // MOD-908: the cascade matrices replace the single light matrix rather than joining
            // it -- a receiver reads one or the other, never both, so leaving a stale
            // lightViewProjColMajor behind would be harmless but misleading to anyone reading it.
            p.cascadeCount = shadowCascadesEXT_.Count;
            for (int c = 0; c < shadowCascadesEXT_.Count; ++c)
            {
                const float* m = &shadowCascadesEXT_.WorldToAtlas[c].M11;
                for (int i = 0; i < 16; ++i) p.cascadeMatricesColMajor[c * 16 + i] = m[i];
                p.cascadeSplits[c] = shadowCascadesEXT_.SplitDistance[c];
            }
            // The view matrix's third column: dotting a world position with it gives view-space Z,
            // whose negation is the depth the splits are expressed in.
            p.cascadeViewZRow[0] = shadowCascadesEXT_.CameraView.M13;
            p.cascadeViewZRow[1] = shadowCascadesEXT_.CameraView.M23;
            p.cascadeViewZRow[2] = shadowCascadesEXT_.CameraView.M33;
            p.cascadeViewZRow[3] = shadowCascadesEXT_.CameraView.M43;
            p.cascadeBlendBand = shadowCascadesEXT_.BlendBand;
            p.cascadeDebugTint = shadowCascadesEXT_.DebugTint;
        }
        if (p.shadowsEnabled)
        {
            p.shadowMap = &shadowMapEXT_->GetRenderer();
            const float* m = &lightViewProjectionEXT_.M11;
            for (int i = 0; i < 16; ++i) p.lightViewProjColMajor[i] = m[i];
        }

        p.textureEnabled     = textureEnabled_;
        p.vertexColorEnabled = VertexColorEnabled;
        p.lightingEnabled    = lightingEnabled_;
        p.preferPerPixelLighting = preferPerPixelLighting_;

        if (p.textureEnabled)
        {
            if (Texture2D* texture = getTextureProperty()) p.texture0 = &texture->GetRenderer();
        }

        // FNA's EffectHelpers.SetMaterialColor: when lighting is disabled, ambient/directional
        // lights are never computed at all, so EmissiveColor has to be baked directly into the
        // forwarded diffuse color (DiffuseColor+EmissiveColor)*Alpha — otherwise it would be
        // silently dropped, since no lit shader path runs to add it separately. When lighting is
        // enabled, EmissiveColor is added on the lit path instead (see p.emissiveColor below), so
        // DiffuseColor stays a plain DiffuseColor*Alpha here.
        const Vector3 forwardedDiffuse = lightingEnabled_ ? diffuseColor_ : (diffuseColor_ + emissiveColor_);
        p.diffuseColor[0] = forwardedDiffuse.X * alpha_;
        p.diffuseColor[1] = forwardedDiffuse.Y * alpha_;
        p.diffuseColor[2] = forwardedDiffuse.Z * alpha_;
        p.diffuseColor[3] = alpha_;

        p.ambientColor[0] = ambientLightColor_.X;
        p.ambientColor[1] = ambientLightColor_.Y;
        p.ambientColor[2] = ambientLightColor_.Z;

        // FNA's DirectionalLight.Enabled setter zeroes the GPU-facing diffuse/specular parameters
        // when disabled (DirectionalLight.cs), regardless of what DiffuseColor is still set to at
        // the C# property level. CNA's DirectionalLight has no such side effect, so the gating
        // must happen here.
        const bool    light0On = DirectionalLight0.getEnabledProperty();
        const Vector3 ld  = light0On ? DirectionalLight0.getDiffuseColorProperty() : Vector3::Zero;
        const Vector3 ls  = light0On ? DirectionalLight0.getSpecularColorProperty() : Vector3::Zero;
        const Vector3 dir = DirectionalLight0.getDirectionProperty();
        p.light0Dir[0]     = dir.X; p.light0Dir[1]     = dir.Y; p.light0Dir[2]     = dir.Z;
        p.light0Diffuse[0] = ld.X;  p.light0Diffuse[1] = ld.Y;  p.light0Diffuse[2] = ld.Z;
        p.light0Specular[0]= ls.X;  p.light0Specular[1]= ls.Y;  p.light0Specular[2]= ls.Z;

        const bool    light1On = DirectionalLight1.getEnabledProperty();
        const Vector3 ld1  = light1On ? DirectionalLight1.getDiffuseColorProperty() : Vector3::Zero;
        const Vector3 ls1  = light1On ? DirectionalLight1.getSpecularColorProperty() : Vector3::Zero;
        const Vector3 dir1 = DirectionalLight1.getDirectionProperty();
        p.light1Dir[0]     = dir1.X; p.light1Dir[1]     = dir1.Y; p.light1Dir[2]     = dir1.Z;
        p.light1Diffuse[0] = ld1.X;  p.light1Diffuse[1] = ld1.Y;  p.light1Diffuse[2] = ld1.Z;
        p.light1Specular[0]= ls1.X;  p.light1Specular[1]= ls1.Y;  p.light1Specular[2]= ls1.Z;

        const bool    light2On = DirectionalLight2.getEnabledProperty();
        const Vector3 ld2  = light2On ? DirectionalLight2.getDiffuseColorProperty() : Vector3::Zero;
        const Vector3 ls2  = light2On ? DirectionalLight2.getSpecularColorProperty() : Vector3::Zero;
        const Vector3 dir2 = DirectionalLight2.getDirectionProperty();
        p.light2Dir[0]     = dir2.X; p.light2Dir[1]     = dir2.Y; p.light2Dir[2]     = dir2.Z;
        p.light2Diffuse[0] = ld2.X;  p.light2Diffuse[1] = ld2.Y;  p.light2Diffuse[2] = ld2.Z;
        p.light2Specular[0]= ls2.X;  p.light2Specular[1]= ls2.Y;  p.light2Specular[2]= ls2.Z;

        // Lit path only: EmissiveColor is added after the ambient/light sum is multiplied by
        // DiffuseColor (see each renderer's lit shader formula) — the disabled-lighting path
        // already bakes EmissiveColor into the forwarded diffuse color above instead. Specular is
        // likewise lit-path only: FNA's Lighting.fxh only ever computes it inside the lit branch,
        // and the material SpecularColor is applied once to the summed per-light contribution
        // (not per-light, unlike each light's own SpecularColor which enters the sum individually).
        if (lightingEnabled_)
        {
            p.emissiveColor[0] = emissiveColor_.X * alpha_;
            p.emissiveColor[1] = emissiveColor_.Y * alpha_;
            p.emissiveColor[2] = emissiveColor_.Z * alpha_;

            const Vector3 specularColor = getSpecularColorProperty();
            p.specularColor[0] = specularColor.X;
            p.specularColor[1] = specularColor.Y;
            p.specularColor[2] = specularColor.Z;
            p.specularPower     = getSpecularPowerProperty();

            const Matrix  viewInverse = Matrix::Invert(View);
            const Vector3 eyePos      = viewInverse.getTranslationProperty();
            p.eyePositionWorld[0] = eyePos.X;
            p.eyePositionWorld[1] = eyePos.Y;
            p.eyePositionWorld[2] = eyePos.Z;
        }

        World.ToColumnMajor(p.worldColMajor);

        p.fogEnabled   = fogEnabled_;
        const Vector3 fogColor = getFogColorProperty();
        p.fogColor[0]  = fogColor.X;
        p.fogColor[1]  = fogColor.Y;
        p.fogColor[2]  = fogColor.Z;
        // REMED-GFX-010: FNA EffectHelpers.SetFogVector. Fog is a view-space Z term computed by
        // dotting this vector with the object-space vertex position in the shader; the vector bakes
        // the third column of World*View. Matches OnApply()'s fogVectorParam path exactly. Zero when
        // disabled (no-op) or {0,0,0,1} for the fogStart==fogEnd degenerate case (fully fogged).
        Matrix fogWorldView; Matrix::Multiply(World, View, fogWorldView);
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

    // IEffectLights
    Vector3 BasicEffect::getAmbientLightColorProperty() const { return ambientLightColor_; }
    void BasicEffect::setAmbientLightColorProperty(const Vector3& v) { ambientLightColor_ = v; }
    bool BasicEffect::getLightingEnabledProperty() const { return lightingEnabled_; }
    void BasicEffect::setLightingEnabledProperty(bool v) { lightingEnabled_ = v; }
    DirectionalLight& BasicEffect::getDirectionalLight0Property() { return DirectionalLight0; }
    DirectionalLight& BasicEffect::getDirectionalLight1Property() { return DirectionalLight1; }
    DirectionalLight& BasicEffect::getDirectionalLight2Property() { return DirectionalLight2; }

    bool BasicEffect::getPreferPerPixelLightingProperty() const { return preferPerPixelLighting_; }
    void BasicEffect::setPreferPerPixelLightingProperty(bool v) { preferPerPixelLighting_ = v; }

    Vector3 BasicEffect::getDiffuseColorProperty() const { return diffuseColor_; }
    void BasicEffect::setDiffuseColorProperty(const Vector3& v) { diffuseColor_ = v; }
    Vector3 BasicEffect::getEmissiveColorProperty() const { return emissiveColor_; }
    void BasicEffect::setEmissiveColorProperty(const Vector3& v) { emissiveColor_ = v; }
    Vector3 BasicEffect::getSpecularColorProperty() const
    {
        return specularColorParam_ ? specularColorParam_->GetValueVector3() : specularColor_;
    }
    void BasicEffect::setSpecularColorProperty(const Vector3& v)
    {
        specularColor_ = v;
        if (specularColorParam_) specularColorParam_->SetValue(v);
    }
    float BasicEffect::getSpecularPowerProperty() const
    {
        return specularPowerParam_ ? specularPowerParam_->GetValueSingle() : specularPower_;
    }
    void BasicEffect::setSpecularPowerProperty(float v)
    {
        specularPower_ = v;
        if (specularPowerParam_) specularPowerParam_->SetValue(v);
    }
    float BasicEffect::getAlphaProperty() const { return alpha_; }
    void BasicEffect::setAlphaProperty(float v) { alpha_ = v; }

    bool BasicEffect::getTextureEnabledProperty() const { return textureEnabled_; }
    void BasicEffect::setTextureEnabledProperty(bool v) { textureEnabled_ = v; }
    Texture2D* BasicEffect::getTextureProperty() const
    {
        return textureParam_ ? textureParam_->GetValueTexture2D() : texture_;
    }
    void BasicEffect::setTextureProperty(Texture2D* v)
    {
        texture_ = v;
        if (textureParam_) textureParam_->SetValue(v);
    }
    void BasicEffect::SetOwnedTexture(std::shared_ptr<Texture2D> texture)
    {
        ownedTexture_ = std::move(texture);
        setTextureProperty(ownedTexture_.get());
    }

    // IEffectFog
    Vector3 BasicEffect::getFogColorProperty() const
    {
        return fogColorParam_ ? fogColorParam_->GetValueVector3() : fogColor_;
    }
    void BasicEffect::setFogColorProperty(const Vector3& v)
    {
        fogColor_ = v;
        if (fogColorParam_) fogColorParam_->SetValue(v);
    }
    bool BasicEffect::getFogEnabledProperty() const { return fogEnabled_; }
    void BasicEffect::setFogEnabledProperty(bool v) { fogEnabled_ = v; }
    float BasicEffect::getFogStartProperty() const { return fogStart_; }
    void BasicEffect::setFogStartProperty(float v) { fogStart_ = v; }
    float BasicEffect::getFogEndProperty() const { return fogEnd_; }
    void BasicEffect::setFogEndProperty(float v) { fogEnd_ = v; }

    void BasicEffect::EnableDefaultLighting()
    {
        lightingEnabled_ = true;
        ambientLightColor_ = Vector3{0.05333332f, 0.09882354f, 0.1819608f};

        DirectionalLight0.setDiffuseColorProperty(Vector3{1.0f, 0.9607844f, 0.8078432f});
        DirectionalLight0.setDirectionProperty(Vector3{-0.5265408f, -0.5735765f, -0.6275069f});
        DirectionalLight0.setSpecularColorProperty(Vector3{1.0f, 0.9607844f, 0.8078432f});
        DirectionalLight0.setEnabledProperty(true);

        DirectionalLight1.setDiffuseColorProperty(Vector3{0.9647059f, 0.7607844f, 0.4078432f});
        DirectionalLight1.setDirectionProperty(Vector3{0.7198464f, 0.3420201f, 0.6040227f});
        DirectionalLight1.setSpecularColorProperty(Vector3::Zero);
        DirectionalLight1.setEnabledProperty(true);

        DirectionalLight2.setDiffuseColorProperty(Vector3{0.3231373f, 0.3607844f, 0.3937255f});
        DirectionalLight2.setDirectionProperty(Vector3{0.4545195f, -0.7660444f, 0.4545195f});
        DirectionalLight2.setSpecularColorProperty(Vector3{0.3231373f, 0.3607844f, 0.3937255f});
        DirectionalLight2.setEnabledProperty(true);
    }

    const std::string& BasicEffect::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.BasicEffect";
        return name;
    }

    void BasicEffect::setShadowMapEXT(Texture2D* shadowMap) { shadowMapEXT_ = shadowMap; }

    Texture2D* BasicEffect::getShadowMapEXT() const { return shadowMapEXT_; }

    void BasicEffect::setLightViewProjectionEXT(const Matrix& lightViewProjection)
    {
        lightViewProjectionEXT_ = lightViewProjection;
    }

    Matrix BasicEffect::getLightViewProjectionEXT() const { return lightViewProjectionEXT_; }

    void BasicEffect::setShadowsEnabledEXT(bool enabled) { shadowsEnabledEXT_ = enabled; }

    bool BasicEffect::isShadowsEnabledEXT() const { return shadowsEnabledEXT_; }

    void BasicEffect::setShadowDepthBiasEXT(float bias) { shadowDepthBiasEXT_ = bias; }

    float BasicEffect::getShadowDepthBiasEXT() const { return shadowDepthBiasEXT_; }

    void BasicEffect::setShadowFilterRadiusEXT(int radius) { shadowFilterRadiusEXT_ = radius; }

    int BasicEffect::getShadowFilterRadiusEXT() const { return shadowFilterRadiusEXT_; }

    void BasicEffect::setShadowCascadesEXT(const ShadowCascadeStateEXT& state)
    {
        shadowCascadesEXT_ = state;
    }

    const ShadowCascadeStateEXT& BasicEffect::getShadowCascadesEXT() const
    {
        return shadowCascadesEXT_;
    }

    void BasicEffect::setPunctualLightEXT(const PunctualLightEXT& light)
    {
        punctualLightEXT_ = light;
    }

    const PunctualLightEXT& BasicEffect::getPunctualLightEXT() const
    {
        return punctualLightEXT_;
    }
}
