// SPDX-License-Identifier: MS-PL
// Task 445: Pixel/query test -- a fully visible quad (nothing occluding it) returns a POSITIVE
// pixel count from an OcclusionQuery, on EasyGL.
//
// Tasks 35/442-444's own existing examples/occlusion_query_test.cpp only ever checked that
// Begin()/End()/IsComplete()/PixelCount() don't crash or throw -- it never actually verified real
// occlusion-query CORRECTNESS (that a query genuinely reports "visible" for something that really
// is visible). This is the first test to do that (Task 441's own audit finding).
//
// A large, fully-visible quad is drawn (full NDC, no occlusion at all) with the draw call wrapped
// in Begin()/End(). `GetBackBufferData` is used both to force the GL driver to synchronize/flush
// pending commands (so the query's result becomes available) and to independently confirm the
// quad actually rendered its expected colour -- verifying the SAME frame both via the query API and
// via real pixel data, not just one or the other.
//
// Per OcclusionQuery.hpp's own documented GLES3 behavior, PixelCount() is 0 (none) or 1 (any) under
// GL_ANY_SAMPLES_PASSED, not a literal on-screen pixel tally -- "positive" here means exactly 1.
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
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
    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255); // background

    bool colourMatch(Color got, Color want, int tol = 40)
    {
        return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
            && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
            && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
    }
}

class OcclusionQueryVisibleQuadTest : public Game
{
    std::unique_ptr<VertexBuffer> vb_;
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

        // A full-NDC quad -- covers the entire viewport, nothing occludes it.
        const VertexPositionColor verts[4] = {
            { Vector3(-1.0f,  1.0f, 0.0f), kRed },
            { Vector3(-1.0f, -1.0f, 0.0f), kRed },
            { Vector3( 1.0f, -1.0f, 0.0f), kRed },
            { Vector3( 1.0f,  1.0f, 0.0f), kRed },
        };
        vb_ = std::make_unique<VertexBuffer>(device, 4);
        vb_->SetData(verts, 4);

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

        device.Clear(kGreen);
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        // Task 896 finding: NDC quad winding is CCW/back-facing under CNA's real default
        // RasterizerState -- needs CullNone.
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect fx(device);
        fx.VertexColorEnabled = true;

        OcclusionQuery query(device);

        query.Begin();
        device.SetVertexBuffer(vb_.get());
        device.setIndicesProperty(ib_.get());
        EffectTechnique* technique = fx.getCurrentTechniqueProperty();
        for (EffectPass& pass : technique->getPassesProperty())
        {
            pass.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        }
        query.End();

        // Force the GL driver to synchronize/flush pending commands (GetBackBufferData reads back
        // via glReadPixels) so the occlusion query's result has a chance to become available;
        // bounded retry loop in case one readback isn't enough on this driver.
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
            sample(W / 2, H / 2); // another readback, in case one flush wasn't enough
            complete = query.getIsCompleteProperty();
        }

        check(colourMatch(centre, kRed), "fully visible quad actually renders Red at centre");
        check(complete, "OcclusionQuery becomes complete after the quad's draw call");
        check(query.getPixelCountProperty() > 0,
              "fully visible quad -> OcclusionQuery.PixelCount() is positive (> 0)");

        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    OcclusionQueryVisibleQuadTest game;
    game.Run();
    return game.getResult();
}
