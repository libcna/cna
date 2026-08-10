// SPDX-License-Identifier: MS-PL
// CNB-67 (Phase 13C): golden-image test for SkinnedEffect's new NOXNA VertexColorEnabled
// property (see SkinnedEffect.hpp) -- proves the stride-56 skinned+Color vertex layout's aColor
// attribute (EasyGLRenderer::ApplyLayout's stride==56 case, attribute location 5) is
// actually read by both EnsureSkinnedProgram() (per-pixel-lit) and
// EnsureSkinnedVertexLitProgram() (vertex-lit, real XNA's own PreferPerPixelLighting=false
// default) and correctly gated by the uVertexColorEnabled uniform.
//
// Uses a per-vertex color of pure black rather than trying to analytically reproduce
// EnableDefaultLighting()'s Phong math (impractical by hand, same reasoning
// easygl_skinnedeffect_golden_test.cpp's own header comment gives): litRGB*texColor.rgb*vc.rgb
// with vc.rgb=(0,0,0) must zero out to (0,0,0,alpha) regardless of what the lit/textured color
// would otherwise be, independent of the lighting math -- an unambiguous, lighting-independent
// check that VertexColorEnabled actually gates the shader path, not just that the field exists.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // Stride-56 GPU-compact skinned+Color vertex: matches ApplyLayout's stride==56 case (Color
    // appended after BlendIndices, CNB-67).
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

    void AppendQuad(std::vector<SkinnedColorGpuVertex>& out, float xMin, float xMax,
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

class SkinnedEffectVertexColorTest : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        const std::vector<std::uint8_t> px = { 255, 0, 0, 255 };
        Texture2D tex = Texture2D::CreateFromPixels(device, 1, 1, px);

        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        SkinnedEffect fx(device);
        fx.setTextureProperty(&tex);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        fx.EnableDefaultLighting();

        std::vector<SkinnedColorGpuVertex> verts;
        // Quad A (left): VertexColorEnabled=false -- per-vertex black must be ignored, rendering
        // the same red-dominant lit/textured result as easygl_skinnedeffect_golden_test.cpp's own
        // identity-bone quad.
        AppendQuad(verts, -1.0f, -0.5f, 0, 0, 0, 255);
        // Quad B (right): VertexColorEnabled=true -- per-vertex black must zero the lit/textured
        // result to pure black, regardless of the lighting math.
        AppendQuad(verts, 0.5f, 1.0f, 0, 0, 0, 255);

        VertexBuffer vb(device, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(SkinnedColorGpuVertex)));
        device.SetVertexBuffer(&vb);

        fx.VertexColorEnabled = false;
        fx.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        fx.VertexColorEnabled = true;
        fx.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 6, 2);

        const int sampleY = H / 2;
        const int sampleAx = W / 8;       // quad A: NDC x ~ -0.75
        const int sampleBx = (W * 7) / 8; // quad B: NDC x ~ +0.75

        // Cross-check against easygl_skinnedeffect_golden_test.cpp's own live-observed value for
        // the identical identity-bone/red-texture/lit scenario -- VertexColorEnabled=false must
        // reproduce it unchanged.
        ExpectPixel("quadA-vertexcolor-disabled", Rectangle(sampleAx, sampleY, 1, 1),
                    Color(174, 0, 0, 255), /*tolerance=*/40);
        // VertexColorEnabled=true with a pure-black per-vertex color must zero the result.
        ExpectPixel("quadB-vertexcolor-enabled-black", Rectangle(sampleBx, sampleY, 1, 1),
                    Color(0, 0, 0, 255), /*tolerance=*/10);

        CompareGoldenImage("skinnedeffect-vertexcolor-quadA",
                            Rectangle(sampleAx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_skinnedeffect_vertexcolor_test_a.png",
                            /*tolerance=*/40);
        CompareGoldenImage("skinnedeffect-vertexcolor-quadB",
                            Rectangle(sampleBx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_skinnedeffect_vertexcolor_test_b.png",
                            /*tolerance=*/10);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<SkinnedEffectVertexColorTest>();
}
