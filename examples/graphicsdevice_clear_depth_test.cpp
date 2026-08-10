// SPDX-License-Identifier: MS-PL
// Task 950: verify GraphicsDevice::Clear()'s `depth` parameter actually takes effect on Vulkan
// and Bgfx, instead of every clear silently hardcoding depth=1.0 regardless of what was requested
// (a real, separate, pre-existing bug found while implementing Task 871's stencil-clearing fix).
//
// Method: clear to a deliberately non-default depth (0.3), then draw a single quad at z=0.6 with
// DepthBufferFunction::Less and DepthBufferWriteEnable=false (so the draw itself can't perturb the
// buffer). If the clear correctly wrote depth=0.3, the new fragment's z=0.6 fails a Less test
// against it (0.6 is NOT less than 0.3) — REJECTED, pixel stays background. If the bug is present
// (depth silently hardcoded to 1.0 regardless of what was cleared), 0.6 IS less than 1.0 — the
// fragment WRONGLY passes and overwrites the pixel with the test colour. This is a single-frame
// Clear()-then-Draw() sequence (not a same-frame stamp/clear/compare race) — Clear() legitimately
// happens once, before the only draw, matching every renderer's per-frame clear semantics exactly,
// so no frame-split is needed (unlike the stamp-based Task 871 stencil test).
//
// Check A -- ClearOptions::Target|DepthBuffer (exercises IGraphicsRenderer::ClearColorAndDepth):
//   clear depth to 0.3, draw at z=0.6.
// Check B -- ClearOptions::Target|DepthBuffer|Stencil (exercises
//   IGraphicsRenderer::ClearColorDepthAndStencil): clear depth to 0.7 (a different value than Check
//   A, so a broken dispatch that leaves depth at some other single hardcoded constant can't
//   coincidentally still pass both checks), draw at z=0.9 (still less than the buggy hardcoded 1.0,
//   still NOT less than the correctly-cleared 0.7).
//
// ClearOptions::DepthBuffer alone (IGraphicsRenderer::ClearDepth) was deliberately NOT exercised
// here, because Bgfx's ClearDepth() then only stored the new clear value without an immediate
// EnsureViewState()/touch().
//
// REMED-GFX-129 false-positive note: that exemption also meant nothing in this file ever reached
// Vulkan's ClearDepth, which recorded no clear request at all -- so a depth-ONLY Clear() did
// literally nothing there and both checks below still passed, since both include
// ClearOptions::Target. Bgfx's own gap was closed by REMED-GFX-018 and Vulkan's by REMED-GFX-129;
// `examples/graphicsdevice_ordered_clear_test.cpp` (checks D1, D5, D6 and D7) now covers a
// depth-only clear, its value at both ends of the legal range, and its ordering against draws, on
// every renderer that has a depth oracle.
//
// Exit code 0 = both checks PASS, 1 = either FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

namespace
{
    const Color kBackground(20, 20, 20, 255);
    const Color kTestColor(0, 255, 0, 255);

    void DrawQuad(GraphicsDevice& dev, float z, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, z), color },
            { Vector3(-1.0f, -1.0f, z), color },
            { Vector3( 1.0f, -1.0f, z), color },
            { Vector3(-1.0f,  1.0f, z), color },
            { Vector3( 1.0f, -1.0f, z), color },
            { Vector3( 1.0f,  1.0f, z), color },
        };
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real
        // default RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    void ApplyBasicEffect(GraphicsDevice& dev)
    {
        static BasicEffect* fx = nullptr;
        if (fx == nullptr) fx = new BasicEffect(dev);
        fx->setWorldProperty(Matrix::getIdentityProperty());
        fx->setViewProperty(Matrix::getIdentityProperty());
        fx->setProjectionProperty(Matrix::getIdentityProperty());
        fx->VertexColorEnabled = true;
        fx->Apply();
    }

    // Returns true if the test-colour quad was correctly REJECTED (pixel stays background) --
    // i.e. the clear's requested depth actually took effect.
    bool ClearThenExpectRejected(GraphicsDevice& dev, ClearOptions clearOptions,
                                  float clearDepthValue, float drawZ)
    {
        dev.Clear(clearOptions, kBackground, clearDepthValue, 0);
        dev.setBlendStateProperty(BlendState::Opaque);
        ApplyBasicEffect(dev);

        DepthStencilState test;
        test.setDepthBufferEnableProperty(true);
        test.setDepthBufferWriteEnableProperty(false);
        test.setDepthBufferFunctionProperty(CompareFunction::Less);
        dev.setDepthStencilStateProperty(test);
        DrawQuad(dev, drawZ, kTestColor);

        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color c(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &c, 0, 1);
        // Background (20,20,20) expected if rejected; test colour (0,255,0) if the bug let it through.
        return c.getGProperty() <= 60 && c.getRProperty() <= 60 && c.getBProperty() <= 60;
    }
}

class GraphicsDeviceClearDepthTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  step_      = 0;
    int  passCount_ = 0;
    bool done_      = false;
    int  result_    = 1;

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;

        auto& dev = getGraphicsDeviceProperty();

        switch (step_)
        {
        case 0:
        {
            // Check A -- Target|DepthBuffer (ClearColorAndDepth): clear depth to 0.3, draw at 0.6.
            const bool okA = ClearThenExpectRejected(
                dev, ClearOptions::Target | ClearOptions::DepthBuffer, 0.3f, 0.6f);
            std::printf("[%s] ClearOptions::Target|DepthBuffer (clear depth=0.3, draw z=0.6, "
                        "Less test must reject)\n", okA ? "PASS" : "FAIL");
            if (okA) ++passCount_;
            break;
        }
        case 1:
        {
            // Check B -- Target|DepthBuffer|Stencil (ClearColorDepthAndStencil): clear depth to
            // 0.7 (different value than Check A), draw at 0.9.
            const bool okB = ClearThenExpectRejected(
                dev, ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                0.7f, 0.9f);
            std::printf("[%s] ClearOptions::Target|DepthBuffer|Stencil (clear depth=0.7, draw "
                        "z=0.9, Less test must reject)\n", okB ? "PASS" : "FAIL");
            if (okB) ++passCount_;

            std::printf("=== %d/2 PASS ===\n", passCount_);
            result_ = (passCount_ == 2) ? 0 : 1;
            done_ = true;
            Exit();
            break;
        }
        default:
            break;
        }
        ++step_;
    }

public:
    GraphicsDeviceClearDepthTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }

    int getResult() const { return result_; }
};

int main()
{
    GraphicsDeviceClearDepthTest game;
    game.Run();
    return game.getResult();
}
