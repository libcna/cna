// SPDX-License-Identifier: MS-PL
// Task 315: verify DepthStencilState.StencilEnable actually gates the stencil test.
//
// IMPORTANT: PresentationParameters.DepthStencilFormat defaults to DepthFormat::Depth24 (no
// stencil aspect at all) -- with no real stencil buffer, GL/Vulkan/etc. treat the stencil test as
// trivially always-passing regardless of DepthStencilState.StencilEnable, which silently makes any
// stencil pixel test meaningless. This test's constructor explicitly requests
// DepthFormat::Depth24Stencil8 via GraphicsDeviceManager -- do not remove this, and any future
// stencil pixel test in this codebase needs the same setup.
//
// Method (two-column differential test): each column is split into a "stamped" left half and an
// "unstamped" right half.
//   1. A full-screen "zero stamp" draw first writes stencil=0 everywhere (StencilFunction=Always,
//      StencilPass=Replace, ReferenceStencil=0), in the background colour -- this establishes a
//      known baseline explicitly via a real draw, NOT via GraphicsDevice::Clear(), because
//      GraphicsDevice::Clear ignores ClearOptions::Stencil entirely on every renderer (confirmed
//      while writing this test, tracked as a new Task 871 -- see plans/plan_graphics.md) and this
//      project's Vulkan render passes use stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE, so the
//      stencil aspect's initial content is genuinely undefined there. Depth testing is disabled
//      for every draw in this test (DepthBufferEnable=false throughout) to isolate stencil
//      behaviour from the already-tracked depth-compare bug (Task 870).
//   2. Each column's LEFT half gets a second stamp: stencil=1 (same Always/Replace pattern),
//      still in the background colour -- so after steps 1-2, stencil=1 on the left half, 0 on the
//      right half, and the visible colour is still the background everywhere.
//   3. A GREEN "test" quad is drawn over the WHOLE column, using StencilFunction=Equal,
//      ReferenceStencil=1, and the property under test: StencilEnable=true for Check A,
//      StencilEnable=false for Check B.
//
// Check A (StencilEnable=true): the stencil compare actually gates the fragment -- left half
//   (stencil==1, matches) -> GREEN; right half (stencil==0, no match) -> stays BACKGROUND.
// Check B (StencilEnable=false): the stencil test is skipped entirely (FNA/XNA semantics: fragments
//   always pass when disabled) -- BOTH halves -> GREEN, regardless of the stencil buffer contents.
//
// NOTE: this project's Vulkan renderer (VulkanRenderer::ApplyDepthStencilState, tracked as
// Task 870) never sets VkPipelineDepthStencilStateCreateInfo::stencilTestEnable/front/back at all
// -- stencil testing is completely non-functional there regardless of StencilEnable. Expect Check
// A to fail outright on Vulkan (both halves show GREEN, since no gating ever happens) while Check
// B passes coincidentally (its expected result already matches "stencil test never gates
// anything"). This is a reconfirmation of Task 870, not a new bug -- do not misread Check B's pass
// as evidence stencil testing works.
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

    void DrawQuad(GraphicsDevice& dev, float x0, float x1, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(x0,  1.0f, 0.5f), color },
            { Vector3(x0, -1.0f, 0.5f), color },
            { Vector3(x1, -1.0f, 0.5f), color },
            { Vector3(x0,  1.0f, 0.5f), color },
            { Vector3(x1, -1.0f, 0.5f), color },
            { Vector3(x1,  1.0f, 0.5f), color },
        };
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real
        // default RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    DepthStencilState MakeStampState(int referenceStencil)
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Always);
        ds.setStencilPassProperty(StencilOperation::Replace);
        ds.setReferenceStencilProperty(referenceStencil);
        return ds;
    }

    DepthStencilState MakeTestState(bool stencilEnable)
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setStencilEnableProperty(stencilEnable);
        ds.setStencilFunctionProperty(CompareFunction::Equal);
        ds.setReferenceStencilProperty(1);
        ds.setStencilPassProperty(StencilOperation::Keep);
        ds.setStencilFailProperty(StencilOperation::Keep);
        return ds;
    }
}

class DepthStencilStateStencilEnableTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

    static bool IsGreen(const Color& c)
    {
        return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60;
    }
    static bool IsBackground(const Color& c)
    {
        return c.getRProperty() <= 40 && c.getGProperty() <= 40 && c.getBProperty() <= 40;
    }

protected:
    void Initialize() override { Game::Initialize(); }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const auto& vp = dev.getViewportProperty();

        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kBackground, 1.0f, 0);
        dev.setBlendStateProperty(BlendState::Opaque);

        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        // Step 1: explicit full-screen zero-stamp (see file header for why Clear() can't be used).
        dev.setDepthStencilStateProperty(MakeStampState(0));
        DrawQuad(dev, -1.0f, 1.0f, kBackground);

        // Column A [-1, 0]: left half [-1,-0.5] stamped 1, right half [-0.5,0] left at 0.
        dev.setDepthStencilStateProperty(MakeStampState(1));
        DrawQuad(dev, -1.0f, -0.5f, kBackground);
        dev.setDepthStencilStateProperty(MakeTestState(/*stencilEnable=*/true));
        DrawQuad(dev, -1.0f, 0.0f, kGreen);

        // Column B [0, 1]: left half [0,0.5] stamped 1, right half [0.5,1] left at 0.
        dev.setDepthStencilStateProperty(MakeStampState(1));
        DrawQuad(dev, 0.0f, 0.5f, kBackground);
        dev.setDepthStencilStateProperty(MakeTestState(/*stencilEnable=*/false));
        DrawQuad(dev, 0.0f, 1.0f, kGreen);

        auto sample = [&](float cx) {
            const int px = static_cast<int>((cx + 1.0f) * 0.5f * vp.getWidthProperty());
            Rectangle reg(px, vp.getHeightProperty() / 2, 1, 1);
            Color c(0, 0, 0, 0);
            dev.GetBackBufferData(&reg, &c, 0, 1);
            return c;
        };

        const Color aLeft  = sample(-0.75f);
        const Color aRight = sample(-0.25f);
        const Color bLeft  = sample(0.25f);
        const Color bRight = sample(0.75f);

        const bool aOk = IsGreen(aLeft) && IsBackground(aRight);
        const bool bOk = IsGreen(bLeft) && IsGreen(bRight);

        std::printf("[%s] StencilEnable=true: left=(%d,%d,%d) expected GREEN, right=(%d,%d,%d) expected BACKGROUND\n",
                    aOk ? "PASS" : "FAIL",
                    aLeft.getRProperty(), aLeft.getGProperty(), aLeft.getBProperty(),
                    aRight.getRProperty(), aRight.getGProperty(), aRight.getBProperty());
        std::printf("[%s] StencilEnable=false: left=(%d,%d,%d) expected GREEN, right=(%d,%d,%d) expected GREEN\n",
                    bOk ? "PASS" : "FAIL",
                    bLeft.getRProperty(), bLeft.getGProperty(), bLeft.getBProperty(),
                    bRight.getRProperty(), bRight.getGProperty(), bRight.getBProperty());

        if (!aOk)
        {
            std::printf("[INFO] A mismatch here means the stencil test is not actually gating\n"
                        "       fragments when StencilEnable=true.\n");
        }

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    DepthStencilStateStencilEnableTest()
    {
        // The default PresentationParameters.DepthStencilFormat is DepthFormat::Depth24 (no
        // stencil aspect) -- a real stencil buffer must be explicitly requested or the stencil
        // test trivially always passes regardless of DepthStencilState.StencilEnable, which would
        // make this test unable to distinguish anything.
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }

    int getResult() const { return result_; }
};

int main()
{
    DepthStencilStateStencilEnableTest game;
    game.Run();
    return game.getResult();
}
