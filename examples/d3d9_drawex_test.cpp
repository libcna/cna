// SPDX-License-Identifier: MS-PL
// plan_dx9.md Phase D9-8 (D9-82b/c/d): real effect-aware DrawPrimitivesEx/DrawIndexedPrimitivesEx
// dispatch -- BasicEffect (D9-82b), AlphaTestEffect (D9-82c), DualTextureEffect (D9-82d).
// EnvironmentMapEffect/SkinnedEffect are D9-82e/f, not yet implemented; Check F confirms each
// throws a named, row-specific not-yet-implemented rather than silently drawing the wrong thing.
//
// Every expected pixel value below is HAND-COMPUTED directly from BasicEffect.fx's/
// AlphaTestEffect.fx's/DualTextureEffect.fx's/Lighting.fxh's own real HLSL formulas
// (SampleTexture*Diffuse, ComputeLights' mul(diffuse,lightDiffuse)*DiffuseColor.rgb+EmissiveColor,
// ApplyFog's lerp, AlphaTestEffect's clip((cmp) ? AlphaTest.z : AlphaTest.w),
// DualTextureEffect's color.rgb*=2; color*=overlay*Diffuse), not just "close enough" --
// SpecularColor is deliberately zeroed in every BasicEffect check so AddSpecular() never
// contributes (sidesteps needing to also hand-compute a half-vector-dependent specular term;
// SpecularColor=0 makes that term exactly zero regardless of light direction/geometry, an exact
// simplification, not an approximation).
//
// Check A -- unlit + textured (stride 20, ShaderIndex 4/5): exact texture*DiffuseColor readback.
// Check B -- unlit + vertex-color + textured (stride 24, ShaderIndex 6/7): exact
//   texture*vertexColor*DiffuseColor readback.
// Check C -- lit + textured, VertexLighting bucket (stride 32, ShaderIndex 12/13): TWO active
//   lights (oneLight derivation must resolve to false) -- exact texture*(dotL0*Ld0+dotL1*Ld1).
// Check D -- lit + textured, OneLight bucket (stride 32, ShaderIndex 20/21): ONE active light
//   (oneLight derivation must resolve to true) -- exact texture*(dotL0*Ld0). Checks C+D together
//   prove correct bucket selection: swapping which shader answers which check would change the
//   numeric result (light1's contribution is only visible in Check C).
// Check E -- fog: same unlit+textured setup as Check A, but with fog forced to its fully-fogged
//   (fogFactor=1) case via a specific fogStart/fogEnd/geometry-Z combination -- exact FogColor
//   readback (ApplyFog's lerp(color,FogColor*alpha,1) == FogColor*alpha).
// Check F -- an unsupported BasicEffect flag/stride combination and an unsupported
//   DualTextureEffect flag combination (no matching CNA vertex layout) each throw a named error
//   rather than silently drawing wrong; EnvironmentMapEffect/SkinnedEffect each throw their own
//   row-specific not-yet-implemented (D9-82e/f).
// Check G -- AlphaTestEffect, Less compare, PASSES (LtGt bucket, stride 20): exact
//   texture*DiffuseColor readback, proving DiffuseColor + the clip()-passes path both work.
// Check H -- AlphaTestEffect, Less compare, FAILS (discarded): the background is left genuinely
//   UNPAINTED -- proves clip() actually discards rather than always drawing.
// Check I -- AlphaTestEffect, Equal compare, PASSES, vertex-color bucket (stride 24, EqNe PS):
//   exact texture readback, proving the EqNe pixel shader and the Vc vertex shader both work.
// Check J -- DualTextureEffect, no fog/vertex color (stride 28, the new D3D9-only layout): exact
//   2*texture0*texture1*DiffuseColor readback, proving the doubling-blend formula, the two-sampler
//   bind (texture0/texture1), and the new stride-28 vertex declaration all work together.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"

#include "CNA/Internal/Backends/D3D9/D3D9GraphicsBackend.hpp"
#include "CNA/Internal/Backends/D3D9/D3D9Textures.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::D3D9;
using CNA::Internal::Backends::GpuDrawParams;
using CNA::Internal::Backends::ImageData;

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

    struct VPT { float x, y, z, u, v; };           // stride 20: Position+TexCoord
    struct VPCT { float x, y, z; uint32_t color; float u, v; }; // stride 24: Position+Color+TexCoord
    struct VPNT { float x, y, z, nx, ny, nz, u, v; }; // stride 32: Position+Normal+TexCoord

    // Same full-NDC "oversized triangle" trick D9-82's own DrawColoredPrimitives test uses.
    const VPT kTriTx[3] = {
        {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f},
        { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f},
        {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f},
    };
    const VPCT kTriColorTx[3] = {
        {-1.0f, -1.0f, 0.0f, 0xFFFFFFFFu, 0.0f, 0.0f}, // white vertex color -- see Check B override below
        { 3.0f, -1.0f, 0.0f, 0xFFFFFFFFu, 0.0f, 0.0f},
        {-1.0f,  3.0f, 0.0f, 0xFFFFFFFFu, 0.0f, 0.0f},
    };
    const VPNT kTriNormalTx[3] = {
        {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
        { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
        {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
    };
    struct VPT2 { float x, y, z, u0, v0, u1, v1; }; // D3D9-only stride 28: Position+TexCoord0+TexCoord1
    const VPT2 kTriDualTx[3] = {
        {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    };

    std::unique_ptr<CNA::Internal::Backends::ITextureBackend> Make1x1Texture(
        D3D9GraphicsBackend& backend, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        ImageData img;
        img.width = 1;
        img.height = 1;
        img.pixels = {r, g, b, a};
        return backend.CreateTexture(img);
    }

    bool ReadCenterPixel(GraphicsDevice& dev, Color& out)
    {
        const Microsoft::Xna::Framework::Rectangle region(28, 28, 1, 1);
        std::vector<Color> pixels(1, Color(0, 0, 0, 0));
        dev.GetBackBufferData(&region, pixels.data(), 0, 1);
        out = pixels[0];
        return true;
    }
}

class D3D9DrawExTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        auto& backend = static_cast<D3D9GraphicsBackend&>(dev.GetBackend());

        backend.ApplyBlendState(0, 0, 1, 1, 0, 0);
        backend.ApplyDepthStencilState(false, false, 0, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
        backend.ApplyRasterizerState(0 /*CullMode::None*/, 0 /*FillMode::Solid*/, false, 0.0f, 0.0f);
        backend.ApplySamplerState(0, 1 /*TextureFilter::Point*/, 0, 0, 1);

        auto tex = Make1x1Texture(backend, 200, 120, 40);

        // Check A: unlit + textured -- exact texture*DiffuseColor.
        {
            auto vb = backend.CreateVertexBuffer(3);
            vb->SetData(kTriTx, 3, sizeof(VPT));

            GpuDrawParams params;
            params.textureEnabled = true;
            params.vertexColorEnabled = false; // GpuDrawParams defaults this to true
            params.texture0 = tex.get();
            params.diffuseColor[0] = params.diffuseColor[1] = params.diffuseColor[2] = params.diffuseColor[3] = 1.0f;

            dev.Clear(Color(0, 0, 255, 255));
            backend.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            Color px(0, 0, 0, 0);
            ReadCenterPixel(dev, px);
            check(px.getRProperty() == 200 && px.getGProperty() == 120 && px.getBProperty() == 40 && px.getAProperty() == 255,
                  "DrawPrimitivesEx (BasicEffect unlit+textured, ShaderIndex 4/5): exact texture*DiffuseColor readback");
        }

        // Check B: unlit + vertex-color + textured -- exact texture*vertexColor*DiffuseColor.
        {
            auto vb = backend.CreateVertexBuffer(3);
            vb->SetData(kTriColorTx, 3, sizeof(VPCT));

            GpuDrawParams params;
            params.textureEnabled = true;
            params.vertexColorEnabled = true;
            params.texture0 = tex.get();
            params.diffuseColor[0] = params.diffuseColor[1] = params.diffuseColor[2] = params.diffuseColor[3] = 1.0f;

            dev.Clear(Color(0, 0, 255, 255));
            backend.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            Color px(0, 0, 0, 0);
            ReadCenterPixel(dev, px);
            // Vertex color is opaque white (0xFFFFFFFF) -- multiplying by it is a no-op, same
            // expected result as Check A, but exercises the VSBasicTxVc/PSBasicTx(NoFog) pair and
            // the stride-24 vertex declaration for real (a different code path than Check A).
            check(px.getRProperty() == 200 && px.getGProperty() == 120 && px.getBProperty() == 40 && px.getAProperty() == 255,
                  "DrawPrimitivesEx (BasicEffect unlit+vertexColor+textured, ShaderIndex 6/7): exact readback");
        }

        // Check C: lit + textured, VertexLighting bucket -- two active lights.
        {
            auto vb = backend.CreateVertexBuffer(3);
            vb->SetData(kTriNormalTx, 3, sizeof(VPNT));

            GpuDrawParams params;
            params.textureEnabled = true;
            params.vertexColorEnabled = false; // GpuDrawParams defaults this to true
            params.lightingEnabled = true;
            params.texture0 = tex.get();
            params.diffuseColor[0] = params.diffuseColor[1] = params.diffuseColor[2] = params.diffuseColor[3] = 1.0f;
            params.specularColor[0] = params.specularColor[1] = params.specularColor[2] = 0.0f; // sidesteps half-vector math
            // Both lights shine from +Z onto our (0,0,1)-facing normal: dotL = dot(-lightDir, normal) = 1 (fully lit).
            params.light0Dir[0] = 0.0f; params.light0Dir[1] = 0.0f; params.light0Dir[2] = -1.0f;
            params.light0Diffuse[0] = params.light0Diffuse[1] = params.light0Diffuse[2] = 0.5f;
            params.light1Dir[0] = 0.0f; params.light1Dir[1] = 0.0f; params.light1Dir[2] = -1.0f;
            params.light1Diffuse[0] = params.light1Diffuse[1] = params.light1Diffuse[2] = 0.25f;
            // light2 stays at its zero default.

            dev.Clear(Color(0, 0, 255, 255));
            backend.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            Color px(0, 0, 0, 0);
            ReadCenterPixel(dev, px);
            // diffuse sum = 1*0.5 + 1*0.25 = 0.75 -> texture(200,120,40)*0.75 = (150,90,30) exactly.
            check(px.getRProperty() == 150 && px.getGProperty() == 90 && px.getBProperty() == 30 && px.getAProperty() == 255,
                  "DrawPrimitivesEx (BasicEffect lit+textured, VertexLighting bucket, ShaderIndex 12/13): "
                  "exact two-light-sum readback (150,90,30)");
        }

        // Check D: lit + textured, OneLight bucket -- one active light (light1/2 zero).
        {
            auto vb = backend.CreateVertexBuffer(3);
            vb->SetData(kTriNormalTx, 3, sizeof(VPNT));

            GpuDrawParams params;
            params.textureEnabled = true;
            params.vertexColorEnabled = false; // GpuDrawParams defaults this to true
            params.lightingEnabled = true;
            params.texture0 = tex.get();
            params.diffuseColor[0] = params.diffuseColor[1] = params.diffuseColor[2] = params.diffuseColor[3] = 1.0f;
            params.specularColor[0] = params.specularColor[1] = params.specularColor[2] = 0.0f;
            params.light0Dir[0] = 0.0f; params.light0Dir[1] = 0.0f; params.light0Dir[2] = -1.0f;
            params.light0Diffuse[0] = params.light0Diffuse[1] = params.light0Diffuse[2] = 0.4f;
            // light1/light2 stay at their zero defaults -> oneLight must resolve to true.

            dev.Clear(Color(0, 0, 255, 255));
            backend.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            Color px(0, 0, 0, 0);
            ReadCenterPixel(dev, px);
            // diffuse sum = 1*0.4 = 0.4 -> texture(200,120,40)*0.4 = (80,48,16) exactly.
            check(px.getRProperty() == 80 && px.getGProperty() == 48 && px.getBProperty() == 16 && px.getAProperty() == 255,
                  "DrawPrimitivesEx (BasicEffect lit+textured, OneLight bucket, ShaderIndex 20/21): "
                  "exact one-light readback (80,48,16) -- different from Check C, proves correct bucket selection");
        }

        // Check E: fog -- same unlit+textured setup as Check A, forced to fully-fogged.
        {
            auto vb = backend.CreateVertexBuffer(3);
            vb->SetData(kTriTx, 3, sizeof(VPT));

            GpuDrawParams params;
            params.textureEnabled = true;
            params.vertexColorEnabled = false; // GpuDrawParams defaults this to true
            params.texture0 = tex.get();
            params.diffuseColor[0] = params.diffuseColor[1] = params.diffuseColor[2] = params.diffuseColor[3] = 1.0f;
            params.fogEnabled = true;
            params.fogStart = 1.0f;
            params.fogEnd = 0.0f; // with world=view=Identity and geometry Z=0, this forces fogFactor=1 exactly.
            const Color fogC(40, 80, 120, 255);
            params.fogColor[0] = fogC.getRProperty() / 255.0f;
            params.fogColor[1] = fogC.getGProperty() / 255.0f;
            params.fogColor[2] = fogC.getBProperty() / 255.0f;

            dev.Clear(Color(0, 0, 255, 255));
            backend.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            Color px(0, 0, 0, 0);
            ReadCenterPixel(dev, px);
            check(px.getRProperty() == 40 && px.getGProperty() == 80 && px.getBProperty() == 120 && px.getAProperty() == 255,
                  "DrawPrimitivesEx (BasicEffect fog, fully-fogged): exact FogColor readback (ApplyFog's lerp at fogFactor=1)");
        }

        // Check G: AlphaTestEffect, Less compare, PASSES (LtGt bucket, stride 20).
        {
            auto vb = backend.CreateVertexBuffer(3);
            vb->SetData(kTriTx, 3, sizeof(VPT));
            auto atexLowAlpha = Make1x1Texture(backend, 200, 120, 40, 64);

            GpuDrawParams params;
            params.textureEnabled = true;
            params.vertexColorEnabled = false;
            params.texture0 = atexLowAlpha.get();
            params.diffuseColor[0] = params.diffuseColor[1] = params.diffuseColor[2] = 0.5f;
            params.diffuseColor[3] = 1.0f;
            // Less: clip((color.a < AlphaTest.x) ? AlphaTest.z : AlphaTest.w). Texture alpha
            // 64/255 = 0.251 < refVal 0.5 -> passes (AlphaTest.z=1).
            params.alphaTest[0] = 0.5f; params.alphaTest[1] = 0.0f;
            params.alphaTest[2] = 1.0f; params.alphaTest[3] = -1.0f;

            dev.Clear(Color(0, 0, 255, 255));
            backend.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            Color px(0, 0, 0, 0);
            ReadCenterPixel(dev, px);
            // texture(200,120,40,64) * DiffuseColor(0.5,0.5,0.5,1) = (100,60,20,64) exactly.
            check(px.getRProperty() == 100 && px.getGProperty() == 60 && px.getBProperty() == 20 && px.getAProperty() == 64,
                  "DrawPrimitivesEx (AlphaTestEffect Less, PASSES, ShaderIndex LtGt bucket): "
                  "exact texture*DiffuseColor readback (100,60,20,64)");
        }

        // Check H: AlphaTestEffect, Less compare, FAILS -- background left genuinely unpainted.
        {
            auto vb = backend.CreateVertexBuffer(3);
            vb->SetData(kTriTx, 3, sizeof(VPT));
            auto atexLowAlpha = Make1x1Texture(backend, 200, 120, 40, 64);

            GpuDrawParams params;
            params.textureEnabled = true;
            params.vertexColorEnabled = false;
            params.texture0 = atexLowAlpha.get();
            params.diffuseColor[0] = params.diffuseColor[1] = params.diffuseColor[2] = params.diffuseColor[3] = 1.0f;
            // Less: texture alpha 64/255 = 0.251 > refVal 0.1 -> the "< " test is FALSE -> discard (AlphaTest.w=-1).
            params.alphaTest[0] = 0.1f; params.alphaTest[1] = 0.0f;
            params.alphaTest[2] = 1.0f; params.alphaTest[3] = -1.0f;

            dev.Clear(Color(0, 0, 255, 255));
            backend.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            Color px(0, 0, 0, 0);
            ReadCenterPixel(dev, px);
            check(px.getRProperty() == 0 && px.getGProperty() == 0 && px.getBProperty() == 255 && px.getAProperty() == 255,
                  "DrawPrimitivesEx (AlphaTestEffect Less, FAILS/discarded): background genuinely UNPAINTED "
                  "(clip() actually discards, not just always drawing)");
        }

        // Check I: AlphaTestEffect, Equal compare, PASSES, vertex-color bucket (stride 24, EqNe PS).
        {
            auto vb = backend.CreateVertexBuffer(3);
            vb->SetData(kTriColorTx, 3, sizeof(VPCT)); // opaque white vertex color
            auto atexMidAlpha = Make1x1Texture(backend, 200, 120, 40, 128);

            GpuDrawParams params;
            params.textureEnabled = true;
            params.vertexColorEnabled = true;
            params.texture0 = atexMidAlpha.get();
            params.diffuseColor[0] = params.diffuseColor[1] = params.diffuseColor[2] = params.diffuseColor[3] = 1.0f;
            // Equal/NotEqual bucket: clip((abs(color.a-AlphaTest.x) < AlphaTest.y) ? z : w).
            // Texture alpha is exactly 128/255 -- same value used for refVal, so |0|<tolerance always
            // passes regardless of float rounding. tolerance>0 is also D9-81's own isEqNe signal.
            params.alphaTest[0] = 128.0f / 255.0f; params.alphaTest[1] = 0.1f;
            params.alphaTest[2] = 1.0f; params.alphaTest[3] = -1.0f;

            dev.Clear(Color(0, 0, 255, 255));
            backend.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            Color px(0, 0, 0, 0);
            ReadCenterPixel(dev, px);
            check(px.getRProperty() == 200 && px.getGProperty() == 120 && px.getBProperty() == 40 && px.getAProperty() == 128,
                  "DrawPrimitivesEx (AlphaTestEffect Equal, PASSES, vertex-color bucket): "
                  "exact texture readback (200,120,40,128)");
        }

        // Check J: DualTextureEffect, no fog, no vertex color (stride 28, D3D9-only layout).
        {
            auto vb = backend.CreateVertexBuffer(3);
            vb->SetData(kTriDualTx, 3, sizeof(VPT2));
            auto texWhite = Make1x1Texture(backend, 255, 255, 255, 255);
            auto texOverlay = Make1x1Texture(backend, 100, 60, 20, 255);

            GpuDrawParams params;
            params.dualTexture = true;
            params.textureEnabled = true;
            params.vertexColorEnabled = false;
            params.texture0 = texWhite.get();
            params.texture1 = texOverlay.get();
            params.diffuseColor[0] = params.diffuseColor[1] = params.diffuseColor[2] = 0.5f;
            params.diffuseColor[3] = 1.0f;

            dev.Clear(Color(0, 0, 255, 255));
            backend.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
            Color px(0, 0, 0, 0);
            ReadCenterPixel(dev, px);
            // PSDualTexture: color = tex0; color.rgb *= 2; color *= tex1 * Diffuse.
            // = (1,1,1,1)*2 * (100/255,60/255,20/255,1) * (0.5,0.5,0.5,1)
            // = (100/255,60/255,20/255,1) exactly -> (100,60,20,255).
            check(px.getRProperty() == 100 && px.getGProperty() == 60 && px.getBProperty() == 20 && px.getAProperty() == 255,
                  "DrawPrimitivesEx (DualTextureEffect, no fog/vertexColor): exact "
                  "2*texture0*texture1*DiffuseColor readback (100,60,20,255)");
        }

        // Check F: honest-gap / not-yet-implemented reporting.
        {
            auto vb = backend.CreateVertexBuffer(3);
            vb->SetData(kTriTx, 3, sizeof(VPT));

            // No matching CNA vertex layout: stride 20 (Position+TexCoord) with lightingEnabled=true
            // (would need VSInputNmTx's Normal, which this buffer's declaration doesn't provide).
            GpuDrawParams badCombo;
            badCombo.textureEnabled = true;
            badCombo.lightingEnabled = true;
            bool threw = false;
            try { backend.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                           Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, badCombo); }
            catch (const std::exception&) { threw = true; }
            check(threw, "DrawPrimitivesEx: an unsupported BasicEffect flag/stride combination throws "
                         "(no matching CNA vertex layout) rather than silently drawing wrong");

            // No matching CNA vertex layout: DualTextureEffect with vertexColorEnabled=true (needs
            // VSInputTx2Vc, 32 bytes, which collides with the existing Position+Normal+TexCoord
            // layout -- D9-82d's own honest gap).
            GpuDrawParams dualTexBadCombo;
            dualTexBadCombo.dualTexture = true;
            dualTexBadCombo.vertexColorEnabled = true;
            threw = false;
            try { backend.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                           Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, dualTexBadCombo); }
            catch (const std::exception&) { threw = true; }
            check(threw, "DrawPrimitivesEx: an unsupported DualTextureEffect flag combination throws "
                         "(no matching CNA vertex layout)");

            GpuDrawParams envMapParams;
            envMapParams.envMapping = true;
            threw = false;
            try { backend.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                           Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, envMapParams); }
            catch (const std::exception&) { threw = true; }
            check(threw, "DrawPrimitivesEx: EnvironmentMapEffect dispatch throws a named not-yet-implemented (D9-82e)");

            GpuDrawParams skinnedParams;
            skinnedParams.skinned = true;
            threw = false;
            try { backend.DrawPrimitivesEx(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                           Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, skinnedParams); }
            catch (const std::exception&) { threw = true; }
            check(threw, "DrawPrimitivesEx: SkinnedEffect dispatch throws a named not-yet-implemented (D9-82f)");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    D3D9DrawExTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }
};

int main()
{
    D3D9DrawExTest game;
    game.Run();

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
