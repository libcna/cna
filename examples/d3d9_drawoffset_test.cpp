// SPDX-License-Identifier: MS-PL
// REMED-GFX-060: D3D9 non-zero draw-offset regressions. Proves every effect-aware D3D9 draw path
// honors the XNA DrawPrimitives(vertexStart) / DrawIndexedPrimitives(baseVertex, startIndex)
// offsets carried by GpuDrawParams::vertexStart/startIndex/baseVertex, rather than hardcoding 0 into
// the underlying IDirect3DDevice9::DrawPrimitive(StartVertex) /
// DrawIndexedPrimitive(BaseVertexIndex, MinIndex, NumVertices, StartIndex, PrimitiveCount) calls.
//
// This is the D3D9 counterpart of the D3D11/D3D12 fix REMED-GFX-020 already landed. It was split
// out from GFX-020 (its own Phase-12 cross-renderer sweep confirmed D3D9 had the identical
// hardcoded-zero defect class spread across several draw sites, out of GFX-020's D3D11/D3DCommon
// scope). D3D9 runtime is verifiable via Wine + DXVK9, so unlike the build-only D3D12 half of
// GFX-020 these are genuine pixel regressions.
//
// Discrimination is POSITION-based (shader-independent): two full-screen "half" triangles are laid
// out in the vertex buffer so that the LEFT triangle covers only the LEFT probe pixel (12,32) and
// the RIGHT triangle covers only the RIGHT probe pixel (52,32). Each check requests the offset that
// should select the RIGHT triangle. Post-fix the RIGHT probe is painted and the LEFT probe stays at
// the blue clear color; pre-fix (offset dropped, draw starts at vertex/index 0) the LEFT triangle is
// drawn instead, so the RIGHT probe stays blue and the LEFT probe is painted -- the check fails.
// This distinguishes "correct offset" from "silently drew from zero" from "drew nothing", exactly
// the failure modes the task requires.
//
// Check A -- DrawPrimitives, non-zero vertexStart (BasicEffect unlit+textured, D3D9EffectDraw.cpp).
// Check B -- DrawIndexedPrimitives, non-zero startIndex (BasicEffect, D3D9EffectDraw.cpp).
// Check C -- DrawIndexedPrimitives, non-zero baseVertex (BasicEffect, D3D9EffectDraw.cpp).
// Check D -- DrawIndexedPrimitives, non-zero startIndex AND baseVertex combined (BasicEffect). The
//   middle vertex range is deliberately OFF-SCREEN so ONLY applying BOTH offsets lands on the RIGHT
//   triangle -- any single-offset-only or zero behavior misses the RIGHT probe.
// Check E -- DrawPrimitives, non-zero vertexStart, PbrEffect path (D3D9PbrDraw.cpp).
// Check F -- DrawIndexedPrimitives, non-zero baseVertex, SkinnedVertexColor3D path
//   (D3D9SkinnedVertexColorDraw.cpp).
// Check G -- DrawInstancedPrimitivesEx, non-zero startIndex AND baseVertex, hardware-instancing path
//   (D3D9InstancedDraw.cpp). Uses small per-instance triangles instead of full-screen ones (the
//   instanced path's own geometry convention); the offset must not be confused with the per-instance
//   stream offset.
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

#include "CNA/Internal/Renderers/D3D9/D3D9Renderer.hpp"
#include "CNA/Internal/Renderers/D3D9/D3D9Textures.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::D3D9;
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

    // --- Full-screen "half" geometry (shared by the BasicEffect/Pbr/Skinned checks) -----------
    //
    // Backbuffer is 64x64. The LEFT triangle occupies x<=0 in NDC (apex at the origin, base far to
    // the left), so it covers the LEFT probe (screen 12,32 ~ NDC x=-0.61) and never the RIGHT probe
    // (screen 52,32 ~ NDC x=+0.64). The RIGHT triangle mirrors it (x>=0). Backface culling is
    // disabled in every check so winding is irrelevant.
    struct VPT { float x, y, z, u, v; };                       // stride 20: Position + TexCoord
    struct VPNTanT { float x, y, z, nx, ny, nz, tx, ty, tz, tw, u, v; }; // stride 48: Pbr layout
    struct VPNTWC                                              // stride 56: SkinnedVertexColor layout
    {
        float x, y, z, nx, ny, nz, u, v;
        float w0, w1, w2, w3;
        uint8_t idx[4];
        uint8_t color[4]; // R,G,B,A ascending (UBYTE4N COLOR0)
    };

    // 6-vertex VB: verts 0-2 = LEFT, verts 3-5 = RIGHT.
    const VPT kLeftRightTx[6] = {
        {-3.0f, -3.0f, 0.0f, 0.0f, 0.0f}, {-3.0f, 3.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, // LEFT
        { 3.0f, -3.0f, 0.0f, 0.0f, 0.0f}, { 3.0f, 3.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, // RIGHT
    };
    // 9-vertex VB: 0-2 LEFT, 3-5 OFF-SCREEN (clipped away), 6-8 RIGHT.
    const VPT kLeftOffRightTx[9] = {
        {-3.0f, -3.0f, 0.0f, 0.0f, 0.0f}, {-3.0f, 3.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, // LEFT
        { 5.0f,  5.0f, 0.0f, 0.0f, 0.0f}, { 6.0f, 5.0f, 0.0f, 0.0f, 0.0f}, {5.0f, 6.0f, 0.0f, 0.0f, 0.0f}, // OFF-SCREEN
        { 3.0f, -3.0f, 0.0f, 0.0f, 0.0f}, { 3.0f, 3.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, // RIGHT
    };

    // Pbr (stride 48): same LEFT/RIGHT positions; normal (0,0,1), tangent (1,0,0,1), uv (0,0).
    VPNTanT PbrVert(float x, float y) { return VPNTanT{x, y, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}; }

    // SkinnedVertexColor (stride 56): same LEFT/RIGHT positions, full weight on the (Identity) bone 0,
    // red vertex color.
    VPNTWC SkinnedVert(float x, float y)
    {
        return VPNTWC{x, y, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, {0, 0, 0, 0}, {255, 0, 0, 255}};
    }

    std::unique_ptr<CNA::Internal::Renderers::ITextureRenderer> Make1x1Texture(
        D3D9Renderer& renderer, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        ImageData img;
        img.width = 1;
        img.height = 1;
        img.pixels = {r, g, b, a};
        return renderer.CreateTexture(img);
    }

    Color ReadPixel(GraphicsDevice& dev, int x, int y)
    {
        const Microsoft::Xna::Framework::Rectangle region(x, y, 1, 1);
        std::vector<Color> pixels(1, Color(0, 0, 0, 0));
        dev.GetBackBufferData(&region, pixels.data(), 0, 1);
        return pixels[0];
    }

    bool IsClearBlue(const Color& c)
    {
        return c.getRProperty() == 0 && c.getGProperty() == 0 && c.getBProperty() == 255 && c.getAProperty() == 255;
    }

    // "The intended (RIGHT) probe was painted (i.e. is no longer the clear color) AND the zero-offset
    // (LEFT) probe was NOT painted (still the clear color)." Together these prove the geometry landed
    // where the requested offset dictates and NOT at the offset-0 range. Colour correctness itself is
    // already covered by the per-effect d3d9_drawex/pbr/skinnedvertexcolor tests.
    bool OffsetHonored(const Color& rightProbe, const Color& leftProbe)
    {
        return !IsClearBlue(rightProbe) && IsClearBlue(leftProbe);
    }

    // Left probe (12,32) is inside the LEFT triangle only; right probe (52,32) inside the RIGHT only.
    constexpr int kLeftX = 12, kRightX = 52, kProbeY = 32;
    // Instanced probes (small per-instance triangles centered at NDC -/+0.5 -> screen 16/48); sample
    // slightly off the 45-degree hypotenuse, matching d3d9_instanced_test.cpp's own proven points.
    constexpr int kInstLeftX = 14, kInstRightX = 46, kInstProbeY = 34;
}

class D3D9DrawOffsetTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

    void CommonState(D3D9Renderer& renderer)
    {
        renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, CNA::Internal::Renderers::BlendWriteState{}); // REMED-GFX-077 default write state
        renderer.ApplyDepthStencilState(false, false, 0, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
        renderer.ApplyRasterizerState(0 /*CullMode::None*/, 0 /*FillMode::Solid*/, false, 0.0f, 0.0f);
        renderer.ApplySamplerState(0, 1 /*TextureFilter::Point*/, 0, 0, 1);
    }

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<D3D9Renderer&>(dev.GetRenderer());
        CommonState(renderer);

        auto redTex = Make1x1Texture(renderer, 255, 0, 0);   // painted color for the textured paths
        auto whiteTex = Make1x1Texture(renderer, 255, 255, 255);

        const Matrix I = Matrix::getIdentityProperty();

        auto basicParams = [&](CNA::Internal::Renderers::ITextureRenderer* tex) {
            GpuDrawParams p;
            p.textureEnabled = true;
            p.vertexColorEnabled = false; // stride-20 BasicEffect combo requires this false
            p.texture0 = tex;
            p.diffuseColor[0] = p.diffuseColor[1] = p.diffuseColor[2] = p.diffuseColor[3] = 1.0f;
            return p;
        };

        // Check A: DrawPrimitives, non-zero vertexStart (BasicEffect unlit+textured).
        {
            auto vb = renderer.CreateVertexBuffer(6);
            vb->SetData(kLeftRightTx, 6, sizeof(VPT));
            GpuDrawParams p = basicParams(redTex.get());
            p.vertexStart = 3; // select verts 3..5 = RIGHT triangle
            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawPrimitivesEx(*vb, I, I, I, PrimitiveType::TriangleList, 1, p);
            check(OffsetHonored(ReadPixel(dev, kRightX, kProbeY), ReadPixel(dev, kLeftX, kProbeY)),
                  "DrawPrimitives vertexStart=3 (BasicEffect): RIGHT triangle drawn, not vertices 0..2");
        }

        // Check B: DrawIndexedPrimitives, non-zero startIndex.
        {
            auto vb = renderer.CreateVertexBuffer(6);
            vb->SetData(kLeftRightTx, 6, sizeof(VPT));
            const uint16_t idx[6] = {0, 1, 2, 3, 4, 5};
            auto ib = renderer.CreateIndexBuffer16(6);
            ib->SetData16(idx, 6);
            GpuDrawParams p = basicParams(redTex.get());
            p.startIndex = 3; // read indices at IB position 3..5 -> verts 3..5 = RIGHT
            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawIndexedPrimitivesEx(*vb, *ib, I, I, I, PrimitiveType::TriangleList, 1, p);
            check(OffsetHonored(ReadPixel(dev, kRightX, kProbeY), ReadPixel(dev, kLeftX, kProbeY)),
                  "DrawIndexedPrimitives startIndex=3 (BasicEffect): reads IB from index 3, RIGHT drawn");
        }

        // Check C: DrawIndexedPrimitives, non-zero baseVertex.
        {
            auto vb = renderer.CreateVertexBuffer(6);
            vb->SetData(kLeftRightTx, 6, sizeof(VPT));
            const uint16_t idx[3] = {0, 1, 2};
            auto ib = renderer.CreateIndexBuffer16(3);
            ib->SetData16(idx, 3);
            GpuDrawParams p = basicParams(redTex.get());
            p.baseVertex = 3; // each index + 3 -> verts 3..5 = RIGHT
            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawIndexedPrimitivesEx(*vb, *ib, I, I, I, PrimitiveType::TriangleList, 1, p);
            check(OffsetHonored(ReadPixel(dev, kRightX, kProbeY), ReadPixel(dev, kLeftX, kProbeY)),
                  "DrawIndexedPrimitives baseVertex=3 (BasicEffect): index+baseVertex -> RIGHT drawn");
        }

        // Check D: DrawIndexedPrimitives, startIndex AND baseVertex combined. Only applying BOTH
        // lands on the RIGHT triangle (verts 6..8); the middle range (verts 3..5) is off-screen, so
        // any single-offset behavior draws nothing at either probe and zero-offset draws the LEFT.
        {
            auto vb = renderer.CreateVertexBuffer(9);
            vb->SetData(kLeftOffRightTx, 9, sizeof(VPT));
            const uint16_t idx[6] = {0, 1, 2, 3, 4, 5};
            auto ib = renderer.CreateIndexBuffer16(6);
            ib->SetData16(idx, 6);
            GpuDrawParams p = basicParams(redTex.get());
            p.startIndex = 3; // IB[3..5] = indices 3,4,5
            p.baseVertex = 3; //   + 3 -> verts 6,7,8 = RIGHT
            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawIndexedPrimitivesEx(*vb, *ib, I, I, I, PrimitiveType::TriangleList, 1, p);
            check(OffsetHonored(ReadPixel(dev, kRightX, kProbeY), ReadPixel(dev, kLeftX, kProbeY)),
                  "DrawIndexedPrimitives startIndex=3 + baseVertex=3 (BasicEffect): BOTH offsets -> RIGHT drawn");
        }

        // Check E: DrawPrimitives, non-zero vertexStart, PbrEffect path (D3D9PbrDraw.cpp). Ambient-only
        // (all lights zeroed) so the PBR PS collapses to texture*DiffuseColor = red.
        {
            const VPNTanT verts[6] = {
                PbrVert(-3.0f, -3.0f), PbrVert(-3.0f, 3.0f), PbrVert(0.0f, 0.0f), // LEFT
                PbrVert( 3.0f, -3.0f), PbrVert( 3.0f, 3.0f), PbrVert(0.0f, 0.0f), // RIGHT
            };
            auto vb = renderer.CreateVertexBuffer(6);
            vb->SetData(verts, 6, sizeof(VPNTanT));
            GpuDrawParams p;
            p.pbr = true;
            p.texture0 = redTex.get();
            p.diffuseColor[0] = p.diffuseColor[1] = p.diffuseColor[2] = p.diffuseColor[3] = 1.0f;
            p.ambientColor[0] = p.ambientColor[1] = p.ambientColor[2] = 1.0f;
            p.emissiveColor[0] = p.emissiveColor[1] = p.emissiveColor[2] = 0.0f;
            p.light0Diffuse[0] = p.light0Diffuse[1] = p.light0Diffuse[2] = 0.0f;
            p.light1Diffuse[0] = p.light1Diffuse[1] = p.light1Diffuse[2] = 0.0f;
            p.light2Diffuse[0] = p.light2Diffuse[1] = p.light2Diffuse[2] = 0.0f;
            p.pbrMetallicFactor = 0.0f;
            p.pbrRoughnessFactor = 1.0f;
            p.vertexStart = 3; // verts 3..5 = RIGHT
            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawPrimitivesEx(*vb, I, I, I, PrimitiveType::TriangleList, 1, p);
            check(OffsetHonored(ReadPixel(dev, kRightX, kProbeY), ReadPixel(dev, kLeftX, kProbeY)),
                  "DrawPrimitives vertexStart=3 (PbrEffect, D3D9PbrDraw): RIGHT triangle drawn");
        }

        // Check F: DrawIndexedPrimitives, non-zero baseVertex, SkinnedVertexColor3D path (stride 56).
        {
            const VPNTWC verts[6] = {
                SkinnedVert(-3.0f, -3.0f), SkinnedVert(-3.0f, 3.0f), SkinnedVert(0.0f, 0.0f), // LEFT
                SkinnedVert( 3.0f, -3.0f), SkinnedVert( 3.0f, 3.0f), SkinnedVert(0.0f, 0.0f), // RIGHT
            };
            auto vb = renderer.CreateVertexBuffer(6);
            vb->SetData(verts, 6, sizeof(VPNTWC));
            const uint16_t idx[3] = {0, 1, 2};
            auto ib = renderer.CreateIndexBuffer16(3);
            ib->SetData16(idx, 3);
            GpuDrawParams p;
            p.skinned = true;
            p.vertexColorEnabled = true;
            p.texture0 = whiteTex.get();
            p.diffuseColor[0] = p.diffuseColor[1] = p.diffuseColor[2] = p.diffuseColor[3] = 1.0f;
            p.emissiveColor[0] = p.emissiveColor[1] = p.emissiveColor[2] = 1.0f; // pre-folded ambient
            p.light0Diffuse[0] = p.light0Diffuse[1] = p.light0Diffuse[2] = 0.0f;
            p.light1Diffuse[0] = p.light1Diffuse[1] = p.light1Diffuse[2] = 0.0f;
            p.light2Diffuse[0] = p.light2Diffuse[1] = p.light2Diffuse[2] = 0.0f;
            p.specularColor[0] = p.specularColor[1] = p.specularColor[2] = 0.0f;
            p.weightsPerVertex = 1;
            p.boneCount = 1;
            const float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            for (int i = 0; i < 16; ++i) p.boneTransforms[i] = ident[i];
            p.baseVertex = 3; // index + 3 -> verts 3..5 = RIGHT
            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawIndexedPrimitivesEx(*vb, *ib, I, I, I, PrimitiveType::TriangleList, 1, p);
            check(OffsetHonored(ReadPixel(dev, kRightX, kProbeY), ReadPixel(dev, kLeftX, kProbeY)),
                  "DrawIndexedPrimitives baseVertex=3 (SkinnedVertexColor3D, D3D9SkinnedVertexColorDraw): RIGHT drawn");
        }

        // Check G: DrawInstancedPrimitivesEx, startIndex AND baseVertex combined (D3D9InstancedDraw).
        // Small per-instance triangles: verts 0..2 = A (screen ~16), 3..5 off-screen, 6..8 = B
        // (screen ~48). One instance, identity translation. Applying BOTH offsets must select B; the
        // per-instance stream offset must not be confused with baseVertex/startIndex.
        {
            struct VP { float x, y, z; };
            const VP verts[9] = {
                {-0.6f, -0.1f, 0.0f}, {-0.4f, -0.1f, 0.0f}, {-0.6f, 0.1f, 0.0f}, // A (left)
                { 5.0f,  5.0f, 0.0f}, { 6.0f,  5.0f, 0.0f}, { 5.0f, 6.0f, 0.0f}, // off-screen
                { 0.4f, -0.1f, 0.0f}, { 0.6f, -0.1f, 0.0f}, { 0.4f, 0.1f, 0.0f}, // B (right)
            };
            auto vb = renderer.CreateVertexBuffer(9);
            vb->SetData(verts, 9, sizeof(VP));
            const uint16_t idx[6] = {0, 1, 2, 3, 4, 5};
            auto ib = renderer.CreateIndexBuffer16(6);
            ib->SetData16(idx, 6);

            struct InstanceRow { float row0[4], row1[4], row2[4], row3[4]; };
            const InstanceRow instances[1] = {{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}}}; // identity
            auto instVb = renderer.CreateVertexBuffer(1);
            instVb->SetData(instances, 1, sizeof(InstanceRow));

            GpuDrawParams p;
            // REMED-GFX-202: the classic two-stream instanced binding set.
            SetInstancedVertexStreamsEXT(p, *vb, *instVb, /*instanceFrequency=*/1,
                                         static_cast<int>(sizeof(VP)), 0,
                                         static_cast<int>(sizeof(InstanceRow)), 0);
            p.instanceCount = 1;
            p.diffuseColor[0] = 0.0f; p.diffuseColor[1] = 1.0f; p.diffuseColor[2] = 0.0f; p.diffuseColor[3] = 1.0f;
            p.startIndex = 3; // IB[3..5] = indices 3,4,5
            p.baseVertex = 3; //   + 3 -> verts 6,7,8 = B (right)
            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawInstancedPrimitivesEx(*vb, *ib, I, I, I, PrimitiveType::TriangleList, 1, 1, p);
            check(OffsetHonored(ReadPixel(dev, kInstRightX, kInstProbeY), ReadPixel(dev, kInstLeftX, kInstProbeY)),
                  "DrawInstancedPrimitivesEx startIndex=3 + baseVertex=3 (D3D9InstancedDraw): instance B drawn");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    D3D9DrawOffsetTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }
};

int main()
{
    D3D9DrawOffsetTest game;
    game.Run();

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
