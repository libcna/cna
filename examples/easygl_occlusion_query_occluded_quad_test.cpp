// SPDX-License-Identifier: MS-PL
// Task 446: Pixel/query test -- a FULLY OCCLUDED quad (hidden entirely behind a nearer, opaque
// occluder) returns a ZERO (or lower) pixel count from an OcclusionQuery, on EasyGL. The mirror
// counterpart to Task 445's fully-visible-quad test.
//
// Method (matches this project's own established DepthStencilState pixel-test convention, e.g.
// examples/easygl_depthstencilstate_compare_function_test.cpp): draw a full-NDC Blue occluder quad
// at depth 0.3 with DepthStencilState::Default (writes depth, DepthBufferFunction = LessEqual, per
// DepthStencilState.cpp), then draw a full-NDC Red target quad at depth 0.7 -- 0.7 <= 0.3 is FALSE,
// so every one of the target quad's fragments is rejected by the depth test. The target quad's draw
// call is wrapped in Begin()/End(); if depth testing genuinely occludes it, the query should report
// PixelCount() == 0. Z values kept within [0,1] per this project's own established Vulkan
// clip-space-Z convention (not exercised by this EasyGL-only test, but kept consistent anyway).
//
// GetBackBufferData (via glReadPixels, a synchronizing GL call) both forces the driver to flush
// pending work so the query's result becomes available (same technique as Task 445) and
// independently confirms genuine occlusion: the backbuffer must still show the OCCLUDER's colour
// (Blue), not the target quad's colour (Red) and not the original background -- proving the
// occluder is actually in front, not just that nothing was drawn.
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kBlue (  0,   0, 255, 255); // occluder
    const Color kRed  (255,   0,   0, 255); // target (should be fully hidden)
    const Color kGreen(  0, 255,   0, 255); // background

    bool colourMatch(Color got, Color want, int tol = 40)
    {
        return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
            && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
            && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
    }

    std::unique_ptr<VertexBuffer> MakeQuad(GraphicsDevice& device, float z, Color colour)
    {
        const VertexPositionColor verts[4] = {
            { Vector3(-1.0f,  1.0f, z), colour },
            { Vector3(-1.0f, -1.0f, z), colour },
            { Vector3( 1.0f, -1.0f, z), colour },
            { Vector3( 1.0f,  1.0f, z), colour },
        };
        auto vb = std::make_unique<VertexBuffer>(device, 4);
        vb->SetData(verts, 4);
        return vb;
    }
}

class OcclusionQueryOccludedQuadTest : public Game
{
    std::unique_ptr<VertexBuffer> vbOccluder_, vbTarget_;
    std::unique_ptr<IndexBuffer>  ib_;
    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        vbOccluder_ = MakeQuad(device, 0.3f, kBlue); // nearer
        vbTarget_   = MakeQuad(device, 0.7f, kRed);  // farther -- should be fully occluded

        const uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
        ib_ = std::make_unique<IndexBuffer>(device, 6);
        ib_->SetData(indices, 6);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kGreen, 1.0f, 0);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::Default);

        BasicEffect fx(device);
        fx.VertexColorEnabled = true;

        auto drawQuad = [&](VertexBuffer* vb) {
            device.SetVertexBuffer(vb);
            device.setIndicesProperty(ib_.get());
            EffectTechnique* technique = fx.getCurrentTechniqueProperty();
            for (EffectPass& pass : technique->getPassesProperty())
            {
                pass.Apply();
                device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            }
        };

        // Occluder first, no query -- writes depth 0.3 across the whole viewport.
        drawQuad(vbOccluder_.get());

        // Target quad, farther away (depth 0.7): DepthStencilState::Default's LessEqual compare
        // rejects every fragment (0.7 <= 0.3 is false) -- fully occluded.
        OcclusionQuery query(device);
        query.Begin();
        drawQuad(vbTarget_.get());
        query.End();

        auto sample = [&](int x, int y) {
            Color px(0, 0, 0, 0);
            Rectangle reg(x, y, 1, 1);
            device.GetBackBufferData(&reg, &px, 0, 1);
            return px;
        };

        Color centre = sample(W / 2, H / 2);

        bool complete = query.getIsCompleteProperty();
        for (int attempt = 0; attempt < 30 && !complete; ++attempt)
        {
            sample(W / 2, H / 2);
            complete = query.getIsCompleteProperty();
        }

        check(colourMatch(centre, kBlue),
              "occluder's own colour (Blue) still shows -- target quad genuinely hidden behind it");
        check(complete, "OcclusionQuery becomes complete after the occluded draw call");
        check(query.getPixelCountProperty() <= 0,
              "fully occluded quad -> OcclusionQuery.PixelCount() is zero (or lower)");

        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    OcclusionQueryOccludedQuadTest game;
    game.Run();
    return game.getResult();
}
