// SPDX-License-Identifier: MS-PL
// Task 467: golden-image consumer reusing Phase 36's already-verified BlendState::Additive
// scene (Task 306, examples/easygl_blendstate_additive_test.cpp) via
// PixelTestGame::CompareGoldenImage() (Task 463) instead of Task 306's own two range checks.
//
// Same scene as Task 306 exactly: clear to Color(200,50,0,255), draw a full-screen opaque quad
// Color(255,100,0,255) under BlendState::Additive. Expected (Additive: colorDst=One, no
// InverseSourceAlpha): R = 255+200 = 455 saturates/clamps to 255 (unambiguous, no rounding);
// G = 100+50 = 150 exactly (the distinguishing check that catches Task 868's Vulkan bug, where a
// hardcoded InverseSourceAlpha destination factor would instead drop the destination entirely,
// giving G~100). Reuses Task 306's own tolerance=10 (its own [140,160] range around 150) against
// the literal (255,150,0).
//
// A full-screen quad means any crop is safe -- unlike Tasks 465/466's small marker/blend
// regions, there's no risk of straddling an edge.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class BlendStateAdditiveGoldenTest : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(200, 50, 0, 255)); // background to be added-into, not replaced
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

        // Task 896 finding -- same as Task 306's own comment.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        // Cross-check against Task 306's own literal expected value, independent of the golden
        // PNG's own contents (same rationale as Tasks 464-466).
        ExpectPixel("additive-vs-task306-expected", Rectangle(W / 2, H / 2, 1, 1),
                    Color(255, 150, 0, 255), /*tolerance=*/10);
        CompareGoldenImage("blendstate-additive",
                            Rectangle(W / 2 - 4, H / 2 - 4, 8, 8),
                            "examples/golden/easygl_blendstate_additive_golden_test.png",
                            /*tolerance=*/10);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<BlendStateAdditiveGoldenTest>();
}
