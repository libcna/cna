// SPDX-License-Identifier: MS-PL
// WEBGPU-41/77/78/79/80/81/82/83: verifies WebGPUGraphicsBackend's real BlendState/
// RasterizerState/scissor/viewport wiring, closing the gap where every 3D draw silently ignored
// these (ApplyBlendState/ApplyRasterizerState/SetScissorRect/SetViewport had no override at all,
// falling back to IGraphicsBackend's no-op defaults -- confirmed by grepping this backend's own
// .cpp before this task).
//
// Check A -- CullMode.CullClockwiseFace culls a known-winding quad: it must NOT render.
// Check B -- CullMode.CullCounterClockwiseFace does NOT cull the SAME quad: it must render.
//   (A+B together prove real, direction-correct cull-mode wiring -- not just "a pipeline was
//   created". The quad's own winding is derived by hand in the comment above DrawWindingQuad().)
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
// Check G -- FillMode.WireFrame does not crash or misbehave (WEBGPU-115: wgpu-native has no
//   polygon-mode API at all, a documented, accepted deviation -- this is a smoke check only, not
//   a real wireframe-rendering check).
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

#include <cstdio>
#include <cstdlib>
#include <memory>

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
    // (both share the top-left/bottom-right diagonal). Hand-derived winding for triangle 1
    // (A=(-1,1), B=(-1,-1), C=(1,-1)): the NDC (Y-up, math-convention) signed area is
    //   cross((B-A),(C-B)) = cross((0,-2)),(2,0)) = 0*0 - (-2)*2 = +4  =>  CCW in NDC.
    // WGPU/D3D determine front/back winding in RASTER space (Y-down, row 0 = top), which mirrors
    // NDC across the X axis -- a single-axis mirror reverses chirality, so this triangle is
    // CLOCKWISE in raster space. XNA's own default RasterizerState (CullCounterClockwiseFace)
    // culls counter-clockwise (raster-space) faces and keeps clockwise ones as "front" -- so this
    // quad is a real, ordinary XNA front-facing quad, exactly like every other 3D test in this
    // suite already draws, not a specially-reversed one.
    void DrawWindingQuad(GraphicsDevice& dev, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.5f), color },
            { Vector3(-1.0f, -1.0f, 0.5f), color },
            { Vector3( 1.0f, -1.0f, 0.5f), color },
            { Vector3(-1.0f,  1.0f, 0.5f), color },
            { Vector3( 1.0f, -1.0f, 0.5f), color },
            { Vector3( 1.0f,  1.0f, 0.5f), color },
        };
        ApplyBasicEffect(dev);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }
}

class WebGpuGraphicsStateTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    static constexpr int kTotalChecks = 8;
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

        // ---- Check A: CullClockwiseFace culls this (raster-space-clockwise) quad. ----
        {
            RasterizerState rs;
            rs.setCullModeProperty(CullMode::CullClockwiseFace);
            dev.setRasterizerStateProperty(rs);
            dev.Clear(Color::Black);
            DrawWindingQuad(dev, Color::White);
            check(colorNear(readPixel(dev, kSize / 2, kSize / 2), Color::Black),
                  "CullClockwiseFace culls the (raster-space-clockwise) quad -- background stays");
        }

        // ---- Check B: CullCounterClockwiseFace does NOT cull the SAME quad. ----
        {
            RasterizerState rs;
            rs.setCullModeProperty(CullMode::CullCounterClockwiseFace);
            dev.setRasterizerStateProperty(rs);
            dev.Clear(Color::Black);
            DrawWindingQuad(dev, Color::White);
            check(colorNear(readPixel(dev, kSize / 2, kSize / 2), Color::White),
                  "CullCounterClockwiseFace keeps the quad visible (does not cull it)");
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
        // was issued under -- it passes whether the backend captures the rectangle per draw or
        // resolves it when it records the pass, and it therefore could not see this backend's
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

        // ---- Check G: FillMode.WireFrame does not crash (WEBGPU-115 documented deviation). ----
        {
            RasterizerState rs;
            rs.setFillModeProperty(FillMode::WireFrame);
            dev.setRasterizerStateProperty(rs);
            dev.Clear(Color::Black);
            DrawWindingQuad(dev, Color::White);
            readPixel(dev, kSize / 2, kSize / 2); // must not throw/crash -- pixel value not asserted
            check(true, "FillMode.WireFrame does not crash (real wireframe rendering not "
                        "implemented -- WEBGPU-115 documented deviation, wgpu-native has no "
                        "polygon-mode API)");
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
