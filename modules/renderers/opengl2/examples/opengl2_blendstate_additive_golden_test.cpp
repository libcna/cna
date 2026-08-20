// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: cross-renderer visual-parity proof -- reuses
// examples/easygl_blendstate_additive_golden_test.cpp's own BlendState::Additive scene verbatim
// (clear to Color(200,50,0,255), draw a full-screen opaque quad Color(255,100,0,255) under
// BlendState::Additive) and its own golden PNG.
//
// Expected (Additive: colorDst=One, no InverseSourceAlpha): R = 255+200 = 455 saturates to 255
// (unambiguous); G = 100+50 = 150 exactly (distinguishes real additive blending from a broken
// InverseSourceAlpha destination factor, which would instead drop the destination entirely).

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class OpenGL2BlendStateAdditiveGoldenTest : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(200, 50, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Additive);

        const Color kSource(255, 100, 0, 255);
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), kSource },
            { Vector3(-1.0f, -1.0f, 0.0f), kSource },
            { Vector3( 1.0f, -1.0f, 0.0f), kSource },
            { Vector3(-1.0f,  1.0f, 0.0f), kSource },
            { Vector3( 1.0f, -1.0f, 0.0f), kSource },
            { Vector3( 1.0f,  1.0f, 0.0f), kSource },
        };

        BasicEffect fx(device);
        fx.VertexColorEnabled = true;
        fx.Apply();

        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        ExpectPixel("additive-vs-expected", Rectangle(W / 2, H / 2, 1, 1),
                    Color(255, 150, 0, 255), /*tolerance=*/10);
        CompareGoldenImage("blendstate-additive-vs-easygl",
                            Rectangle(W / 2 - 4, H / 2 - 4, 8, 8),
                            "examples/golden/easygl_blendstate_additive_golden_test.png",
                            /*tolerance=*/10);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL2BlendStateAdditiveGoldenTest>();
}
