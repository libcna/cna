// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: cross-renderer visual-parity proof -- reuses
// examples/easygl_skinnedeffect_vertexcolor_test.cpp's own SkinnedEffect.VertexColorEnabled scene
// verbatim: quad A (VertexColorEnabled=false, per-vertex black must be ignored, same red-dominant
// lit/textured result as the plain SkinnedEffect golden test's identity-bone quad) vs quad B
// (VertexColorEnabled=true, per-vertex black must zero the lit/textured result to pure black,
// regardless of the lighting math -- an unambiguous, lighting-independent check).

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
    // Stride-56 GPU-compact skinned+Color vertex: Position(0)+Normal(12)+TexCoord(24)+
    // BoneWeight(32)+BoneIndices(48)+Color(52) -- matches OpenGL2's own BindVertexAttributesForStride
    // stride==56 case exactly (mirrors EasyGLRenderer's identical layout).
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

class OpenGL2SkinnedEffectVertexColorGoldenTest : public CNA::Examples::PixelTestGame
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
        AppendQuad(verts, -1.0f, -0.5f, 0, 0, 0, 255); // quad A (left): VertexColorEnabled=false
        AppendQuad(verts, 0.5f, 1.0f, 0, 0, 0, 255);   // quad B (right): VertexColorEnabled=true

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

        ExpectPixel("quadA-vertexcolor-disabled", Rectangle(sampleAx, sampleY, 1, 1),
                    Color(174, 0, 0, 255), /*tolerance=*/40);
        ExpectPixel("quadB-vertexcolor-enabled-black", Rectangle(sampleBx, sampleY, 1, 1),
                    Color(0, 0, 0, 255), /*tolerance=*/10);

        CompareGoldenImage("skinnedeffect-vertexcolor-quadA-vs-easygl",
                            Rectangle(sampleAx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_skinnedeffect_vertexcolor_test_a.png",
                            /*tolerance=*/40);
        CompareGoldenImage("skinnedeffect-vertexcolor-quadB-vs-easygl",
                            Rectangle(sampleBx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_skinnedeffect_vertexcolor_test_b.png",
                            /*tolerance=*/10);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL2SkinnedEffectVertexColorGoldenTest>();
}
