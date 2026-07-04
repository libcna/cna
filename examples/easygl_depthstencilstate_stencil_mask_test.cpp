// SPDX-License-Identifier: MS-PL
// Task 316: verify DepthStencilState.StencilMask (read mask) and StencilWriteMask (write mask)
// actually affect stencil comparisons and writes.
//
// IMPORTANT: PresentationParameters.DepthStencilFormat defaults to DepthFormat::Depth24 (no
// stencil aspect) -- this test's constructor explicitly requests DepthFormat::Depth24Stencil8 via
// GraphicsDeviceManager, same as Task 315's test. Do not remove this. Also remember
// GraphicsDevice::Clear ignores ClearOptions::Stencil entirely (Task 871) -- every column
// establishes its own known stencil baseline via a real "stamp" draw with a full write mask,
// never via Clear(). Depth testing is disabled throughout (DepthBufferEnable=false) to isolate
// stencil behaviour from the already-tracked depth-compare bug (Task 870).
//
// Method: 4 columns, each testing one property with an unambiguous, differential pair of checks.
//
// Columns 1-2 (StencilMask, the read mask applied to both the buffer value and ReferenceStencil
// before comparing):
//   Stamp: write stencil=0x05 (0b0101) with a full write mask.
//   Column 1 (narrow StencilMask=0x01): test ReferenceStencil=0x01, StencilFunction=Equal.
//     (0x01 & 0x01) == (0x05 & 0x01) -> 0x01 == 0x01 -> PASS -> GREEN, if the mask is honored.
//   Column 2 (full StencilMask=0xFF, contrast/control): same test but no masking applied.
//     0x01 == 0x05 is false -> FAIL -> stays BACKGROUND. If column 1 ignored StencilMask (treated
//     as this same full/no-op mask), it would ALSO show BACKGROUND -- pairing narrow vs full masks
//     with opposite expected outcomes is what proves the mask is genuinely read, not coincidental.
//
// Columns 3-4 (StencilWriteMask, which bits of a stencil write actually take effect):
//   Stamp: write stencil=0xFF with a full write mask.
//   Write: StencilFunction=Always (always executes the Pass op), StencilPass=Replace,
//     ReferenceStencil=0x00.
//   Column 3 (narrow StencilWriteMask=0x0F): only the lower 4 bits are writable -> if honored,
//     final buffer = (0x00 & 0x0F) | (0xFF & ~0x0F) = 0xF0. Read back with StencilFunction=Equal,
//     ReferenceStencil=0xF0, full read mask -> PASS -> GREEN.
//   Column 4 (full StencilWriteMask=0xFF, contrast/control): the whole buffer is replaced ->
//     final buffer = 0x00. The SAME read-back check (expects 0xF0) now correctly FAILS -> stays
//     BACKGROUND. If column 3 ignored StencilWriteMask (treated as this same full mask), it would
//     ALSO show BACKGROUND -- again the narrow/full pairing is what proves the write mask works.
//
// NOTE: this project's Vulkan backend (VulkanGraphicsBackend::ApplyDepthStencilState, tracked as
// Task 870) never sets stencilTestEnable at all, so the stencil test never gates fragments
// regardless of any property, including StencilMask/StencilWriteMask. Expect columns 1 and 3
// (which expect PASS/GREEN) to pass on Vulkan purely by coincidence -- since nothing ever gates,
// every column renders GREEN -- while columns 2 and 4 (which expect FAIL/BACKGROUND) correctly
// fail, revealing the bug. This is a further reconfirmation of Task 870, not a new bug; do not
// read columns 1/3 passing as evidence the masks work on Vulkan.
//
// Exit code 0 = all 4 checks PASS, 1 = any FAIL.

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
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    DepthStencilState MakeStampState(int referenceStencil, int writeMask)
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Always);
        ds.setStencilPassProperty(StencilOperation::Replace);
        ds.setReferenceStencilProperty(referenceStencil);
        ds.setStencilWriteMaskProperty(writeMask);
        return ds;
    }

    DepthStencilState MakeTestState(int referenceStencil, int readMask)
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Equal);
        ds.setReferenceStencilProperty(referenceStencil);
        ds.setStencilMaskProperty(readMask);
        ds.setStencilPassProperty(StencilOperation::Keep);
        ds.setStencilFailProperty(StencilOperation::Keep);
        return ds;
    }
}

class DepthStencilStateStencilMaskTest : public Game
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

        const float colW = 2.0f / 4.0f;
        auto colX = [&](int i) { return -1.0f + colW * static_cast<float>(i); };

        // Columns 1-2: StencilMask (read mask). Stamp 0x05, then test with narrow vs full mask.
        for (int i = 0; i < 2; ++i)
        {
            const float x0 = colX(i), x1 = x0 + colW;
            dev.setDepthStencilStateProperty(MakeStampState(0x05, 0x7FFFFFFF));
            DrawQuad(dev, x0, x1, kBackground);

            const int readMask = (i == 0) ? 0x01 : 0xFF;
            dev.setDepthStencilStateProperty(MakeTestState(0x01, readMask));
            DrawQuad(dev, x0, x1, kGreen);
        }

        // Columns 3-4: StencilWriteMask. Stamp 0xFF, then Replace-with-0x00 using narrow vs full
        // write mask, then read back expecting 0xF0 (only honored when the write mask is narrow).
        for (int i = 2; i < 4; ++i)
        {
            const float x0 = colX(i), x1 = x0 + colW;
            dev.setDepthStencilStateProperty(MakeStampState(0xFF, 0x7FFFFFFF));
            DrawQuad(dev, x0, x1, kBackground);

            const int writeMask = (i == 2) ? 0x0F : 0xFF;
            dev.setDepthStencilStateProperty(MakeStampState(0x00, writeMask));
            DrawQuad(dev, x0, x1, kBackground);

            dev.setDepthStencilStateProperty(MakeTestState(0xF0, 0xFF));
            DrawQuad(dev, x0, x1, kGreen);
        }

        Color results[4] = {
            Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0)
        };
        for (int i = 0; i < 4; ++i)
        {
            const float cx = colX(i) + colW * 0.5f;
            const int px = static_cast<int>((cx + 1.0f) * 0.5f * vp.getWidthProperty());
            Rectangle reg(px, vp.getHeightProperty() / 2, 1, 1);
            dev.GetBackBufferData(&reg, &results[i], 0, 1);
        }

        const char* names[4] = {
            "StencilMask=0x01 (narrow, expect PASS)",
            "StencilMask=0xFF (full, expect FAIL)",
            "StencilWriteMask=0x0F (narrow, expect PASS)",
            "StencilWriteMask=0xFF (full, expect FAIL)",
        };
        const bool expectPass[4] = { true, false, true, false };

        int passCount = 0;
        for (int i = 0; i < 4; ++i)
        {
            const Color& c = results[i];
            const bool ok = expectPass[i] ? IsGreen(c) : IsBackground(c);
            std::printf("[%s] %s: centre=(%d,%d,%d)\n",
                        ok ? "PASS" : "FAIL", names[i],
                        c.getRProperty(), c.getGProperty(), c.getBProperty());
            if (ok) ++passCount;
        }

        result_ = (passCount == 4) ? 0 : 1;
        Exit();
    }

public:
    DepthStencilStateStencilMaskTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }

    int getResult() const { return result_; }
};

int main()
{
    DepthStencilStateStencilMaskTest game;
    game.Run();
    return game.getResult();
}
