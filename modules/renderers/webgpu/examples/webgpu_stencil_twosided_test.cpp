// SPDX-License-Identifier: MS-PL
// WEBGPU-83 (Phase 3), corrected by TQ-012: validate DepthStencilState.TwoSidedStencilMode on the
// WebGPU renderer -- the CounterClockwise* stencil state must apply to COUNTER-CLOCKWISE triangles
// and the ordinary state to CLOCKWISE ones, as XNA's DepthStencilState::Apply does by writing the
// former to D3DRS_CCW_STENCIL* (and as the shared TwoSidedStencilTest pins on Software/EasyGL).
//
// History: this program used to draw its operation on a CLOCKWISE quad and expect the CCW ops to
// apply to it, which encoded the renderer's then-swapped stencilFront/stencilBack slots instead of
// XNA's rule. The geometry and expectations now match the EasyGL reference
// (easygl_depthstencilstate_stencil_twosided_test.cpp, Task 318), plus a third column that the old
// two could not tell apart from "the CCW ops apply to every face".
//
// Three columns share this setup:
//   1. Stamp: StencilFunction=Always/StencilPass=Replace/ref=0x05 -> buffer 0x05 (Always is
//      winding-agnostic, so both windings are set identically here).
//   2. Operation, ref=0x05 shared (XNA has one ReferenceStencil):
//        Ordinary (clockwise):  Equal (0x05==0x05 -> PASS), StencilPass=Decrement -> 0x04.
//        Counter-clockwise:     NotEqual (0x05!=0x05 -> FAIL), CounterClockwiseStencilFail=Increment
//                               -> 0x06.
//      Column 0: TwoSidedStencilMode=true, COUNTER-CLOCKWISE quad -> CCW ops -> 0x06.
//      Column 1: TwoSidedStencilMode=false, COUNTER-CLOCKWISE quad -> ordinary ops -> 0x04.
//      Column 2: TwoSidedStencilMode=true, CLOCKWISE quad -> ordinary ops -> 0x04.
//   3. Read-back (green quad): Equal ref=0x06. Column 0 expects GREEN; columns 1 and 2 BACKGROUND.
// Column 1 toggles only the mode against column 0, and column 2 only the winding, so a bypassed
// stencil test, an ignored mode and a front/back swap each fail a different column.
//
// Exit code 0 = all three checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
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

    // Counter-clockwise as displayed: top-left, bottom-left, bottom-right (NDC y points up).
    void DrawQuadCounterClockwise(GraphicsDevice& dev, float x0, float x1, const Color& color)
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

    // Clockwise as displayed -- XNA's front face (kept by the default CullCounterClockwiseFace).
    void DrawQuadClockwise(GraphicsDevice& dev, float x0, float x1, const Color& color)
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

        // Ordinary (clockwise): Equal (0x05==0x05 -> PASS), then Decrement.
        ds.setStencilFunctionProperty(CompareFunction::Equal);
        ds.setStencilPassProperty(StencilOperation::Decrement);
        ds.setStencilFailProperty(StencilOperation::Keep);
        ds.setStencilDepthBufferFailProperty(StencilOperation::Keep);

        // Counter-clockwise: NotEqual (0x05!=0x05 -> FAIL), then Increment on fail.
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

        constexpr int kColumns = 3;
        const float colW = 2.0f / static_cast<float>(kColumns);
        auto colX = [&](int i) { return -1.0f + colW * static_cast<float>(i); };
        using DrawQuad = void (*)(GraphicsDevice&, float, float, const Color&);
        const bool twoSided[kColumns] = { true, false, true };
        const DrawQuad operation[kColumns] = {
            DrawQuadCounterClockwise, DrawQuadCounterClockwise, DrawQuadClockwise };

        for (int i = 0; i < kColumns; ++i)
        {
            const float x0 = colX(i), x1 = x0 + colW;
            dev.setDepthStencilStateProperty(MakeStampState());
            DrawQuadCounterClockwise(dev, x0, x1, kBackground);
            dev.setDepthStencilStateProperty(MakeOpState(twoSided[i]));
            operation[i](dev, x0, x1, kBackground);
        }

        for (int i = 0; i < kColumns; ++i)
        {
            const float x0 = colX(i), x1 = x0 + colW;
            dev.setDepthStencilStateProperty(MakeReadBackState());
            DrawQuadCounterClockwise(dev, x0, x1, kGreen);
        }

        // Fixed pixel centres of the three ~21px-wide columns of the 64x64 backbuffer (column i
        // spans [64i/3, 64(i+1)/3)). Deliberately NOT derived from the Viewport: on this renderer
        // the viewport can report physical/HiDPI pixels, which would push a scaled read rectangle
        // out of the 64x64 backbuffer bounds.
        const int kColX[kColumns] = { 10, 32, 53 };
        Color results[kColumns] = { Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0) };
        for (int i = 0; i < kColumns; ++i)
        {
            Rectangle reg(kColX[i], 32, 1, 1);
            dev.GetBackBufferData(&reg, &results[i], 0, 1);
        }

        const char* names[kColumns] = {
            "TwoSidedStencilMode=true, counter-clockwise quad (CCW ops apply, expect PASS)",
            "TwoSidedStencilMode=false, counter-clockwise quad (ordinary ops apply, must reject)",
            "TwoSidedStencilMode=true, clockwise quad (ordinary ops apply, must reject)",
        };
        const bool expectGreen[kColumns] = { true, false, false };

        int passCount = 0;
        for (int i = 0; i < kColumns; ++i)
        {
            const Color& c = results[i];
            const bool ok = expectGreen[i] ? IsGreen(c) : !IsGreen(c);
            std::printf("[%s] %s: centre=(%d,%d,%d), expected %s\n",
                        ok ? "PASS" : "FAIL", names[i],
                        c.getRProperty(), c.getGProperty(), c.getBProperty(),
                        expectGreen[i] ? "GREEN" : "BACKGROUND");
            if (ok) ++passCount;
        }

        std::printf("=== %d/%d PASS ===\n", passCount, kColumns);
        result_ = (passCount == kColumns) ? 0 : 1;
        Exit();
    }

public:
    WebGpuStencilTwoSidedTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // GPU test isolation GTI-0003: GetBackBufferData is HiDef-only (SOFTWARE-213
        // enforces XNA's rule), so under the default Reach profile this test aborted before its
        // first check -- the defect VKPAR-0006 fixed in 104 Vulkan
        // tests.
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
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
