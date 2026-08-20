// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: cross-renderer visual-parity proof -- reuses
// examples/easygl_pbreffect_golden_test.cpp's own glTF metallic-roughness BRDF scene and
// hand-derived/captured expected values verbatim (4 quads: A=white/rough/non-metallic/flat
// normal, B=same as A with a tilted normal map, C=red/fully-metallic, D=red/fully-dielectric).
// Both renderers implement the same physically-based glTF reference BRDF, so the same expected
// values should hold; see that file's own header comment for the full derivation/rationale.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    struct PbrGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PbrGpuVertex) == 48, "PBR vertex must be 48 bytes");

    void AppendQuad(std::vector<PbrGpuVertex>& out, float xMin, float xMax)
    {
        const PbrGpuVertex tl{ xMin,  1, 0,  0,0,1,  1,0,0,1,  0,0 };
        const PbrGpuVertex bl{ xMin, -1, 0,  0,0,1,  1,0,0,1,  0,1 };
        const PbrGpuVertex br{ xMax, -1, 0,  0,0,1,  1,0,0,1,  1,1 };
        const PbrGpuVertex tr{ xMax,  1, 0,  0,0,1,  1,0,0,1,  1,0 };
        out.push_back(tl); out.push_back(bl); out.push_back(br);
        out.push_back(tl); out.push_back(br); out.push_back(tr);
    }
}

class OpenGL2PbrEffectGoldenTest : public CNA::Examples::PixelTestGame
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

        PbrEffect fx(device);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.EnableDefaultLighting();

        std::vector<PbrGpuVertex> verts;
        AppendQuad(verts, -1.0f, -0.5f);  // quad A
        AppendQuad(verts, -0.34f, 0.16f); // quad B
        AppendQuad(verts, 0.17f, 0.67f);  // quad C (metallic)
        AppendQuad(verts, 0.68f, 1.0f);   // quad D (dielectric)

        VertexBuffer vb(device, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(PbrGpuVertex)));
        device.SetVertexBuffer(&vb);

        fx.setTextureProperty(&whiteTex);
        fx.setNormalMapProperty(nullptr);
        fx.setRoughnessFactorProperty(1.0f);
        fx.setMetallicFactorProperty(0.0f);
        fx.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        fx.setNormalMapProperty(&tiltedNormalTex);
        fx.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 6, 2);

        fx.setTextureProperty(&redTex);
        fx.setNormalMapProperty(nullptr);
        fx.setMetallicFactorProperty(1.0f);
        fx.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 12, 2);

        fx.setMetallicFactorProperty(0.0f);
        fx.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 18, 2);

        const int sampleY = H / 2;
        const int sampleAx = W * 1 / 8;
        const int sampleBx = W * 3 / 8;
        const int sampleCx = W * 5 / 8;
        const int sampleDx = W * 7 / 8;

        ExpectPixel("quadA-flat-normal-lit", Rectangle(sampleAx, sampleY, 1, 1),
                    Color(64, 74, 87, 255), /*tolerance=*/20);
        ExpectPixel("quadB-tilted-normal-darker", Rectangle(sampleBx, sampleY, 1, 1),
                    Color(56, 65, 80, 255), /*tolerance=*/20);
        ExpectPixel("quadC-metallic-red", Rectangle(sampleCx, sampleY, 1, 1),
                    Color(45, 1, 1, 255), /*tolerance=*/20);
        ExpectPixel("quadD-dielectric-red", Rectangle(sampleDx, sampleY, 1, 1),
                    Color(63, 2, 2, 255), /*tolerance=*/25);

        CompareGoldenImage("pbreffect-quadA-vs-easygl", Rectangle(sampleAx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_pbreffect_golden_test_a.png", /*tolerance=*/35);
        CompareGoldenImage("pbreffect-quadB-vs-easygl", Rectangle(sampleBx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_pbreffect_golden_test_b.png", /*tolerance=*/25);
        CompareGoldenImage("pbreffect-quadC-vs-easygl", Rectangle(sampleCx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_pbreffect_golden_test_c.png", /*tolerance=*/20);
        CompareGoldenImage("pbreffect-quadD-vs-easygl", Rectangle(sampleDx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_pbreffect_golden_test_d.png", /*tolerance=*/30);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL2PbrEffectGoldenTest>();
}
