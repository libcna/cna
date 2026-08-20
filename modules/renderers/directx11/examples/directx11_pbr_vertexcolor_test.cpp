// SPDX-License-Identifier: MS-PL
// plans/plan_cnj.md CNB-58/CNB-67 follow-up: real-GPU pixel test for the D3D11 renderer's new
// PbrEffect (stride 48)/SkinnedPbrEffect (stride 68)/SkinnedEffect vertex-color-on-skinned-mesh
// (stride 56, Skinned3dColored/Skinned3dVertexLitColored) shader variants -- proves each new
// stride/variant is actually selected and executes correctly end-to-end via a real D3D11 draw
// (Wine+DXVK on a real GPU), not just "does it link".
//
// Exact analytic BRDF values are impractical to hand-derive for a realistically-lit PBR scene
// (see easygl_pbreffect_golden_test.cpp's own header comment for why) -- so quads A/B instead use
// a degenerate, EXACTLY hand-derivable scene: no directional lights enabled and AmbientLightColor
// left at its default zero, so PbrLight()'s own returned contribution is (0,0,0,0) for every
// light (multiplied by a zero lightColor) and the ambient term is zero too -- the shader's output
// collapses to EmissiveFactor * emissiveMap exactly. Quad A leaves both the base-colour map and
// EmissiveMap unbound so the result also exercises the default-white-SRV fallback path
// (DirectX11Renderer::GetOrCreateDefaultWhiteSrvEXT). EmissiveFactor=(1,0,0) with 0/1-only
// channel values is also gamma/sRGB-invariant (the sRGB transfer function fixes both endpoints),
// so the expected pixel is exactly pure red regardless of the swapchain's sRGB back buffer format.
//
// Quads C/D reuse easygl_skinnedeffect_vertexcolor_test.cpp's own established, lighting-
// independent technique: a pure-black per-vertex Color with VertexColorEnabled=true must zero the
// final combined (diffuse+specular) output to exact black regardless of what the lit/textured
// color would otherwise have been -- an unambiguous proof that Skinned3dColored (quad C,
// PreferPerPixelLighting=true) and Skinned3dVertexLitColored (quad D, PreferPerPixelLighting=false,
// real XNA's own SkinnedEffect default) both genuinely read and apply the new Color vertex
// attribute, not just that stride 56 links/binds without crashing.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // Stride-48 GPU-compact PBR vertex: Position+Normal+Tangent+TextureCoordinate
    // (VertexPositionNormalTangentTexture, matches D3DVertexFormatHelper's kStride48).
    struct PbrGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PbrGpuVertex) == 48, "PBR vertex must be 48 bytes");

    void AppendPbrQuad(std::vector<PbrGpuVertex>& out, float xMin, float xMax)
    {
        const PbrGpuVertex tl{ xMin,  1, 0,  0,0,1,  1,0,0,1,  0,0 };
        const PbrGpuVertex bl{ xMin, -1, 0,  0,0,1,  1,0,0,1,  0,1 };
        const PbrGpuVertex br{ xMax, -1, 0,  0,0,1,  1,0,0,1,  1,1 };
        const PbrGpuVertex tr{ xMax,  1, 0,  0,0,1,  1,0,0,1,  1,0 };
        out.push_back(tl); out.push_back(bl); out.push_back(br);
        out.push_back(tl); out.push_back(br); out.push_back(tr);
    }

    // Stride-68 GPU-compact skinned-PBR vertex: the stride-48 layout above with the stride-52
    // skinning suffix appended (VertexPositionNormalTangentTextureSkinned, matches
    // D3DVertexFormatHelper's kStride68). A single identity bone (weight 1.0, index 0) is used, so
    // skinning is a geometric no-op here -- this test isolates "does the PBR+skinning combo
    // dispatch/cbuffer wiring work", not skinning math itself (already covered by the existing
    // DirectX11_Smoke skinned specular checks, DX-151).
    struct PbrSkinnedGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(PbrSkinnedGpuVertex) == 68, "PBR skinned vertex must be 68 bytes");

    void AppendPbrSkinnedQuad(std::vector<PbrSkinnedGpuVertex>& out, float xMin, float xMax)
    {
        const PbrSkinnedGpuVertex tl{ xMin,  1, 0,  0,0,1,  1,0,0,1,  0,0,  1,0,0,0,  0,0,0,0 };
        const PbrSkinnedGpuVertex bl{ xMin, -1, 0,  0,0,1,  1,0,0,1,  0,1,  1,0,0,0,  0,0,0,0 };
        const PbrSkinnedGpuVertex br{ xMax, -1, 0,  0,0,1,  1,0,0,1,  1,1,  1,0,0,0,  0,0,0,0 };
        const PbrSkinnedGpuVertex tr{ xMax,  1, 0,  0,0,1,  1,0,0,1,  1,0,  1,0,0,0,  0,0,0,0 };
        out.push_back(tl); out.push_back(bl); out.push_back(br);
        out.push_back(tl); out.push_back(br); out.push_back(tr);
    }

    // Stride-56 GPU-compact skinned+Color vertex: matches D3DVertexFormatHelper's kStride56
    // (the stride-52 SkinnedVertex layout with a per-vertex Color appended at offset 52,
    // CNB-67) -- identical layout to easygl_skinnedeffect_vertexcolor_test.cpp's own
    // SkinnedColorGpuVertex.
    struct SkinnedColorGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(SkinnedColorGpuVertex) == 56, "skinned+color vertex must be 56 bytes");

    void AppendColorQuad(std::vector<SkinnedColorGpuVertex>& out, float xMin, float xMax,
                          std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
    {
        const SkinnedColorGpuVertex tl{ xMin,  1, 0,  0,0,1,  0,0,  1,0,0,0,  0,0,0,0,  r,g,b,a };
        const SkinnedColorGpuVertex bl{ xMin, -1, 0,  0,0,1,  0,1,  1,0,0,0,  0,0,0,0,  r,g,b,a };
        const SkinnedColorGpuVertex br{ xMax, -1, 0,  0,0,1,  1,1,  1,0,0,0,  0,0,0,0,  r,g,b,a };
        const SkinnedColorGpuVertex tr{ xMax,  1, 0,  0,0,1,  1,0,  1,0,0,0,  0,0,0,0,  r,g,b,a };
        out.push_back(tl); out.push_back(bl); out.push_back(br);
        out.push_back(tl); out.push_back(br); out.push_back(tr);
    }
}

class D3D11PbrVertexColorTest : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        const std::vector<std::uint8_t> white = { 255, 255, 255, 255 };
        Texture2D whiteTex = Texture2D::CreateFromPixels(device, 1, 1, white);
        const std::vector<std::uint8_t> red = { 255, 0, 0, 255 };
        Texture2D redTex = Texture2D::CreateFromPixels(device, 1, 1, red);

        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const int sampleY = H / 2;

        // --- Quad A: PbrEffect (stride 48, Pbr3d), emissive-only, no lights/ambient. ---
        {
            PbrEffect fx(device);
            fx.setBaseColorTextureIsSrgbEXTProperty(false);
            fx.setEmissiveTextureIsSrgbEXTProperty(false);
            fx.setEncodeOutputToSrgbEXTProperty(false);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setEmissiveFactorProperty(Vector3(1.0f, 0.0f, 0.0f));
            // BaseColorTexture/NormalMap/MetallicRoughnessMap/EmissiveMap/OcclusionMap are all
            // deliberately unbound. The base fallback must contribute opaque-white alpha while
            // the emissive fallback contributes white RGB, yielding exact opaque red.

            std::vector<PbrGpuVertex> verts;
            AppendPbrQuad(verts, -1.0f, -0.5f);
            VertexBuffer vb(device, static_cast<int>(verts.size()));
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(PbrGpuVertex)));
            device.SetVertexBuffer(&vb);

            fx.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

            const int sampleAx = W / 8;
            ExpectPixel("pbr3d-unskinned-emissive-only", Rectangle(sampleAx, sampleY, 1, 1),
                        Color(255, 0, 0, 255), /*tolerance=*/8);
        }

        // --- Quad B: SkinnedPbrEffect (stride 68, PbrSkinned3d), emissive-only, identity bone. ---
        {
            SkinnedPbrEffect fx(device);
            fx.setBaseColorTextureIsSrgbEXTProperty(false);
            fx.setEmissiveTextureIsSrgbEXTProperty(false);
            fx.setEncodeOutputToSrgbEXTProperty(false);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureProperty(&whiteTex);
            fx.setEmissiveFactorProperty(Vector3(1.0f, 0.0f, 0.0f));
            std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
            fx.SetBoneTransforms(bones);
            fx.setWeightsPerVertexProperty(1);

            std::vector<PbrSkinnedGpuVertex> verts;
            AppendPbrSkinnedQuad(verts, -0.34f, 0.16f);
            VertexBuffer vb(device, static_cast<int>(verts.size()));
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(PbrSkinnedGpuVertex)));
            device.SetVertexBuffer(&vb);

            fx.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

            const int sampleBx = W * 3 / 8;
            ExpectPixel("pbrskinned3d-emissive-only-identity-bone", Rectangle(sampleBx, sampleY, 1, 1),
                        Color(255, 0, 0, 255), /*tolerance=*/8);
        }

        // --- Quad C/D: SkinnedEffect (stride 56), VertexColorEnabled=true with a pure-black
        // per-vertex color must zero the final output, for BOTH the per-pixel-lit
        // (Skinned3dColored) and vertex-lit (Skinned3dVertexLitColored, real XNA's own
        // PreferPerPixelLighting=false default) shader variants. ---
        {
            SkinnedEffect fx(device);
            fx.setTextureProperty(&redTex);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
            fx.SetBoneTransforms(bones);
            fx.setWeightsPerVertexProperty(1);
            fx.EnableDefaultLighting();
            fx.VertexColorEnabled = true;

            std::vector<SkinnedColorGpuVertex> verts;
            AppendColorQuad(verts, 0.17f, 0.67f, 0, 0, 0, 255);  // quad C
            AppendColorQuad(verts, 0.68f, 1.0f,  0, 0, 0, 255);  // quad D

            VertexBuffer vb(device, static_cast<int>(verts.size()));
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(SkinnedColorGpuVertex)));
            device.SetVertexBuffer(&vb);

            // Quad C: PreferPerPixelLighting=true -> Skinned3dColored.
            fx.setPreferPerPixelLightingProperty(true);
            fx.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

            // Quad D: PreferPerPixelLighting=false (XNA's real default) -> Skinned3dVertexLitColored.
            fx.setPreferPerPixelLightingProperty(false);
            fx.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 6, 2);

            const int sampleCx = W * 5 / 8;
            const int sampleDx = W * 7 / 8;
            ExpectPixel("skinned3dcolored-black-vertexcolor", Rectangle(sampleCx, sampleY, 1, 1),
                        Color(0, 0, 0, 255), /*tolerance=*/10);
            ExpectPixel("skinned3dvertexlitcolored-black-vertexcolor", Rectangle(sampleDx, sampleY, 1, 1),
                        Color(0, 0, 0, 255), /*tolerance=*/10);
        }

        // --- REMED-GFX-020 regression: DrawPrimitives() must honor a non-zero startVertex. ---
        // The quad-D failure above was ROOT-CAUSED not to vertex color but to
        // DirectX11Renderer ignoring GpuDrawParams::vertexStart (context->Draw() hardcoded
        // StartVertexLocation=0), so DrawPrimitives(..., startVertex=6, ...) silently redrew the
        // first quad's vertices and left quad D's screen region at the clear color. This dedicated,
        // lighting-independent check pins the fix: a single 2-quad PbrEffect emissive VB (pure-red
        // output, gamma-invariant, no lighting) with quad-0 on screen-left and quad-1 on
        // screen-right, of which ONLY quad-1 is drawn -- via startVertex=6. With the offset honored,
        // screen-right is red and screen-left stays the green clear; a regressed (ignored) offset
        // would draw quad-0 instead, making screen-left red and screen-right green -- both asserts
        // below flip, so this discriminates the exact defect.
        {
            PbrEffect fx(device);
            fx.setBaseColorTextureIsSrgbEXTProperty(false);
            fx.setEmissiveTextureIsSrgbEXTProperty(false);
            fx.setEncodeOutputToSrgbEXTProperty(false);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureProperty(&whiteTex);
            fx.setEmissiveFactorProperty(Vector3(1.0f, 0.0f, 0.0f));

            std::vector<PbrGpuVertex> verts;
            AppendPbrQuad(verts, -1.0f, -0.5f);  // quad-0: screen-left  (vertices 0..5)
            AppendPbrQuad(verts,  0.5f,  1.0f);  // quad-1: screen-right (vertices 6..11)
            VertexBuffer vb(device, static_cast<int>(verts.size()));
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(PbrGpuVertex)));

            device.Clear(Color(0, 255, 0, 255));
            device.SetDepthTestEnabled(false);
            device.setBlendStateProperty(BlendState::Opaque);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.SetVertexBuffer(&vb);
            fx.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 6, 2);  // draw ONLY quad-1

            const int leftX  = W / 8;
            const int rightX = W * 7 / 8;
            ExpectPixel("startvertex-offset-right-quad-drawn", Rectangle(rightX, sampleY, 1, 1),
                        Color(255, 0, 0, 255), /*tolerance=*/8);
            ExpectPixel("startvertex-offset-left-quad-untouched", Rectangle(leftX, sampleY, 1, 1),
                        Color(0, 255, 0, 255), /*tolerance=*/8);
        }
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<D3D11PbrVertexColorTest>();
}
