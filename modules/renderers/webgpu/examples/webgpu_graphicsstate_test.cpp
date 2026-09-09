// SPDX-License-Identifier: MS-PL
// WEBGPU-41/77/78/79/80/81/82/83: verifies WebGPURenderer's real BlendState/
// RasterizerState/scissor/viewport wiring, closing the gap where every 3D draw silently ignored
// these (ApplyBlendState/ApplyRasterizerState/SetScissorRect/SetViewport had no override at all,
// falling back to IGraphicsRenderer's no-op defaults -- confirmed by grepping this renderer's own
// .cpp before this task).
//
// Check A1/A2 -- CullMode.CullClockwiseFace removes the CLOCKWISE-as-displayed quad and KEEPS the
//   counter-clockwise one.
// Check B1/B2 -- CullMode.CullCounterClockwiseFace is the exact complement of that.
//   (REMED-GFX-160: these four together prove real, direction-correct cull-mode wiring. They used
//   to be two checks over ONE quad whose winding was labelled by a hand derivation that came out
//   backwards, which is how this renderer's cull mapping ended up inverted -- see the comment above
//   DrawWindingQuad(). Both windings are now drawn so no single label can invert it again.)
// Check C -- BlendState.NonPremultiplied genuinely blends a 50%-alpha red quad over a black
//   background: the result must land strictly between black and pure red, not either extreme.
// Check D -- BlendState.Opaque with the SAME 50%-alpha draw produces the PURE quad colour
//   (alpha is ignored, straight overwrite) -- a genuine differential against Check C, proving
//   ApplyBlendState's real factors reach the pipeline, not just an enabled/disabled bit.
// Check E -- ScissorRectangle clips a full-screen quad exactly at its boundary: a pixel just
//   inside the scissor rect shows the quad colour, a pixel just outside it shows the untouched
//   clear colour.
// Check F -- Viewport smaller than the backbuffer confines a full-clip-space quad to that
//   sub-rectangle: a pixel inside the viewport shows the quad colour, a pixel outside it (but
//   still inside the backbuffer) shows the untouched clear colour.
// Check G -- FillMode.WireFrame renders a WIREFRAME (WEBGPU-153). It asserted WEBGPU-115's
//   refusal contract until WEBGPU-153 implemented the state by expanding each triangle's edges
//   into a line list -- the reference renderer's own mechanism, which needs no polygon-mode API on
//   any target. The check now asserts that the draw is served rather than refused, that the quad's
//   interior is EMPTY under WireFrame and filled under Solid through the identical route, and that
//   an ordinary Solid draw works immediately afterwards.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    bool colorNear(Color a, Color b, int tol = 16)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tol;
    }

    std::string ColorStr(const Color& c)
    {
        return '(' + std::to_string(c.getRProperty()) + ',' + std::to_string(c.getGProperty()) +
               ',' + std::to_string(c.getBProperty()) + ',' + std::to_string(c.getAProperty()) + ')';
    }

    Color readPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    void ApplyBasicEffect(GraphicsDevice& dev, bool vertexColor = true)
    {
        static BasicEffect* fx = nullptr;
        if (fx == nullptr) fx = new BasicEffect(dev);
        fx->setWorldProperty(Matrix::getIdentityProperty());
        fx->setViewProperty(Matrix::getIdentityProperty());
        fx->setProjectionProperty(Matrix::getIdentityProperty());
        fx->VertexColorEnabled = vertexColor;
        fx->Apply();
    }

    // A full-screen quad (clip-space -1..1), built from 2 triangles that share the SAME winding
    // (both share the top-left/bottom-right diagonal).
    //
    // REMED-GFX-160 -- THE WINDING OF THIS QUAD, AND THE DERIVATION THAT USED TO BE HERE.
    // The vertex order below is TL -> BL -> BR, whose NDC (Y-up) signed area is +4, i.e. CCW in
    // NDC. The old comment then argued that raster space mirrors NDC across the X axis, so the
    // triangle is "CLOCKWISE in raster space", and concluded it was "a real, ordinary XNA
    // front-facing quad". The first half is arithmetic and correct; the conclusion is not, and it
    // is what inverted this renderer's cull mapping for real games.
    //
    // Flipping to Y-down raster coordinates does flip the SIGN of the computed area -- but the name
    // a GPU API attaches to that sign is defined IN that Y-down space, so "clockwise in raster
    // space" means COUNTER-CLOCKWISE AS DISPLAYED. XNA's enums are named for the DISPLAYED
    // orientation instead: FNA's SpriteBatch emits TL -> TR -> BL and BR -> BL -> TR, both
    // clockwise as displayed, and they survive the RasterizerState.CullCounterClockwise that
    // SpriteBatch.Begin itself defaults to. So clockwise-as-displayed is XNA's FRONT face, and
    // TL -> BL -> BR is a BACK face -- the exact opposite of what checks A and B used to assert.
    //
    // Rather than just flip the two expectations, both windings are now drawn and each cull mode
    // is asserted to be their exact complement, so no future reading of one quad's hand-derived
    // label can invert this renderer again. The FNA-derived contract itself is measured across every
    // renderer by examples/frontface_winding_test.cpp.
    //
    // The DEFAULT is the FRONT-facing (clockwise-as-displayed) quad, deliberately. Checks E and H
    // below construct a fresh `RasterizerState` to turn scissor testing on, and a default-
    // constructed RasterizerState carries XNA's default CullCounterClockwiseFace -- so with a
    // back-facing default quad those checks would silently depend on a BACK face staying visible
    // under the default cull mode, which is the same false premise that inverted this renderer.
    // They passed only because the mapping was inverted too, and both broke the moment it was
    // corrected. Ordinary front-facing geometry is what a real game draws and what they mean.
    //
    // @param backFacing  true selects TL -> BL -> BR / TL -> BR -> TR, which is COUNTER-clockwise
    //                    as displayed and therefore XNA's BACK face.
    void DrawWindingQuad(GraphicsDevice& dev, const Color& color, bool backFacing = false)
    {
        const Vector3 tl(-1.0f,  1.0f, 0.5f), tr( 1.0f,  1.0f, 0.5f);
        const Vector3 bl(-1.0f, -1.0f, 0.5f), br( 1.0f, -1.0f, 0.5f);
        const VertexPositionColor back[6] = {   // counter-clockwise as displayed: a BACK face
            { tl, color }, { bl, color }, { br, color },
            { tl, color }, { br, color }, { tr, color },
        };
        const VertexPositionColor front[6] = {  // clockwise as displayed: FNA's sprite winding
            { tl, color }, { tr, color }, { bl, color },
            { br, color }, { bl, color }, { tr, color },
        };
        ApplyBasicEffect(dev);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, backFacing ? back : front, 0, 2);
    }
}

class WebGpuGraphicsStateTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    static constexpr int kTotalChecks = 14;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);

        // ---- Checks A1/A2: CullClockwiseFace removes the CLOCKWISE-as-displayed quad and keeps
        //      the counter-clockwise one. REMED-GFX-160: each enum names the face it CULLS. ----
        {
            RasterizerState rs;
            rs.setCullModeProperty(CullMode::CullClockwiseFace);
            dev.setRasterizerStateProperty(rs);
            dev.Clear(Color::Black);
            DrawWindingQuad(dev, Color::White);
            check(colorNear(readPixel(dev, kSize / 2, kSize / 2), Color::Black),
                  "CullClockwiseFace culls the clockwise-as-displayed quad (FNA's own sprite "
                  "winding, XNA's front face) -- background stays");

            dev.Clear(Color::Black);
            DrawWindingQuad(dev, Color::White, /*backFacing=*/true);
            check(colorNear(readPixel(dev, kSize / 2, kSize / 2), Color::White),
                  "CullClockwiseFace KEEPS the counter-clockwise-as-displayed quad");
        }

        // ---- Checks B1/B2: CullCounterClockwiseFace is the exact complement of that. ----
        {
            RasterizerState rs;
            rs.setCullModeProperty(CullMode::CullCounterClockwiseFace);
            dev.setRasterizerStateProperty(rs);
            dev.Clear(Color::Black);
            DrawWindingQuad(dev, Color::White);
            check(colorNear(readPixel(dev, kSize / 2, kSize / 2), Color::White),
                  "CullCounterClockwiseFace KEEPS the clockwise-as-displayed quad -- FNA's sprite "
                  "winding must survive XNA's default cull mode");

            dev.Clear(Color::Black);
            DrawWindingQuad(dev, Color::White, /*backFacing=*/true);
            check(colorNear(readPixel(dev, kSize / 2, kSize / 2), Color::Black),
                  "CullCounterClockwiseFace culls the counter-clockwise-as-displayed quad");
        }

        // Reset to a neutral (no culling) rasterizer state for the remaining checks -- they are
        // not about cull mode, and the quad's winding is only guaranteed correct for this one.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        // ---- Check C: BlendState.NonPremultiplied genuinely blends. ----
        {
            dev.setBlendStateProperty(BlendState::NonPremultiplied);
            dev.Clear(Color::Black);
            DrawWindingQuad(dev, Color(255, 0, 0, 128));
            const Color blended = readPixel(dev, kSize / 2, kSize / 2);
            // Expected: src.rgb*a + dst.rgb*(1-a) with dst=black, a=128/255 => R ~ 128, G=B=0.
            const bool strictlyBetween = blended.getRProperty() > 40 && blended.getRProperty() < 215 &&
                                         blended.getGProperty() < 16 && blended.getBProperty() < 16;
            check(strictlyBetween,
                  "BlendState.NonPremultiplied: 50%-alpha red over black lands strictly between "
                  "black and pure red (real blend, not overwrite-or-nothing)");
        }

        // ---- Check D: BlendState.Opaque ignores alpha -- pure overwrite (differential vs C). ----
        {
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.Clear(Color::Black);
            DrawWindingQuad(dev, Color(255, 0, 0, 128));
            const Color opaque = readPixel(dev, kSize / 2, kSize / 2);
            check(colorNear(opaque, Color::Red),
                  "BlendState.Opaque: the SAME 50%-alpha red draw is a pure, unblended overwrite");
        }

        dev.setBlendStateProperty(BlendState::Opaque);

        // ---- Check E: ScissorRectangle clips exactly at its boundary. ----
        {
            RasterizerState rs;
            rs.setScissorTestEnableProperty(true);
            dev.setRasterizerStateProperty(rs);
            dev.setScissorRectangleProperty(Rectangle(0, 0, kSize / 2, kSize));
            dev.Clear(Color::Black);
            DrawWindingQuad(dev, Color::White);
            const Color inside = readPixel(dev, kSize / 4, kSize / 2);       // x=16, inside [0,32)
            const Color outside = readPixel(dev, kSize / 2 + kSize / 4, kSize / 2); // x=48, outside
            check(colorNear(inside, Color::White) && colorNear(outside, Color::Black),
                  "ScissorRectangle clips the quad exactly at its boundary "
                  "(inside=quad colour, outside=untouched clear colour)");
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.setScissorRectangleProperty(Rectangle(0, 0, kSize, kSize));
        }

        // ---- Check F: Viewport confines rendering to a sub-rectangle. ----
        {
            dev.setViewportProperty(Viewport(0, 0, kSize / 2, kSize));
            dev.Clear(Color::Black);
            DrawWindingQuad(dev, Color::White);
            const Color inside = readPixel(dev, kSize / 4, kSize / 2);
            const Color outside = readPixel(dev, kSize / 2 + kSize / 4, kSize / 2);
            check(colorNear(inside, Color::White) && colorNear(outside, Color::Black),
                  "Viewport confines the full-clip-space quad to its own sub-rectangle "
                  "(inside=quad colour, outside=untouched clear colour)");
            dev.setViewportProperty(Viewport(0, 0, kSize, kSize));
        }

        // ---- Check H: TWO ScissorRectangles in one bind cycle, read once at the end. ----
        // REMED-GFX-146 false-positive audit. Check E above uses exactly ONE rectangle and reads
        // it back BEFORE restoring, so the live rectangle at flush time is still the one the draw
        // was issued under -- it passes whether the renderer captures the rectangle per draw or
        // resolves it when it records the pass, and it therefore could not see this renderer's
        // deferred-scissor defect at all. This check issues both draws and only then reads, with
        // the rectangle restored to the whole backbuffer first, so "resolve at flush time"
        // predicts white everywhere and "capture at queue time" predicts two disjoint bands.
        {
            RasterizerState rs;
            rs.setScissorTestEnableProperty(true);
            dev.setRasterizerStateProperty(rs);
            dev.Clear(Color::Black);
            dev.setScissorRectangleProperty(Rectangle(0, 0, kSize / 4, kSize));
            DrawWindingQuad(dev, Color::White);
            dev.setScissorRectangleProperty(Rectangle(kSize / 2, 0, kSize / 4, kSize));
            DrawWindingQuad(dev, Color::Red);
            dev.setScissorRectangleProperty(Rectangle(0, 0, kSize, kSize));
            const Color first  = readPixel(dev, kSize / 8, kSize / 2);          // inside rect A
            const Color gap    = readPixel(dev, (3 * kSize) / 8, kSize / 2);    // between A and B
            const Color second = readPixel(dev, (5 * kSize) / 8, kSize / 2);    // inside rect B
            const Color tail   = readPixel(dev, (7 * kSize) / 8, kSize / 2);    // past B
            check(colorNear(first, Color::White) && colorNear(gap, Color::Black) &&
                  colorNear(second, Color::Red) && colorNear(tail, Color::Black),
                  "two ScissorRectangles in one bind cycle each clip their own draw "
                  "(restored to the whole target before the single read)");
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
        }

        // ---- Check G: FillMode.WireFrame renders a WIREFRAME (plans/plan_webgpu.md WEBGPU-153). ----
        // This arm asserted WEBGPU-115's refusal contract until WEBGPU-153 implemented the state by
        // expanding each triangle's edges into a line list -- the reference renderer's own
        // mechanism, needing no polygon mode on any target. The backbuffer route is checked here;
        // the render-target routes and the full native cardinality live in the CnaTests
        // WebGpuWireFrameContract suite.
        {
            check(dev.SupportsCapability(CNA::GraphicsCapability::WireFrame),
                  "WEBGPU-153: SupportsCapability(WireFrame) reports true");

            // Solid first, as the control: the same quad through the same route, so the wireframe
            // reading below is a comparison rather than an isolated number.
            RasterizerState solid;
            solid.setCullModeProperty(CullMode::None);
            solid.setFillModeProperty(FillMode::Solid);
            dev.setRasterizerStateProperty(solid);
            dev.Clear(Color::Black);
            DrawWindingQuad(dev, Color::White);
            const Color solidCentre = readPixel(dev, kSize / 2, kSize / 2);
            check(colorNear(solidCentre, Color::White),
                  ("WEBGPU-153: Solid fills the quad's centre: got=" +
                   ColorStr(solidCentre)).c_str());

            RasterizerState rs;
            rs.setCullModeProperty(CullMode::None);
            rs.setFillModeProperty(FillMode::WireFrame);
            dev.setRasterizerStateProperty(rs);
            dev.Clear(Color::Black);

            bool threw = false;
            std::string message;
            try
            {
                DrawWindingQuad(dev, Color::White);
            }
            catch (const System::NotSupportedException& e)
            {
                threw = true;
                message = e.what();
            }
            check(!threw,
                  ("WEBGPU-153: a WireFrame draw is served rather than refused: \"" +
                   message + '"').c_str());

            // THE measurement: the quad's centre is empty under WireFrame and filled under Solid.
            // The two Clears are identical, so a centre that is still black is the interior the
            // wireframe did not fill rather than a draw that never happened -- and the Solid
            // control above is what rules the second reading out.
            const Color wireCentre = readPixel(dev, kSize / 2, kSize / 2);
            check(colorNear(wireCentre, Color::Black),
                  ("WEBGPU-153: WireFrame leaves the quad's interior unfilled: got=" +
                   ColorStr(wireCentre)).c_str());

            // And the device is immediately usable again through the identical route.
            dev.setRasterizerStateProperty(solid);
            dev.Clear(Color::Black);
            DrawWindingQuad(dev, Color::White);
            const Color recovered = readPixel(dev, kSize / 2, kSize / 2);
            check(colorNear(recovered, Color::White),
                  ("WEBGPU-153: Solid renders exactly after a WireFrame draw: got=" +
                   ColorStr(recovered)).c_str());

            dev.setRasterizerStateProperty(RasterizerState::CullNone);
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, kTotalChecks);
        result_ = (passCount_ == kTotalChecks) ? 0 : 1;
        Exit();
    }

public:
    WebGpuGraphicsStateTest()
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
    WebGpuGraphicsStateTest game;
    game.Run();
    return game.getResult();
}
