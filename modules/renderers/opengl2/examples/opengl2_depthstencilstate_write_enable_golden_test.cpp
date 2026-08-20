// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: cross-renderer visual-parity proof -- reuses
// examples/easygl_depthstencilstate_write_enable_golden_test.cpp's own DepthStencilState
// write-enable scene verbatim: draw far quad A (Red, z=0.8, DepthStencilState::Default), then
// near quad B (Green, z=0.2, DepthBufferWriteEnable=false), then quad C (Blue, z=0.5, Default
// again) over the same screen half. C's depth compare (0.5 < 0.8, A's still-present depth since
// B's write was correctly skipped) passes, so C ends up visible: expect BLUE.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    void DrawQuad(GraphicsDevice& dev, float x0, float x1, float z, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(x0,  1.0f, z), color },
            { Vector3(x0, -1.0f, z), color },
            { Vector3(x1, -1.0f, z), color },
            { Vector3(x0,  1.0f, z), color },
            { Vector3(x1, -1.0f, z), color },
            { Vector3(x1,  1.0f, z), color },
        };
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }
}

class OpenGL2DepthStencilStateWriteEnableGoldenTest : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color(0, 0, 0, 255), 1.0f, 0);
        device.setBlendStateProperty(BlendState::Opaque);

        BasicEffect fx(device);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        DepthStencilState writeDisabled;
        writeDisabled.setDepthBufferEnableProperty(true);
        writeDisabled.setDepthBufferWriteEnableProperty(false);
        writeDisabled.setDepthBufferFunctionProperty(CompareFunction::LessEqual);

        const Color kRed(255, 0, 0, 255);
        const Color kGreen(0, 255, 0, 255);
        const Color kBlue(0, 0, 255, 255);

        device.setDepthStencilStateProperty(DepthStencilState::Default);
        DrawQuad(device, -1.0f, 0.0f, 0.8f, kRed);
        device.setDepthStencilStateProperty(writeDisabled);
        DrawQuad(device, -1.0f, 0.0f, 0.2f, kGreen);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        DrawQuad(device, -1.0f, 0.0f, 0.5f, kBlue);

        const int samplePx = W / 4;
        const int sampleY = H / 2;
        ExpectPixel("depthwrite-disabled-vs-expected", Rectangle(samplePx, sampleY, 1, 1),
                    kBlue, /*tolerance=*/60);
        CompareGoldenImage("depthstencilstate-write-disabled-vs-easygl",
                            Rectangle(samplePx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_depthstencilstate_write_enable_golden_test.png",
                            /*tolerance=*/60);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL2DepthStencilStateWriteEnableGoldenTest>();
}
