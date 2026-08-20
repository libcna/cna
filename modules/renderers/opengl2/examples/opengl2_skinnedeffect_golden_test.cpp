// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: cross-renderer visual-parity proof #4 -- reuses
// examples/easygl_skinnedeffect_golden_test.cpp's own SkinnedEffect scene verbatim ("quad A":
// w0=1, i0=0 -> Bones[0]=Identity -> stays at authored NDC x: -1.0..-0.5, EnableDefaultLighting())
// and its own golden PNG (examples/golden/easygl_skinnedeffect_golden_test.png).
//
// EnableDefaultLighting() applies real Phong lighting math that isn't practical to derive
// analytically by hand (same reasoning as the EasyGL test's own header comment) -- the
// ExpectPixel cross-check below reuses that test's own live-observed, red-dominant expected value
// and tolerance=40.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"

#include <array>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    void AppendQuad(std::vector<VertexPositionNormalTextureSkinned>& out,
                     const Vector4& weight, const std::array<std::uint8_t, 4>& indices)
    {
        const float xMin = -1.0f, xMax = -0.5f;
        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTextureSkinned tl(Vector3(xMin,  1, 0), n, Vector2(0, 0), weight, indices);
        const VertexPositionNormalTextureSkinned bl(Vector3(xMin, -1, 0), n, Vector2(0, 1), weight, indices);
        const VertexPositionNormalTextureSkinned br(Vector3(xMax, -1, 0), n, Vector2(1, 1), weight, indices);
        const VertexPositionNormalTextureSkinned tr(Vector3(xMax,  1, 0), n, Vector2(1, 0), weight, indices);
        out.push_back(tl); out.push_back(bl); out.push_back(br);
        out.push_back(tl); out.push_back(br); out.push_back(tr);
    }
}

class OpenGL2SkinnedEffectGoldenTest : public CNA::Examples::PixelTestGame
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
        fx.Apply();

        std::vector<VertexPositionNormalTextureSkinned> verts;
        AppendQuad(verts, Vector4(1.0f, 0.0f, 0.0f, 0.0f), {0, 0, 0, 0}); // quad A: identity bone

        VertexBuffer vb(device, static_cast<int>(verts.size()));
        vb.SetData(verts.data(), 0, static_cast<int>(verts.size()));
        device.SetVertexBuffer(&vb);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, static_cast<int>(verts.size() / 3));

        // Same pixel column easygl_skinnedeffect_golden_test.cpp uses for quad A (W/8, NDC x ~ -0.75).
        const int samplePx = W / 8;
        const int sampleY = H / 2;
        ExpectPixel("quadA-identity-vs-observed", Rectangle(samplePx, sampleY, 1, 1),
                    Color(174, 0, 0, 255), /*tolerance=*/40);
        CompareGoldenImage("skinnedeffect-quadA-identity-vs-easygl",
                            Rectangle(samplePx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_skinnedeffect_golden_test.png",
                            /*tolerance=*/40);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL2SkinnedEffectGoldenTest>();
}
