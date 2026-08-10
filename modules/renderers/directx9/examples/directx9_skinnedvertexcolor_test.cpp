// SPDX-License-Identifier: MS-PL
// D3D9 skinned-vertex-color porting task: real DrawPrimitivesEx dispatch for CNA's own CNAEXT
// "SkinnedVertexColor3D" shader (stride 56 -- SkinnedEffect + a vertex Color real XNA's own
// compiled SkinnedEffect.fx bytecode never carries), via D3D9SkinnedVertexColorDraw.cpp /
// shaders/cna/SkinnedVertexColor3D.hlsl.
//
// Every expected pixel value below is hand-derived from SkinnedVertexColor3D.hlsl's own PS formula
// with every light's diffuse AND specular color deliberately zeroed (mirrors
// directx9_drawex_test.cpp's own "SpecularColor=0 sidesteps a half-vector-dependent term" precedent):
// `litRGB = (EmissiveColor + lightSum(=0)) * DiffuseColor.rgb` collapses to just
// `EmissiveColor * DiffuseColor.rgb`, and `specularRGB` is exactly zero regardless of geometry/eye
// vector. With EmissiveColor=(1,1,1), DiffuseColor=(1,1,1,1), and an opaque white texture, litRGB
// and texColor.rgb are both (1,1,1) -- the pixel is then EXACTLY the vertex-color term alone,
// isolating the one thing this task actually adds over the real SkinnedEffect.
//
// Check A -- VertexColorEnabled=true, vertex color = pure opaque red (255,0,0,255): the final pixel
//   is EXACTLY red (255,0,0,255) -- proves the vertex-color multiply happens AFTER the (zero)
//   specular add, not before (the documented "trap": a black/off-primary vertex color must
//   genuinely override every channel of the final combined output, matching
//   EnsureSkinnedProgram()'s own established fix on the EasyGL side).
// Check B -- VertexColorEnabled=false, SAME red vertex data: the final pixel is white (255,255,255,
//   255) -- proves the enable/disable gate reads the real GpuDrawParams flag (not hardcoded true),
//   and that disabling it genuinely ignores the bound vertex Color stream.
// Check C -- a single Identity bone at 100% weight (skinning is a deliberate no-op, same precedent
//   as directx9_drawex_test.cpp's own Check O / directx9_pbr_test.cpp's own Check B): Check A's setup
//   redrawn with boneCount=1/weightsPerVertex=1 gives the SAME exact red readback, proving the
//   Bones[72] upload doesn't corrupt vertex-color output either.
// Check D -- missing texture0 throws a named error, matching DrawSkinnedEffectEXT's own identical
//   requirement for the real SkinnedEffect.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.
//
// NOTE (2026-07): this file is written and cross-compile-checked (mingw) but has NOT been run --
// this development machine has no Windows/live D3D9 device/GPU driver to execute it against. See
// this task's own final report for the exact verification that WAS performed.

#include "Microsoft/Xna/Framework/Game.hpp"
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

#include <cstdint>
#include <cstdio>
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
        if (ok) ++passCount;
    }

    // stride 56: VertexPositionNormalTextureSkinned + Color (Position, Normal, TexCoord,
    // BlendWeight(FLOAT4), BlendIndices(UBYTE4), Color(UBYTE4N)).
    struct VPNTWC
    {
        float x, y, z, nx, ny, nz, u, v;
        float w0, w1, w2, w3;
        uint8_t idx[4];
        uint8_t color[4]; // R,G,B,A ascending (matches D3D9VertexDeclarations.hpp's own UBYTE4N COLOR0 convention)
    };
    const VPNTWC kTriRedVertexColor[3] = {
        {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, {0, 0, 0, 0}, {255, 0, 0, 255}},
        { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, {0, 0, 0, 0}, {255, 0, 0, 255}},
        {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, {0, 0, 0, 0}, {255, 0, 0, 255}},
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

    void SetBaseParams(GpuDrawParams& params, CNA::Internal::Renderers::ITextureRenderer* tex)
    {
        params.skinned = true;
        params.texture0 = tex;
        params.diffuseColor[0] = params.diffuseColor[1] = params.diffuseColor[2] = params.diffuseColor[3] = 1.0f;
        params.emissiveColor[0] = params.emissiveColor[1] = params.emissiveColor[2] = 1.0f; // pre-folded ambient
        params.light0Diffuse[0] = params.light0Diffuse[1] = params.light0Diffuse[2] = 0.0f;
        params.light1Diffuse[0] = params.light1Diffuse[1] = params.light1Diffuse[2] = 0.0f;
        params.light2Diffuse[0] = params.light2Diffuse[1] = params.light2Diffuse[2] = 0.0f;
        params.light0Specular[0] = params.light0Specular[1] = params.light0Specular[2] = 0.0f;
        params.light1Specular[0] = params.light1Specular[1] = params.light1Specular[2] = 0.0f;
        params.light2Specular[0] = params.light2Specular[1] = params.light2Specular[2] = 0.0f;
        params.specularColor[0] = params.specularColor[1] = params.specularColor[2] = 0.0f;
        params.weightsPerVertex = 1;
        params.boneCount = 1;
        const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        for (int i = 0; i < 16; ++i) params.boneTransforms[i] = identity[i];
    }
}

class D3D9SkinnedVertexColorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<DirectX9Renderer&>(dev.GetRenderer());

        renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, CNA::Internal::Renderers::BlendWriteState{}); // REMED-GFX-077 default write state
        renderer.ApplyDepthStencilState(false, false, 0, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
        renderer.ApplyRasterizerState(0 /*CullMode::None*/, 0 /*FillMode::Solid*/, false, 0.0f, 0.0f);
        renderer.ApplySamplerState(0, 1 /*TextureFilter::Point*/, 0, 0, 1);

        auto whiteTex = Make1x1Texture(renderer, 255, 255, 255, 255);

        // Check A: VertexColorEnabled=true, red vertex color -- exact red readback.
        {
            auto vb = renderer.CreateVertexBuffer(3);
            vb->SetData(kTriRedVertexColor, 3, sizeof(VPNTWC));

            GpuDrawParams params;
            SetBaseParams(params, whiteTex.get());
            params.vertexColorEnabled = true;

            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            Color px(0, 0, 0, 0);
            ReadCenterPixel(dev, px);
            check(px.getRProperty() == 255 && px.getGProperty() == 0 && px.getBProperty() == 0 && px.getAProperty() == 255,
                  "DrawPrimitivesEx (SkinnedVertexColor3D, VertexColorEnabled=true): exact red "
                  "readback -- vertex color multiplies the combined diffuse+specular output "
                  "AFTER the specular add");
        }

        // Check B: VertexColorEnabled=false, SAME red vertex data -- exact white readback.
        {
            auto vb = renderer.CreateVertexBuffer(3);
            vb->SetData(kTriRedVertexColor, 3, sizeof(VPNTWC));

            GpuDrawParams params;
            SetBaseParams(params, whiteTex.get());
            params.vertexColorEnabled = false;

            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            Color px(0, 0, 0, 0);
            ReadCenterPixel(dev, px);
            check(px.getRProperty() == 255 && px.getGProperty() == 255 && px.getBProperty() == 255 && px.getAProperty() == 255,
                  "DrawPrimitivesEx (SkinnedVertexColor3D, VertexColorEnabled=false): exact white "
                  "readback -- the bound (red) vertex Color stream is genuinely ignored");
        }

        // Check C: Identity bone doesn't corrupt vertex-color output (Check A's setup, indexed draw).
        {
            auto vb = renderer.CreateVertexBuffer(3);
            vb->SetData(kTriRedVertexColor, 3, sizeof(VPNTWC));
            auto ib = renderer.CreateIndexBuffer16(3);
            static const uint16_t kIdx[3] = {0, 1, 2};
            ib->SetData16(kIdx, 3);

            GpuDrawParams params;
            SetBaseParams(params, whiteTex.get());
            params.vertexColorEnabled = true;

            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawIndexedPrimitivesEx(*vb, *ib, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                            Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            Color px(0, 0, 0, 0);
            ReadCenterPixel(dev, px);
            check(px.getRProperty() == 255 && px.getGProperty() == 0 && px.getBProperty() == 0 && px.getAProperty() == 255,
                  "DrawIndexedPrimitivesEx (SkinnedVertexColor3D, Identity bone): exact red "
                  "readback, Bones[72] upload doesn't corrupt vertex-color output");
        }

        // Check D: missing texture0 throws a named error.
        {
            auto vb = renderer.CreateVertexBuffer(3);
            vb->SetData(kTriRedVertexColor, 3, sizeof(VPNTWC));

            GpuDrawParams params;
            SetBaseParams(params, nullptr);
            params.vertexColorEnabled = true;

            dev.Clear(Color(0, 0, 255, 255));
            bool threw = false;
            try
            {
                renderer.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            }
            catch (const std::exception&) { threw = true; }
            check(threw, "DrawPrimitivesEx (SkinnedVertexColor3D): missing texture0 throws a named error");
        }

        // Check E (REMED-GFX-008): EmissiveColor is ADDED outside the diffuse multiply, not
        // multiplied by it. Lights off, DiffuseColor=(0.5), EmissiveColor=(0.8) ->
        // litRGB = lightSum(0)*0.5 + 0.8 = 0.8 -> ~204. The old (EmissiveColor + lightSum)*DiffuseColor
        // bug gave 0.8*0.5 = 0.4 -> ~102, so this discriminates the fix.
        {
            auto vb = renderer.CreateVertexBuffer(3);
            vb->SetData(kTriRedVertexColor, 3, sizeof(VPNTWC));

            GpuDrawParams params;
            SetBaseParams(params, whiteTex.get());
            params.vertexColorEnabled = false;
            params.diffuseColor[0] = params.diffuseColor[1] = params.diffuseColor[2] = 0.5f;
            params.diffuseColor[3] = 1.0f;
            params.emissiveColor[0] = params.emissiveColor[1] = params.emissiveColor[2] = 0.8f;

            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            Color px(0, 0, 0, 0);
            ReadCenterPixel(dev, px);
            const bool okE = px.getRProperty() >= 200 && px.getRProperty() <= 208
                          && px.getGProperty() >= 200 && px.getGProperty() <= 208
                          && px.getBProperty() >= 200 && px.getBProperty() <= 208;
            check(okE,
                  "DrawPrimitivesEx (SkinnedVertexColor3D): EmissiveColor added OUTSIDE the diffuse "
                  "multiply -- Diffuse=0.5 Emissive=0.8 lights-off -> ~204, not the old bug's ~102");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    D3D9SkinnedVertexColorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }
};

int main()
{
    D3D9SkinnedVertexColorTest game;
    game.Run();

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
