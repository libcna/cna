// SPDX-License-Identifier: MS-PL
// plans/plan_sokol.md SOKOL-48: a compact state-transition/lifetime regression matrix covering the
// failure dimensions the 2026-08-03 Phase 8 audit exposed but SOKOL-40..47's own per-ticket tests
// did not each individually close.
//
// Two dimensions specifically deferred here by name in earlier tickets' own closing notes:
//
// Part 1 -- two independent OcclusionQuery instances, interleaved in BOTH begin/end orders,
// followed by a fresh healthy query (plans/plan_sokol.md SOKOL-43's own note: "a dedicated two-object
// interleaving test is deferred to SOKOL-48"). Proves TryActivateOcclusionQueryEXT()/
// ReleaseOcclusionQueryEXT() genuinely hand the context's one active-query slot back and forth
// between two real objects rather than merely not-crashing -- each query's own PixelCount() must
// reflect ONLY the geometry drawn during ITS OWN Begin()/End() window, and the shared slot must
// not get stuck after either ordering.
//
// Part 2 -- DrawCustomEffect3D (the 3D custom-effect raw-GL path, not SpriteBatch's) must apply
// its own RasterizerState.CullMode rather than inheriting whatever cull state a PRECEDING STOCK
// draw left configured in the GL context (plans/plan_sokol.md SOKOL-41's own note: "the DrawCustomEffect3D
// (3D) path [is] left to SOKOL-48's broader regression matrix" for the state axes beyond depth,
// which SOKOL-41's own fix already covers via the shared ApplyCustomEffectRasterStateEXT() helper
// -- this is the pixel-level proof that helper's cull/winding half works end-to-end).
//
// Exit code 0 = PASS, 1 = FAIL, 77 = skipped (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBackBufferWidth = 320;
    constexpr int kBackBufferHeight = 240;

    const Color kRed(255, 0, 0, 255);
    const Color kBlack(0, 0, 0, 255);
    const Color kYellow(255, 255, 0, 255);

    /// Same pixel-space orthographic camera sokol_3d_test.cpp establishes, reused here so the two
    /// triangles' expected winding/visibility follow from their own coordinates.
    Matrix PixelSpaceProjection()
    {
        return Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(kBackBufferWidth),
            static_cast<float>(kBackBufferHeight), 0.0f,
            -1.0f, 1.0f);
    }

    // location 0 = vec3 position, location 1 = vec4 color (ubyte4n) -- the stride-16
    // VertexPositionColor fallback layout BuildCustomEffectAttribLayoutEXT recognises.
    const char* kVertSrc = R"(#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
void main() { gl_Position = Projection * View * World * vec4(aPosition, 1.0); }
)";

    const char* kFragSrc = R"(#version 410 core
out vec4 FragColor;
void main() { FragColor = vec4(1.0, 1.0, 0.0, 1.0); }
)";
}

class StateLifetimeRegressionMatrixTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    // ---------------------------------------------------------------------------------------
    // Part 1: two-object OcclusionQuery interleaving.
    // ---------------------------------------------------------------------------------------

    void DrawOccluderQuad(GraphicsDevice& device, BasicEffect& fx, float x, float y, float size)
    {
        const Vector3 tl(x, y, 0.0f), tr(x + size, y, 0.0f);
        const Vector3 br(x + size, y + size, 0.0f), bl(x, y + size, 0.0f);
        const VertexPositionColor verts[6] = {
            { tl, kRed }, { tr, kRed }, { br, kRed },
            { tl, kRed }, { br, kRed }, { bl, kRed },
        };
        VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 6,
                            BufferUsage::None);
        buffer.SetData(verts, 6);
        device.SetVertexBuffer(&buffer);
        fx.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
    }

    /// Forces the GL driver to flush pending commands (matches the established
    /// easygl_occlusion_query_visible_quad_test.cpp convention) and polls IsComplete() with a
    /// bounded retry loop.
    void SettleQuery(GraphicsDevice& device, OcclusionQuery& query)
    {
        const Rectangle probe(0, 0, 1, 1);
        Color px(0, 0, 0, 0);
        for (int attempt = 0; attempt < 30 && !query.getIsCompleteProperty(); ++attempt)
            device.GetBackBufferData(&probe, &px, 0, 1);
    }

    /// Runs one interleaving round: `first` begins, `second`'s overlapping Begin() must be
    /// silently absorbed (SOKOL-43), a BIG quad is drawn and captured only by `first`, `first`
    /// ends (releasing the slot), `second` then begins for real and captures a SMALL quad.
    /// Returns {firstPixelCount, secondPixelCount}.
    std::pair<int, int> RunInterleavingRound(GraphicsDevice& device, BasicEffect& fx,
                                              OcclusionQuery& first, OcclusionQuery& second)
    {
        device.Clear(kBlack);
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        first.Begin();
        second.Begin();  // overlapping -- must be silently absorbed, not a second real query.
        DrawOccluderQuad(device, fx, 20.0f, 20.0f, 200.0f);  // big
        first.End();

        second.Begin();  // the slot is free now -- this one must actually start.
        DrawOccluderQuad(device, fx, 20.0f, 20.0f, 20.0f);  // small
        second.End();

        SettleQuery(device, first);
        SettleQuery(device, second);
        return { first.getPixelCountProperty(), second.getPixelCountProperty() };
    }

    void RunOcclusionQueryInterleaving(GraphicsDevice& device)
    {
        BasicEffect fx(device);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(PixelSpaceProjection());

        // Round A: q1 goes first (big quad), q2 second (small quad).
        {
            OcclusionQuery q1(device);
            OcclusionQuery q2(device);
            auto [bigCount, smallCount] = RunInterleavingRound(device, fx, q1, q2);
            check(bigCount > 0 && smallCount > 0,
                  "A1: both queries report a positive pixel count (q1-first order)");
            check(bigCount > smallCount,
                  "A2: the big-quad query reports more samples than the small-quad query "
                  "(q1-first order) -- proves no cross-contamination between the two queries");
        }

        // Round B: same two roles, reversed object order -- q2 goes first this time.
        {
            OcclusionQuery q1(device);
            OcclusionQuery q2(device);
            auto [bigCount, smallCount] = RunInterleavingRound(device, fx, q2, q1);
            check(bigCount > 0 && smallCount > 0,
                  "B1: both queries report a positive pixel count (q2-first order)");
            check(bigCount > smallCount,
                  "B2: the big-quad query reports more samples than the small-quad query "
                  "(q2-first order) -- proves the coordination is symmetric, not order-dependent");
        }

        // A plain, single-object cycle afterward -- the shared active-query slot must not be
        // left permanently claimed by either round above.
        {
            device.Clear(kBlack);
            OcclusionQuery fresh(device);
            fresh.Begin();
            DrawOccluderQuad(device, fx, 20.0f, 20.0f, 100.0f);
            fresh.End();
            SettleQuery(device, fresh);
            check(fresh.getPixelCountProperty() > 0,
                  "C: a fresh query after both interleaving rounds completes normally -- the "
                  "shared active-query slot was correctly released, not left stuck");
        }
    }

    // ---------------------------------------------------------------------------------------
    // Part 2: DrawCustomEffect3D cull-mode independence from a preceding stock draw.
    // ---------------------------------------------------------------------------------------

    void RunCustomEffectCullIndependence(GraphicsDevice& device)
    {
        device.Clear(kBlack);
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        // Stock draw: XNA's default CullCounterClockwise, on a screen-space CLOCKWISE-wound
        // triangle (survives -- matches sokol_3d_test.cpp's own check G proof) -- this leaves the
        // GL context's cull state configured for "remove counter-clockwise faces".
        BasicEffect stockFx(device);
        stockFx.VertexColorEnabled = true;
        stockFx.setWorldProperty(Matrix::getIdentityProperty());
        stockFx.setViewProperty(Matrix::getIdentityProperty());
        stockFx.setProjectionProperty(PixelSpaceProjection());
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        {
            const VertexPositionColor verts[3] = {
                { Vector3(20.0f, 20.0f, 0.0f), kRed },
                { Vector3(60.0f, 20.0f, 0.0f), kRed },
                { Vector3(60.0f, 60.0f, 0.0f), kRed },
            };
            VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                                BufferUsage::None);
            buffer.SetData(verts, 3);
            device.SetVertexBuffer(&buffer);
            stockFx.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            device.SetVertexBuffer(nullptr);
        }
        Color stockPixel(0, 0, 0, 0);
        {
            const Rectangle reg(55, 55, 1, 1);
            device.GetBackBufferData(&reg, &stockPixel, 0, 1);
        }
        check(stockPixel.getRProperty() >= 200 && stockPixel.getGProperty() <= 20,
              "D1: sanity -- the stock CullCounterClockwise draw's clockwise-wound triangle "
              "survives (matches sokol_3d_test.cpp's own established proof)");

        // Custom-effect draw immediately after: RasterizerState::CullClockwise (the OPPOSITE
        // setting) on a screen-space COUNTER-CLOCKWISE-wound triangle -- this same triangle would
        // have been REMOVED by the stock draw's CullCounterClockwise state above, so if
        // DrawCustomEffect3D inherited that leftover GL cull state instead of applying its own,
        // this triangle would incorrectly vanish too.
        ShaderEffect customFx(device, kVertSrc, kFragSrc);
        if (!customFx.IsEffectValid())
        {
            check(false, "D2: custom-effect GLSL compiled");
            return;
        }
        customFx.setWorldProperty(Matrix::getIdentityProperty());
        customFx.setViewProperty(Matrix::getIdentityProperty());
        customFx.setProjectionProperty(PixelSpaceProjection());
        device.setRasterizerStateProperty(RasterizerState::CullClockwise);
        {
            // Same counter-clockwise winding as sokol_3d_test.cpp's own "removed" triangle (its
            // corners 2/3 swapped relative to the clockwise one above) -- CullClockwise keeps it.
            const VertexPositionColor verts[3] = {
                { Vector3(100.0f, 20.0f, 0.0f), kYellow },
                { Vector3(100.0f, 60.0f, 0.0f), kYellow },
                { Vector3(140.0f, 60.0f, 0.0f), kYellow },
            };
            VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                                BufferUsage::None);
            buffer.SetData(verts, 3);
            device.SetVertexBuffer(&buffer);
            customFx.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            device.SetVertexBuffer(nullptr);
        }

        Color customPixel(0, 0, 0, 0);
        {
            // Centroid of (100,20)/(100,60)/(140,60), safely interior (not on the hypotenuse).
            const Rectangle reg(113, 47, 1, 1);
            device.GetBackBufferData(&reg, &customPixel, 0, 1);
        }
        check(customPixel.getRProperty() >= 200 && customPixel.getGProperty() >= 200
                  && customPixel.getBProperty() <= 20,
              "D2 (KEY): DrawCustomEffect3D's CullClockwise keeps a counter-clockwise-wound "
              "triangle right after a stock draw left the OPPOSITE cull state configured -- a "
              "leaked stock cull state would incorrectly cull this triangle too");
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        RunOcclusionQueryInterleaving(device);
        RunCustomEffectCullIndependence(device);
        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
    }

public:
    StateLifetimeRegressionMatrixTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBackBufferWidth);
        gdm_->setPreferredBackBufferHeightProperty(kBackBufferHeight);
    }

    [[nodiscard]] bool Failed() const { return fail_ > 0; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    StateLifetimeRegressionMatrixTest game;
    game.Run();
    return game.Failed() ? 1 : 0;
}
