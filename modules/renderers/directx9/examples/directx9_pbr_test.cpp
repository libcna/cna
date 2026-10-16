// SPDX-License-Identifier: MS-PL
// D3D9 PBR porting task: real DrawPrimitivesEx dispatch for CNA's own CNAEXT PbrEffect/
// SkinnedPbrEffect (params.pbr), via CNA's own custom "Pbr3D"/"PbrSkinned3D" vs_3_0/ps_3_0 shaders
// (D3D9PbrDraw.cpp, shaders/cna/Pbr3D.hlsl, shaders/cna/PbrSkinned3D.hlsl).
//
// Every expected pixel value below is hand-derived from Pbr3D.hlsl's/PbrSkinned3D.hlsl's own PS
// formula with all 3 lights' diffuse deliberately zeroed -- PbrLight()'s own return is
// `(...) * lightColor * NdotL`, so a zero lightColor makes that light's ENTIRE contribution exactly
// zero regardless of NdotL/roughness/metallic/normal-map content (no BRDF term needs hand-evaluating
// to get an exact expected value, mirroring directx9_drawex_test.cpp's own "SpecularColor=0 sidesteps
// needing to hand-compute a half-vector-dependent term" precedent). With Lo=0, the remaining formula
// collapses to `outColor.rgb = AmbientColor*albedo*occlusion + emissive`; AmbientColor=(1,1,1),
// occlusion=1 (no map bound -> white fallback), emissive=0 (EmissiveColor=(0,0,0)) reduces this
// further to exactly `albedo = texture.rgb * DiffuseColor.rgb`.
//
// Check A -- unskinned Pbr3D (stride 48), ambient-only: exact texture*DiffuseColor readback.
// Check B -- skinned PbrSkinned3D (stride 68), ambient-only, single Identity bone at 100% weight
//   (skinning is a deliberate no-op, mirrors directx9_drawex_test.cpp's own Check O precedent): the
//   SAME exact readback as Check A, proving the Bones[72] upload doesn't corrupt PBR output even in
//   the trivial identity case.
// Check C -- dispatch priority: params.pbr=true together with params.dualTexture=true (a flag
//   combination that would otherwise select DualTextureEffect) still renders via the PBR path --
//   proves DrawPrimitivesExImpl checks params.pbr BEFORE every Stock Effect flag, matching
//   EasyGLRenderer::SelectProgram()'s own identical priority.
// Check D -- unsupported stride (params.pbr=true, params.skinned=false, stride 20 -- no matching
//   CNA vertex layout for PbrEffect, which needs stride 48) throws a named error rather than
//   silently drawing wrong.
// Checks E-I -- the unshadowed PunctualLightEXT term: disabled, point, out-of-range point, spot
//   aimed at/away from the surface, and the same point light through the skinned shader.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.
//
// NOTE (2026-07): this file is written and cross-compile-checked (mingw) but has NOT been run --
// this development machine has no Windows/live D3D9 device/GPU driver to execute it against. See
// this task's own final report for the exact verification that WAS performed.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"

#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Textures.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "../src/shaders/d3d9_pbr_shaders.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::DirectX9;
using CNA::Internal::Renderers::GpuDrawParams;
using CNA::Internal::Renderers::ImageData;

namespace
{
    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        std::fflush(stdout);
        if (ok) ++passCount;
    }

    // stride 48: VertexPositionNormalTangentTexture (Position, Normal, Tangent(float4), TexCoord).
    struct VPNTanT { float x, y, z, nx, ny, nz, tx, ty, tz, tw, u, v; };
    const VPNTanT kTriPbr[3] = {
        {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
        { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
        {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
    };

    // stride 68: VertexPositionNormalTangentTextureSkinned -- kTriPbr's own 12 floats plus
    // BlendWeight(FLOAT4)/BlendIndices(UBYTE4), full weight on bone 0.
    struct VPNTanTW { float x, y, z, nx, ny, nz, tx, ty, tz, tw, u, v; float w0, w1, w2, w3; uint8_t idx[4]; };
    const VPNTanTW kTriPbrSkinned[3] = {
        {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, {0, 0, 0, 0}},
        { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, {0, 0, 0, 0}},
        {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, {0, 0, 0, 0}},
    };

    struct VPT { float x, y, z, u, v; }; // stride 20 -- used only for Check D's unsupported-stride throw.
    const VPT kTriWrongStride[3] = {
        {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f},
        { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f},
        {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f},
    };

    std::unique_ptr<CNA::Internal::Renderers::ITextureRenderer> Make1x1Texture(
        DirectX9Renderer& renderer, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        ImageData img;
        img.width = 1;
        img.height = 1;
        img.pixels = {r, g, b, a};
        return renderer.CreateTexture(img);
    }

    bool ReadCenterPixel(GraphicsDevice& dev, Color& out)
    {
        const Microsoft::Xna::Framework::Rectangle region(28, 28, 1, 1);
        std::vector<Color> pixels(1, Color(0, 0, 0, 0));
        dev.GetBackBufferData(&region, pixels.data(), 0, 1);
        out = pixels[0];
        return true;
    }

    // Common "ambient-only" GpuDrawParams shape shared by Checks A/B/C -- zeroes every light's
    // diffuse so Lo is exactly zero (see this file's own header comment).
    void SetAmbientOnlyPbrParams(GpuDrawParams& params, CNA::Internal::Renderers::ITextureRenderer* tex)
    {
        params.pbr = true;
        params.texture0 = tex;
        params.diffuseColor[0] = params.diffuseColor[1] = params.diffuseColor[2] = params.diffuseColor[3] = 1.0f;
        params.ambientColor[0] = params.ambientColor[1] = params.ambientColor[2] = 1.0f;
        params.emissiveColor[0] = params.emissiveColor[1] = params.emissiveColor[2] = 0.0f;
        params.light0Diffuse[0] = params.light0Diffuse[1] = params.light0Diffuse[2] = 0.0f;
        params.light1Diffuse[0] = params.light1Diffuse[1] = params.light1Diffuse[2] = 0.0f;
        params.light2Diffuse[0] = params.light2Diffuse[1] = params.light2Diffuse[2] = 0.0f;
        params.pbrMetallicFactor = 0.0f;
        params.pbrRoughnessFactor = 1.0f;
    }

    void SetDirectPbrParams(GpuDrawParams& params, CNA::Internal::Renderers::ITextureRenderer* tex)
    {
        SetAmbientOnlyPbrParams(params, tex);
        params.ambientColor[0] = params.ambientColor[1] = params.ambientColor[2] = 0.0f;
        params.eyePositionWorld[2] = 3.0f;
        params.light0Dir[0] = params.light0Dir[1] = 0.0f;
        params.light0Dir[2] = -1.0f;
        params.light0Diffuse[0] = params.light0Diffuse[1] = params.light0Diffuse[2] = 6.0f;
    }
}

class D3D9PbrTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<DirectX9Renderer&>(dev.GetRenderer());

        renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, CNA::Internal::Renderers::BlendWriteState{}); // default write state
        renderer.ApplyDepthStencilState(false, false, 0, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
        renderer.ApplyRasterizerState(0 /*CullMode::None*/, 0 /*FillMode::Solid*/, false, 0.0f, 0.0f);
        renderer.ApplySamplerState(0, 1 /*TextureFilter::Point*/, 0, 0, 1);

        auto tex = Make1x1Texture(renderer, 200, 120, 40);

        // Check A: unskinned Pbr3D, ambient-only -- exact texture*DiffuseColor.
        {
            auto vb = renderer.CreateVertexBuffer(3);
            vb->SetData(kTriPbr, 3, sizeof(VPNTanT));

            GpuDrawParams params;
            SetAmbientOnlyPbrParams(params, tex.get());
            params.skinned = false;

            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            Color px(0, 0, 0, 0);
            ReadCenterPixel(dev, px);
            check(px.getRProperty() == 200 && px.getGProperty() == 120 && px.getBProperty() == 40 && px.getAProperty() == 255,
                  "DrawPrimitivesEx (Pbr3D, unskinned, ambient-only): exact texture*DiffuseColor readback");
        }

        // Check B: skinned PbrSkinned3D, ambient-only, single Identity bone -- same exact readback.
        {
            auto vb = renderer.CreateVertexBuffer(3);
            vb->SetData(kTriPbrSkinned, 3, sizeof(VPNTanTW));
            auto ib = renderer.CreateIndexBuffer16(3);
            static const uint16_t kIdx[3] = {0, 1, 2};
            ib->SetData16(kIdx, 3);

            GpuDrawParams params;
            SetAmbientOnlyPbrParams(params, tex.get());
            params.skinned = true;
            params.weightsPerVertex = 1;
            params.boneCount = 1;
            // Identity bone: column-major identity 4x4.
            const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            for (int i = 0; i < 16; ++i) params.boneTransforms[i] = identity[i];

            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawIndexedPrimitivesEx(*vb, *ib, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
               Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            Color px(0, 0, 0, 0);
            ReadCenterPixel(dev, px);
            check(px.getRProperty() == 200 && px.getGProperty() == 120 && px.getBProperty() == 40 && px.getAProperty() == 255,
                  "DrawIndexedPrimitivesEx (PbrSkinned3D, Identity bone, ambient-only): exact "
                  "readback, Bones[72] upload doesn't corrupt PBR output");
        }

        // Check C: params.pbr takes priority over params.dualTexture.
        {
            auto vb = renderer.CreateVertexBuffer(3);
            vb->SetData(kTriPbr, 3, sizeof(VPNTanT));

            GpuDrawParams params;
            SetAmbientOnlyPbrParams(params, tex.get());
            params.skinned = false;
            params.dualTexture = true; // would select DualTextureEffect if params.pbr were not checked first

            dev.Clear(Color(0, 0, 255, 255));
            bool threw = false;
            try
            {
                renderer.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            }
            catch (const std::exception&) { threw = true; }
            Color px(0, 0, 0, 0);
            ReadCenterPixel(dev, px);
            check(!threw && px.getRProperty() == 200 && px.getGProperty() == 120 && px.getBProperty() == 40 && px.getAProperty() == 255,
                  "DrawPrimitivesEx: params.pbr=true takes priority over params.dualTexture=true "
                  "(would otherwise throw DualTextureEffect's own stride-48 layout error)");
        }

        // Check D: unsupported stride (20) for PbrEffect throws a named error.
        {
            auto vb = renderer.CreateVertexBuffer(3);
            vb->SetData(kTriWrongStride, 3, sizeof(VPT));

            GpuDrawParams params;
            params.pbr = true;
            params.skinned = false;

            dev.Clear(Color(0, 0, 255, 255));
            bool threw = false;
            try
            {
                renderer.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            }
            catch (const std::exception&) { threw = true; }
            check(threw, "DrawPrimitivesEx (PbrEffect): unsupported stride 20 throws a named error");
        }

        // Checks E-H: the rigid shader consumes the complete unshadowed point/spot contract.
        // Ambient, emissive and all directional lights are zero, so only the punctual term can
        // make the centre pixel non-black.
        {
            auto vb = renderer.CreateVertexBuffer(3);
            vb->SetData(kTriPbr, 3, sizeof(VPNTanT));
            const auto shadePunctual = [&](int kind, float range, float directionZ) {
                GpuDrawParams params;
                SetAmbientOnlyPbrParams(params, tex.get());
                params.ambientColor[0] = params.ambientColor[1] = params.ambientColor[2] = 0.0f;
                params.eyePositionWorld[2] = 3.0f;
                params.punctualKind = kind;
                params.punctualPosition[2] = 1.0f;
                params.punctualDirection[0] = 0.0f;
                params.punctualDirection[1] = 0.0f;
                params.punctualDirection[2] = directionZ;
                params.punctualDiffuse[0] = params.punctualDiffuse[1] =
                    params.punctualDiffuse[2] = 6.0f;
                params.punctualRange = range;
                params.punctualCosInner = 0.9396926f; // cos(20 degrees)
                params.punctualCosOuter = 0.7660444f; // cos(40 degrees)

                dev.Clear(Color::Black);
                renderer.DrawPrimitivesEx(
                    *vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                    Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
                Color px(0, 0, 0, 0);
                ReadCenterPixel(dev, px);
                return px.getRProperty();
            };

            const int none = shadePunctual(0, 10.0f, 0.0f);
            const int point = shadePunctual(1, 10.0f, 0.0f);
            const int outOfRange = shadePunctual(1, 0.5f, 0.0f);
            const int spotAt = shadePunctual(2, 10.0f, -1.0f);
            const int spotAway = shadePunctual(2, 10.0f, 1.0f);
            check(none < 10, "Pbr3D: disabled punctual light contributes nothing");
            check(point > 120, "Pbr3D: in-range point light illuminates the surface");
            check(outOfRange < 10, "Pbr3D: point light outside its range contributes nothing");
            std::printf("[INFO] Pbr3D spot centre red: aimed-at=%d aimed-away=%d\n",
                        spotAt, spotAway);
            check(spotAt > 120, "Pbr3D: spot aimed at the surface illuminates it");
            check(spotAway < 10, "Pbr3D: spot aimed away contributes nothing");
        }

        // Check I: the identical punctual constants and world-position/normal term reach the
        // skinned pixel shader after an identity bone transform.
        {
            auto vb = renderer.CreateVertexBuffer(3);
            vb->SetData(kTriPbrSkinned, 3, sizeof(VPNTanTW));
            auto ib = renderer.CreateIndexBuffer16(3);
            static const uint16_t kIdx[3] = {0, 1, 2};
            ib->SetData16(kIdx, 3);

            GpuDrawParams params;
            SetAmbientOnlyPbrParams(params, tex.get());
            params.ambientColor[0] = params.ambientColor[1] = params.ambientColor[2] = 0.0f;
            params.eyePositionWorld[2] = 3.0f;
            params.skinned = true;
            params.weightsPerVertex = 1;
            params.boneCount = 1;
            const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            for (int i = 0; i < 16; ++i) params.boneTransforms[i] = identity[i];
            params.punctualKind = 1;
            params.punctualPosition[2] = 1.0f;
            params.punctualDiffuse[0] = params.punctualDiffuse[1] =
                params.punctualDiffuse[2] = 6.0f;
            params.punctualRange = 10.0f;

            dev.Clear(Color::Black);
            renderer.DrawIndexedPrimitivesEx(
                *vb, *ib, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            Color px(0, 0, 0, 0);
            ReadCenterPixel(dev, px);
            check(px.getRProperty() > 120,
                  "PbrSkinned3D: point light survives identity-bone skinning");
        }

        // Checks J-N: the shadow bytecode samples the complete shared contract. Constant-colour
        // maps turn every comparison into an unambiguous oracle: white means lit, black shadowed.
        {
            auto vb = renderer.CreateVertexBuffer(3);
            vb->SetData(kTriPbr, 3, sizeof(VPNTanT));
            auto white = Make1x1Texture(renderer, 255, 255, 255);
            auto black = Make1x1Texture(renderer, 0, 0, 0);

            const auto shadeDirectional = [&](CNA::Internal::Renderers::ITextureRenderer* map) {
                GpuDrawParams params;
                SetDirectPbrParams(params, white.get());
                params.shadowsEnabled = true;
                params.shadowMap = map;
                params.shadowDepthBias = 0.0f;
                params.shadowPcfRadius = 0;
                dev.Clear(Color::Black);
                renderer.DrawPrimitivesEx(
                    *vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                    Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
                Color px(0, 0, 0, 0);
                ReadCenterPixel(dev, px);
                return px.getRProperty();
            };

            const int directionalLit = shadeDirectional(white.get());
            const int directionalShadow = shadeDirectional(black.get());
            std::printf("[INFO] Pbr3D directional shadow red: lit=%d shadow=%d\n",
                        directionalLit, directionalShadow);
            check(directionalLit > 80 && directionalShadow < 10,
                  "Pbr3D: single directional shadow map attenuates direct light only");

            ImageData atlasData;
            atlasData.width = 2;
            atlasData.height = 1;
            atlasData.pixels = {0, 0, 0, 255, 255, 255, 255, 255};
            auto atlas = renderer.CreateTexture(atlasData);
            const auto shadeCascade = [&](bool secondCascade) {
                GpuDrawParams params;
                SetDirectPbrParams(params, white.get());
                params.shadowsEnabled = true;
                params.shadowMap = atlas.get();
                params.shadowDepthBias = 0.0f;
                params.shadowPcfRadius = 0;
                params.cascadeCount = 2;
                params.cascadeSplits[0] = secondCascade ? -1.0f : 1.0f;
                params.cascadeSplits[1] = 2.0f;
                for (int cascade = 0; cascade < 2; ++cascade)
                {
                    float* matrix = params.cascadeMatricesColMajor + cascade * 16;
                    matrix[12] = cascade == 0 ? 0.25f : 0.75f;
                    matrix[13] = 0.5f;
                    matrix[14] = 0.5f;
                    matrix[15] = 1.0f;
                }
                dev.Clear(Color::Black);
                renderer.DrawPrimitivesEx(
                    *vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                    Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
                Color px(0, 0, 0, 0);
                ReadCenterPixel(dev, px);
                return px.getRProperty();
            };
            const int cascade0 = shadeCascade(false);
            const int cascade1 = shadeCascade(true);
            std::printf("[INFO] Pbr3D cascade shadow red: cascade0=%d cascade1=%d\n",
                        cascade0, cascade1);
            check(cascade0 < 10 && cascade1 > 80,
                  "Pbr3D: cascade selection samples the selected atlas slice");

            auto whiteCube = renderer.CreateTextureCube(1, false, 0);
            auto blackCube = renderer.CreateTextureCube(1, false, 0);
            const uint8_t whitePixel[4] = {255, 255, 255, 255};
            const uint8_t blackPixel[4] = {0, 0, 0, 255};
            for (int face = 0; face < 6; ++face)
            {
                whiteCube->SetData(face, 0, 0, 0, 1, 1, whitePixel, sizeof(whitePixel));
                blackCube->SetData(face, 0, 0, 0, 1, 1, blackPixel, sizeof(blackPixel));
            }
            const auto shadePoint = [&](CNA::Internal::Renderers::ITextureCubeRenderer* cube) {
                GpuDrawParams params;
                SetAmbientOnlyPbrParams(params, white.get());
                params.ambientColor[0] = params.ambientColor[1] = params.ambientColor[2] = 0.0f;
                params.eyePositionWorld[2] = 3.0f;
                params.punctualKind = 1;
                params.punctualPosition[2] = 1.0f;
                params.punctualDiffuse[0] = params.punctualDiffuse[1] =
                    params.punctualDiffuse[2] = 6.0f;
                params.punctualRange = 10.0f;
                params.punctualShadowBias = 0.0f;
                params.punctualShadowCube = cube;
                dev.Clear(Color::Black);
                renderer.DrawPrimitivesEx(
                    *vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                    Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
                Color px(0, 0, 0, 0);
                ReadCenterPixel(dev, px);
                return px.getRProperty();
            };
            const int pointLit = shadePoint(whiteCube.get());
            const int pointShadow = shadePoint(blackCube.get());
            check(pointLit > 80 && pointShadow < 10,
                  "Pbr3D: point-light cube shadow attenuates only the punctual light");

            const auto shadeSpot = [&](CNA::Internal::Renderers::ITextureRenderer* map) {
                GpuDrawParams params;
                SetAmbientOnlyPbrParams(params, white.get());
                params.ambientColor[0] = params.ambientColor[1] = params.ambientColor[2] = 0.0f;
                params.eyePositionWorld[2] = 3.0f;
                params.punctualKind = 2;
                params.punctualPosition[2] = 1.0f;
                params.punctualDirection[0] = 0.0f;
                params.punctualDirection[1] = 0.0f;
                params.punctualDirection[2] = -1.0f;
                params.punctualDiffuse[0] = params.punctualDiffuse[1] =
                    params.punctualDiffuse[2] = 6.0f;
                params.punctualRange = 10.0f;
                params.punctualCosInner = 0.94f;
                params.punctualCosOuter = 0.76f;
                params.punctualShadowBias = 0.0f;
                params.punctualShadowMap = map;
                dev.Clear(Color::Black);
                renderer.DrawPrimitivesEx(
                    *vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                    Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
                Color px(0, 0, 0, 0);
                ReadCenterPixel(dev, px);
                return px.getRProperty();
            };
            const int spotLit = shadeSpot(white.get());
            const int spotShadow = shadeSpot(black.get());
            std::printf("[INFO] Pbr3D spot shadow red: lit=%d shadow=%d\n",
                        spotLit, spotShadow);
            check(spotLit > 80 && spotShadow < 10,
                  "Pbr3D: spot-light shadow lookup attenuates only the punctual light");

            const auto shadeDirectionalAndSpot = [&](CNA::Internal::Renderers::ITextureRenderer* directionalMap,
                                                      CNA::Internal::Renderers::ITextureRenderer* spotMap,
                                                      float directionalStrength,
                                                      float spotStrength,
                                                      bool cascaded) {
                GpuDrawParams params;
                SetDirectPbrParams(params, white.get());
                params.light0Diffuse[0] = params.light0Diffuse[1] =
                    params.light0Diffuse[2] = directionalStrength;
                params.shadowsEnabled = true;
                params.shadowMap = directionalMap;
                params.shadowDepthBias = 0.0f;
                params.shadowPcfRadius = 0;
                if (cascaded)
                {
                    params.cascadeCount = 2;
                    params.cascadeSplits[0] = -1.0f;
                    params.cascadeSplits[1] = 2.0f;
                    for (int cascade = 0; cascade < 2; ++cascade)
                    {
                        float* matrix = params.cascadeMatricesColMajor + cascade * 16;
                        matrix[12] = cascade == 0 ? 0.25f : 0.75f;
                        matrix[13] = 0.5f;
                        matrix[14] = 0.5f;
                        matrix[15] = 1.0f;
                    }
                }
                params.punctualKind = 2;
                params.punctualPosition[2] = 1.0f;
                params.punctualDirection[0] = params.punctualDirection[1] = 0.0f;
                params.punctualDirection[2] = -1.0f;
                params.punctualDiffuse[0] = params.punctualDiffuse[1] =
                    params.punctualDiffuse[2] = spotStrength;
                params.punctualRange = 10.0f;
                params.punctualCosInner = 0.94f;
                params.punctualCosOuter = 0.76f;
                params.punctualShadowBias = 0.0f;
                params.punctualShadowMap = spotMap;
                dev.Clear(Color::Black);
                renderer.DrawPrimitivesEx(
                    *vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                    Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
                Color px(0, 0, 0, 0);
                ReadCenterPixel(dev, px);
                return px.getRProperty();
            };
            check(shadeDirectionalAndSpot(white.get(), white.get(), 0.0f, 6.0f, false) > 80 &&
                      shadeDirectionalAndSpot(white.get(), black.get(), 0.0f, 6.0f, false) < 10,
                  "Pbr3D: combined directional-plus-spot variant samples the spot map");
            check(shadeDirectionalAndSpot(white.get(), white.get(), 6.0f, 0.0f, false) > 80 &&
                      shadeDirectionalAndSpot(black.get(), white.get(), 6.0f, 0.0f, false) < 10,
                  "Pbr3D: combined directional-plus-spot variant samples the directional map");
            check(shadeDirectionalAndSpot(atlas.get(), white.get(), 6.0f, 0.0f, true) > 80,
                  "Pbr3D: combined cascade-plus-spot variant selects the lit atlas slice");

            auto skinnedVb = renderer.CreateVertexBuffer(3);
            skinnedVb->SetData(kTriPbrSkinned, 3, sizeof(VPNTanTW));
            const auto shadeSkinned = [&](CNA::Internal::Renderers::ITextureRenderer* map) {
                GpuDrawParams params;
                SetDirectPbrParams(params, white.get());
                params.skinned = true;
                params.weightsPerVertex = 1;
                params.boneCount = 1;
                const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
                for (int i = 0; i < 16; ++i) params.boneTransforms[i] = identity[i];
                params.shadowsEnabled = true;
                params.shadowMap = map;
                params.shadowDepthBias = 0.0f;
                params.shadowPcfRadius = 0;
                dev.Clear(Color::Black);
                renderer.DrawPrimitivesEx(
                    *skinnedVb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                    Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
                Color px(0, 0, 0, 0);
                ReadCenterPixel(dev, px);
                return px.getRProperty();
            };
            check(shadeSkinned(white.get()) > 80 && shadeSkinned(black.get()) < 10,
                  "PbrSkinned3D: directional shadow survives identity-bone skinning");

            const auto shadeBasic = [&](CNA::Internal::Renderers::ITextureRenderer* map,
                                        bool skinned) {
                GpuDrawParams params;
                params.lightingEnabled = true;
                params.vertexColorEnabled = false;
                params.textureEnabled = false;
                params.diffuseColor[0] = params.diffuseColor[1] =
                    params.diffuseColor[2] = params.diffuseColor[3] = 1.0f;
                params.light0Dir[2] = -1.0f;
                params.light0Diffuse[0] = params.light0Diffuse[1] =
                    params.light0Diffuse[2] = 1.0f;
                params.eyePositionWorld[2] = 3.0f;
                params.shadowsEnabled = true;
                params.shadowMap = map;
                params.shadowDepthBias = 0.0f;
                params.shadowPcfRadius = 0;
                params.skinned = skinned;
                params.texture0 = white.get();
                params.weightsPerVertex = 1;
                params.boneCount = 1;
                const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
                for (int i = 0; i < 16; ++i) params.boneTransforms[i] = identity[i];
                dev.Clear(Color::Black);
                if (skinned)
                {
                    renderer.DrawPrimitivesEx(
                        *skinnedVb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                        Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
                }
                else
                {
                    renderer.DrawPrimitivesEx(
                        *vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                        Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
                }
                Color px(0, 0, 0, 0);
                ReadCenterPixel(dev, px);
                return px.getRProperty();
            };
            check(shadeBasic(white.get(), false) > 80 && shadeBasic(black.get(), false) < 10,
                  "BasicEffect extension shader samples the directional shadow map");
            check(shadeBasic(white.get(), true) > 80 && shadeBasic(black.get(), true) < 10,
                  "SkinnedEffect extension shader samples the directional shadow map");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        std::fflush(stdout);
        Exit();
    }

public:
    D3D9PbrTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // GetBackBufferData is a HiDef-only operation in XNA; this program reads the back buffer.
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);

        // Validate both generated pixel-shader blobs before the first resize/present. This keeps
        // shader-bytecode acceptance visible even on WineD3D configurations whose swap-chain
        // resize fails before Draw() is reached.
        auto& renderer = static_cast<DirectX9Renderer&>(getGraphicsDeviceProperty().GetRenderer());
        auto* device = renderer.GetDeviceEXT();
        std::printf("[INFO] DirectX9 ps_3_0 instruction slots: %lu\n",
                    static_cast<unsigned long>(renderer.GetCapsEXT().MaxPixelShader30InstructionSlots));
        check(renderer.SupportsShadowSamplingEXT(),
              "DirectX9 advertises shadow sampling with SM3 and cube textures");
        IDirect3DPixelShader9* rigid = nullptr;
        IDirect3DPixelShader9* skinned = nullptr;
        IDirect3DPixelShader9* rigidPointShadow = nullptr;
        IDirect3DPixelShader9* rigidSpotShadow = nullptr;
        IDirect3DPixelShader9* rigidSpotDirectionalShadow = nullptr;
        IDirect3DPixelShader9* skinnedPointShadow = nullptr;
        IDirect3DPixelShader9* skinnedSpotShadow = nullptr;
        IDirect3DPixelShader9* skinnedSpotDirectionalShadow = nullptr;
        const HRESULT rigidHr = device->CreatePixelShader(
            reinterpret_cast<const DWORD*>(Shaders::kPbr3DPSBytecode), &rigid);
        const HRESULT skinnedHr = device->CreatePixelShader(
            reinterpret_cast<const DWORD*>(Shaders::kPbrSkinned3DPSBytecode), &skinned);
        const HRESULT rigidPointShadowHr = device->CreatePixelShader(
            reinterpret_cast<const DWORD*>(Shaders::kPbr3DPointShadowPSBytecode), &rigidPointShadow);
        const HRESULT rigidSpotShadowHr = device->CreatePixelShader(
            reinterpret_cast<const DWORD*>(Shaders::kPbr3DSpotOnlyShadowPSBytecode), &rigidSpotShadow);
        const HRESULT rigidSpotDirectionalShadowHr = device->CreatePixelShader(
            reinterpret_cast<const DWORD*>(Shaders::kPbr3DSpotDirectionalShadowPSBytecode),
            &rigidSpotDirectionalShadow);
        const HRESULT skinnedPointShadowHr = device->CreatePixelShader(
            reinterpret_cast<const DWORD*>(Shaders::kPbrSkinned3DPointShadowPSBytecode), &skinnedPointShadow);
        const HRESULT skinnedSpotShadowHr = device->CreatePixelShader(
            reinterpret_cast<const DWORD*>(Shaders::kPbrSkinned3DSpotOnlyShadowPSBytecode), &skinnedSpotShadow);
        const HRESULT skinnedSpotDirectionalShadowHr = device->CreatePixelShader(
            reinterpret_cast<const DWORD*>(Shaders::kPbrSkinned3DSpotDirectionalShadowPSBytecode),
            &skinnedSpotDirectionalShadow);
        check(SUCCEEDED(rigidHr), "CreatePixelShader accepts the generated rigid PBR blob");
        check(SUCCEEDED(skinnedHr), "CreatePixelShader accepts the generated skinned PBR blob");
        check(SUCCEEDED(rigidPointShadowHr), "CreatePixelShader accepts the rigid PBR point-shadow blob");
        check(SUCCEEDED(rigidSpotShadowHr), "CreatePixelShader accepts the rigid PBR spot-shadow blob");
        check(SUCCEEDED(rigidSpotDirectionalShadowHr),
              "CreatePixelShader accepts the rigid PBR directional-plus-spot blob");
        check(SUCCEEDED(skinnedPointShadowHr), "CreatePixelShader accepts the skinned PBR point-shadow blob");
        check(SUCCEEDED(skinnedSpotShadowHr), "CreatePixelShader accepts the skinned PBR spot-shadow blob");
        check(SUCCEEDED(skinnedSpotDirectionalShadowHr),
              "CreatePixelShader accepts the skinned PBR directional-plus-spot blob");
        if (rigid != nullptr) rigid->Release();
        if (skinned != nullptr) skinned->Release();
        if (rigidPointShadow != nullptr) rigidPointShadow->Release();
        if (rigidSpotShadow != nullptr) rigidSpotShadow->Release();
        if (rigidSpotDirectionalShadow != nullptr) rigidSpotDirectionalShadow->Release();
        if (skinnedPointShadow != nullptr) skinnedPointShadow->Release();
        if (skinnedSpotShadow != nullptr) skinnedSpotShadow->Release();
        if (skinnedSpotDirectionalShadow != nullptr) skinnedSpotDirectionalShadow->Release();
    }
};

int main()
{
    try
    {
        D3D9PbrTest game;
        game.Run();
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "[FAIL] DirectX9 PBR test threw: %s\n", error.what());
        std::fflush(stderr);
        return 2;
    }

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    std::fflush(stdout);
    return (passCount == totalCount) ? 0 : 1;
}
