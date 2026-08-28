// SPDX-License-Identifier: MS-PL
// WEBGPU-83 (Phase 3): validate DepthStencilState.TwoSidedStencilMode on the WebGPU renderer --
// i.e. that a back-facing triangle really picks up the CounterClockwise* stencil ops instead of the
// front-face ones once TwoSidedStencilMode is on. This is the differential test the WEBGPU-83 plan
// row flagged as the last open item: Phase 1/2 baked the two-sided front/back mapping into
// WGPUStencilFaceState (front = the primary/CW ops, back = the CCW ops) but never pixel-verified the
// winding, which is exactly the silent-wrongness class (a Y-flip or a swapped front/back slot would
// send the CCW ops to the wrong face and go unnoticed).
//
// Method and expected values are the cross-renderer parity contract from the EasyGL reference
// (easygl_depthstencilstate_stencil_twosided_test.cpp, Task 318). Two columns share this setup on a
// BACK-facing triangle (reversed winding vs the front-facing quads):
//   1. Stamp (front-facing quad): StencilFunction=Always/StencilPass=Replace/ref=0x05 -> buffer 0x05
//      (Always is face-agnostic, so front and back are set identically here).
//   2. Op (back-facing triangle), ref=0x05 shared (XNA has one ReferenceStencil):
//        Front-face: Equal (0x05==0x05 -> PASS), StencilPass=Decrement.
//        Back-face (CCW): NotEqual (0x05!=0x05 -> FAIL), CounterClockwiseStencilFail=Increment.
//      Column 0 (TwoSidedStencilMode=true): the CCW ops apply to this back-facing triangle ->
//        NotEqual FAILS -> Increment -> buffer 0x06.
//      Column 1 (TwoSidedStencilMode=false, control): the CCW ops are ignored; the front ops apply
//        to every face incl. this back one -> Equal PASSES -> Decrement -> buffer 0x04.
//   3. Read-back (front-facing quad, green): Equal ref=0x06.
//        Column 0 expects GREEN (buffer is genuinely 0x06).
//        Column 1 expects BACKGROUND (buffer is 0x04, not 0x06).
// The two columns toggle ONLY TwoSidedStencilMode and expect OPPOSITE outcomes, so a bypassed
// stencil test or a front/back mix-up cannot pass both (see Task 318's "same outcome both columns
// cannot distinguish feature-works from feature-bypassed" lesson).
//
// Exit code 0 = both checks PASS, 1 = either FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kBackground(20, 20, 20, 255);
    const Color kGreen(0, 255, 0, 255);

    // Front-facing winding (CCW in NDC -- the convention this project's tests use for "front").
    void DrawQuadFront(GraphicsDevice& dev, float x0, float x1, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(x0,  1.0f, 0.5f), color },
            { Vector3(x0, -1.0f, 0.5f), color },
            { Vector3(x1, -1.0f, 0.5f), color },
            { Vector3(x0,  1.0f, 0.5f), color },
            { Vector3(x1, -1.0f, 0.5f), color },
            { Vector3(x1,  1.0f, 0.5f), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    // Reversed winding -> back-facing (needs CullMode::None to rasterize).
    void DrawQuadBack(GraphicsDevice& dev, float x0, float x1, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(x1, -1.0f, 0.5f), color },
            { Vector3(x0, -1.0f, 0.5f), color },
            { Vector3(x0,  1.0f, 0.5f), color },
            { Vector3(x1,  1.0f, 0.5f), color },
            { Vector3(x1, -1.0f, 0.5f), color },
            { Vector3(x0,  1.0f, 0.5f), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    DepthStencilState MakeStampState()
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Always);
        ds.setCounterClockwiseStencilFunctionProperty(CompareFunction::Always);
        ds.setStencilPassProperty(StencilOperation::Replace);
        ds.setCounterClockwiseStencilPassProperty(StencilOperation::Replace);
        ds.setReferenceStencilProperty(0x05);
        return ds;
    }

    DepthStencilState MakeOpState(bool twoSided)
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setTwoSidedStencilModeProperty(twoSided);
        ds.setReferenceStencilProperty(0x05);

        // Front-face: Equal (0x05==0x05 -> PASS), then Decrement.
        ds.setStencilFunctionProperty(CompareFunction::Equal);
        ds.setStencilPassProperty(StencilOperation::Decrement);
        ds.setStencilFailProperty(StencilOperation::Keep);
        ds.setStencilDepthBufferFailProperty(StencilOperation::Keep);

        // Back-face (CCW): NotEqual (0x05!=0x05 -> FAIL), then Increment on fail.
        ds.setCounterClockwiseStencilFunctionProperty(CompareFunction::NotEqual);
        ds.setCounterClockwiseStencilFailProperty(StencilOperation::Increment);
        ds.setCounterClockwiseStencilPassProperty(StencilOperation::Keep);
        ds.setCounterClockwiseStencilDepthBufferFailProperty(StencilOperation::Keep);
        return ds;
    }

    DepthStencilState MakeReadBackState()
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Equal);
        ds.setReferenceStencilProperty(0x06);
        ds.setStencilPassProperty(StencilOperation::Keep);
        ds.setStencilFailProperty(StencilOperation::Keep);
        return ds;
    }
}

class WebGpuStencilTwoSidedTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

    static bool IsGreen(const Color& c)
    {
        return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kBackground, 1.0f, 0);
        dev.setBlendStateProperty(BlendState::Opaque);

        RasterizerState rsNoCull;
        rsNoCull.setCullModeProperty(CullMode::None);
        dev.setRasterizerStateProperty(rsNoCull);

        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        const float colW = 2.0f / 2.0f;
        auto colX = [&](int i) { return -1.0f + colW * static_cast<float>(i); };

        // Column 0: TwoSidedStencilMode=true -- CCW ops apply to the back-facing triangle.
        {
            const float x0 = colX(0), x1 = x0 + colW;
            dev.setDepthStencilStateProperty(MakeStampState());
            DrawQuadFront(dev, x0, x1, kBackground);
            dev.setDepthStencilStateProperty(MakeOpState(/*twoSided=*/true));
            DrawQuadBack(dev, x0, x1, kBackground);
        }

        // Column 1: TwoSidedStencilMode=false (control) -- front ops apply to every face.
        {
            const float x0 = colX(1), x1 = x0 + colW;
            dev.setDepthStencilStateProperty(MakeStampState());
            DrawQuadFront(dev, x0, x1, kBackground);
            dev.setDepthStencilStateProperty(MakeOpState(/*twoSided=*/false));
            DrawQuadBack(dev, x0, x1, kBackground);
        }

        for (int i = 0; i < 2; ++i)
        {
            const float x0 = colX(i), x1 = x0 + colW;
            dev.setDepthStencilStateProperty(MakeReadBackState());
            DrawQuadFront(dev, x0, x1, kGreen);
        }

        // Fixed pixel centres of the two 32px-wide columns of the 64x64 backbuffer. (Deliberately
        // NOT derived from the Viewport: on this renderer the viewport can report physical/HiDPI
        // pixels, which would push a scaled read rectangle out of the 64x64 backbuffer bounds.)
        const int kColX[2] = { 16, 48 };
        Color results[2] = { Color(0, 0, 0, 0), Color(0, 0, 0, 0) };
        for (int i = 0; i < 2; ++i)
        {
            Rectangle reg(kColX[i], 32, 1, 1);
            dev.GetBackBufferData(&reg, &results[i], 0, 1);
        }

        const char* names[2] = {
            "TwoSidedStencilMode=true (CCW ops apply to back face, expect PASS)",
            "TwoSidedStencilMode=false (front ops apply to all faces, must reject)",
        };
        const bool expectGreen[2] = { true, false };

        int passCount = 0;
        for (int i = 0; i < 2; ++i)
        {
            const Color& c = results[i];
            const bool ok = expectGreen[i] ? IsGreen(c) : !IsGreen(c);
            std::printf("[%s] %s: centre=(%d,%d,%d), expected %s\n",
                        ok ? "PASS" : "FAIL", names[i],
                        c.getRProperty(), c.getGProperty(), c.getBProperty(),
                        expectGreen[i] ? "GREEN" : "BACKGROUND");
            if (ok) ++passCount;
        }

        std::printf("=== %d/2 PASS ===\n", passCount);
        result_ = (passCount == 2) ? 0 : 1;
        Exit();
    }

public:
    WebGpuStencilTwoSidedTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuStencilTwoSidedTest game;
    game.Run();
    return game.getResult();
}
