// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: cross-renderer visual-parity proof -- reuses
// examples/easygl_rasterizerstate_cullmode_golden_test.cpp's own RasterizerState.CullMode scene
// verbatim: two quads with opposite winding order (column 0 = CW, column 1 = CCW) drawn under
// RasterizerState{CullMode=None} -- both windings must render since culling is disabled. Samples
// column 0 (the CW quad) at NDC x=-0.5: expect RED.

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

class OpenGL2RasterizerStateCullModeGoldenTest : public CNA::Examples::PixelTestGame
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
        ExpectPixel("cullnone-column0-vs-expected", Rectangle(samplePx, sampleY, 1, 1),
                    kRed, /*tolerance=*/30);
        CompareGoldenImage("rasterizerstate-cullnone-column0-vs-easygl",
                            Rectangle(samplePx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_rasterizerstate_cullmode_golden_test.png",
                            /*tolerance=*/30);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL2RasterizerStateCullModeGoldenTest>();
}
