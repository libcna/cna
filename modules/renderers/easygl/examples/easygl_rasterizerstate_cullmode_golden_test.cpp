// SPDX-License-Identifier: MS-PL
// Task 468: golden-image consumer reusing Phase 38's already-verified RasterizerState.CullMode
// scene (Task 323, examples/easygl_rasterizerstate_cullmode_test.cpp) via
// PixelTestGame::CompareGoldenImage() (Task 463) instead of Task 323's own 6 range checks.
//
// Recreates only Task 323's CullMode::None / "column 0 (CW)" check: two quads with opposite
// winding order (column 0 = CW, signed area < 0 in NDC; column 1 = CCW, signed area > 0) drawn
// under RasterizerState{CullMode=None} -- both windings must render since culling is disabled.
// Samples column 0 (the CW quad) at NDC x=-0.5: expect RED. See Task 323's own file for the full
// 6-check cull-mode-contrast rationale (CullNone/CullCounterClockwiseFace/CullClockwiseFace).
//
// Reuses Task 323's own implicit tolerance (its `IsRed` check: R>=200, G/B<=30 against literal
// (255,0,0) -- equivalent to roughly tolerance=30).

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);

    // Signed area < 0 in NDC -- empirically confirmed to survive XNA's default
    // CullCounterClockwiseFace state (see Task 323's own file header note).
    void DrawQuadCW(GraphicsDevice& dev, float x0, float x1, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(x1, -1.0f, 0.0f), color },
            { Vector3(x0, -1.0f, 0.0f), color },
            { Vector3(x0,  1.0f, 0.0f), color },
            { Vector3(x1,  1.0f, 0.0f), color },
            { Vector3(x1, -1.0f, 0.0f), color },
            { Vector3(x0,  1.0f, 0.0f), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    // Reversed winding (signed area > 0 in NDC) -- culled by XNA's default
    // CullCounterClockwiseFace state.
    void DrawQuadCCW(GraphicsDevice& dev, float x0, float x1, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(x0,  1.0f, 0.0f), color },
            { Vector3(x0, -1.0f, 0.0f), color },
            { Vector3(x1, -1.0f, 0.0f), color },
            { Vector3(x0,  1.0f, 0.0f), color },
            { Vector3(x1, -1.0f, 0.0f), color },
            { Vector3(x1,  1.0f, 0.0f), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }
}

class RasterizerStateCullModeGoldenTest : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int width = vp.getWidthProperty();
        const int height = vp.getHeightProperty();

        device.Clear(Color(0, 0, 0, 255));
        device.setBlendStateProperty(BlendState::Opaque);
        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);

        BasicEffect fx(device);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        DrawQuadCW(device, -1.0f, 0.0f, kRed);     // column 0, left half
        DrawQuadCCW(device, 0.0f, 1.0f, kGreen);   // column 1, right half

        const int samplePx = static_cast<int>((-0.5f + 1.0f) * 0.5f * static_cast<float>(width));
        const int sampleY = height / 2;
        // Cross-check against Task 323's own literal expected value, independent of the golden
        // PNG's own contents (same rationale as Tasks 464-467).
        ExpectPixel("cullnone-column0-vs-task323-expected", Rectangle(samplePx, sampleY, 1, 1),
                    kRed, /*tolerance=*/30);
        CompareGoldenImage("rasterizerstate-cullnone-column0",
                            Rectangle(samplePx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_rasterizerstate_cullmode_golden_test.png",
                            /*tolerance=*/30);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<RasterizerStateCullModeGoldenTest>();
}
