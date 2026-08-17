// SPDX-License-Identifier: MS-PL
// PBR + skinning combo golden-image test for SkinnedPbrEffect's shader
// (EasyGLRenderer::EnsurePbrSkinnedProgram()) -- proves the stride-68
// VertexPositionNormalTangentTextureSkinned layout, the bone-palette skin transform applied to
// Position/Normal/Tangent, and the PBR BRDF fragment stage all work end-to-end via a real GPU
// draw.
//
// Reuses easygl_pbreffect_golden_test.cpp's exact 4-quad scene (same camera, same lights, same
// per-quad material setup) but through SkinnedPbrEffect with a single identity bone (weight 1.0
// on bone index 0, SkinnedEffect's own "Task 406 identity-bone case" precedent). An identity bind
// pose is a mathematical no-op for the skin transform, so the rendered pixels must be identical
// to PbrEffect's own already-verified golden values -- this is the test's own oracle, not a
// freshly hand-derived number, and directly proves the skin matrix multiply in
// EnsurePbrSkinnedProgram()'s vertex shader is wired correctly (a bug there would shift/darken the
// quads relative to the unskinned PbrEffect result).

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // Stride-68 GPU-compact skinned PBR vertex: matches ApplyLayout's stride==68 case
    // (Position+Normal+Tangent+TextureCoordinate+BlendWeight+BlendIndices).
    struct SkinnedPbrGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedPbrGpuVertex) == 68, "skinned PBR vertex must be 68 bytes");

    void AppendQuad(std::vector<SkinnedPbrGpuVertex>& out, float xMin, float xMax)
    {
        const SkinnedPbrGpuVertex tl{ xMin,  1, 0,  0,0,1,  1,0,0,1,  0,0,  1,0,0,0,  0,0,0,0 };
        const SkinnedPbrGpuVertex bl{ xMin, -1, 0,  0,0,1,  1,0,0,1,  0,1,  1,0,0,0,  0,0,0,0 };
        const SkinnedPbrGpuVertex br{ xMax, -1, 0,  0,0,1,  1,0,0,1,  1,1,  1,0,0,0,  0,0,0,0 };
        const SkinnedPbrGpuVertex tr{ xMax,  1, 0,  0,0,1,  1,0,0,1,  1,0,  1,0,0,0,  0,0,0,0 };
        out.push_back(tl); out.push_back(bl); out.push_back(br);
        out.push_back(tl); out.push_back(br); out.push_back(tr);
    }
}

class SkinnedPbrEffectGoldenTest : public CNA::Examples::PixelTestGame
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
        const std::vector<std::uint8_t> tiltedNormal = { 255, 128, 128, 255 };
        Texture2D tiltedNormalTex = Texture2D::CreateFromPixels(device, 1, 1, tiltedNormal);

        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        SkinnedPbrEffect fx(device);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());

        std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        fx.EnableDefaultLighting();

        std::vector<SkinnedPbrGpuVertex> verts;
        AppendQuad(verts, -1.0f, -0.5f);  // quad A: x in [-1,-0.5]
        AppendQuad(verts, -0.34f, 0.16f); // quad B
        AppendQuad(verts, 0.17f, 0.67f);  // quad C (metallic)
        AppendQuad(verts, 0.68f, 1.0f);   // quad D (dielectric)

        VertexBuffer vb(device, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(SkinnedPbrGpuVertex)));
        device.SetVertexBuffer(&vb);

        // Quad A: white, fully rough, non-metallic, flat normal.
        fx.setTextureProperty(&whiteTex);
        fx.setNormalMapProperty(nullptr);
        fx.setRoughnessFactorProperty(1.0f);
        fx.setMetallicFactorProperty(0.0f);
        fx.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        // Quad B: same as A, but with a tilted normal map.
        fx.setNormalMapProperty(&tiltedNormalTex);
        fx.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 6, 2);

        // Quad C: red, fully metallic, flat normal.
        fx.setTextureProperty(&redTex);
        fx.setNormalMapProperty(nullptr);
        fx.setMetallicFactorProperty(1.0f);
        fx.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 12, 2);

        // Quad D: red, fully dielectric, flat normal.
        fx.setMetallicFactorProperty(0.0f);
        fx.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 18, 2);

        const int sampleY = H / 2;
        const int sampleAx = W * 1 / 8;
        const int sampleBx = W * 3 / 8;
        const int sampleCx = W * 5 / 8;
        const int sampleDx = W * 7 / 8;

        // Identity bind pose is a no-op skin transform, so these are exactly
        // easygl_pbreffect_golden_test.cpp's own already-verified expected values -- the shared
        // oracle this file's own header comment describes.
        ExpectPixel("quadA-flat-normal-lit", Rectangle(sampleAx, sampleY, 1, 1),
                    Color(137, 146, 158, 255), /*tolerance=*/20);
        ExpectPixel("quadB-tilted-normal-darker", Rectangle(sampleBx, sampleY, 1, 1),
                    Color(129, 139, 152, 255), /*tolerance=*/20);
        ExpectPixel("quadC-metallic-red", Rectangle(sampleCx, sampleY, 1, 1),
                    Color(117, 13, 11, 255), /*tolerance=*/20);
        ExpectPixel("quadD-dielectric-red", Rectangle(sampleDx, sampleY, 1, 1),
                    Color(137, 23, 21, 255), /*tolerance=*/25);

        CompareGoldenImage("skinnedpbreffect-quadA", Rectangle(sampleAx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_pbreffect_golden_test_a.png", /*tolerance=*/35);
        CompareGoldenImage("skinnedpbreffect-quadB", Rectangle(sampleBx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_pbreffect_golden_test_b.png", /*tolerance=*/25);
        CompareGoldenImage("skinnedpbreffect-quadC", Rectangle(sampleCx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_pbreffect_golden_test_c.png", /*tolerance=*/20);
        CompareGoldenImage("skinnedpbreffect-quadD", Rectangle(sampleDx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_pbreffect_golden_test_d.png", /*tolerance=*/30);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<SkinnedPbrEffectGoldenTest>();
}
