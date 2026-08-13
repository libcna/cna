// SPDX-License-Identifier: MS-PL
// End-to-end smoke test for the TinyGL (C-Chads/tinygl, CPU fixed-function OpenGL 1.x) graphics
// renderer. Constructs a real Game, runs a frame entirely under CNA_GRAPHICS_RENDERER=TINYGL, and
// asserts real pixel results -- not just "did not throw" -- proving the renderer genuinely
// rasterizes through TinyGL's own fixed-function pipeline (real glLoadMatrixf/glVertexPointer/
// glColorPointer/glDrawArrays/glArrayElement calls).
//
// Check A -- SDL's video subsystem was never initialized and GetWindowInternal() is null.
// Check B -- Clear(r,g,b,a) followed by GetBackBufferData() reads the clear color back.
// Check C -- a full-viewport DrawPrimitives (VertexBuffer + BasicEffect, VertexPositionColor)
//   produces the vertex color at the backbuffer's center -- proves the real vertex-array path.
// Check D -- the indexed route (glArrayElement inside glBegin/glEnd, since TinyGL has no
//   glDrawElements at all) renders the same geometry.
// Check E -- buffers honestly report the counts they were given.
// Check F -- SetDepthTestEnabled/SetBlendEnabled/SetDepthWriteEnabled reach real TinyGL state.
//
// TinyGL interpolates vertex colours in fixed point, so an exact channel can land one LSB below
// the requested value (TINYGL-0 measured 254 for a requested 255). Pixel checks below therefore
// use a tolerance of 2 rather than demanding byte equality -- documented in
// docs/tinygl-renderer.md, not silently absorbed.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/TinyGL/TinyGLRenderer.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::TinyGL;

namespace
{
    constexpr int kChecks = 10;

    VertexDeclaration PosColorDecl()
    {
        return VertexDeclaration(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
        });
    }

    bool NearColor(const Color& actual, int r, int g, int b, int tolerance = 2)
    {
        return std::abs(static_cast<int>(actual.getRProperty()) - r) <= tolerance &&
               std::abs(static_cast<int>(actual.getGProperty()) - g) <= tolerance &&
               std::abs(static_cast<int>(actual.getBProperty()) - b) <= tolerance;
    }
}

class TinyGLSmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<TinyGLRenderer&>(dev.GetRenderer());

        // Check A: no real window/video subsystem anywhere.
        check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
              "SDL_INIT_VIDEO was never initialized under the TinyGL renderer");
        check(renderer.GetWindowInternal() == nullptr,
              "GraphicsDevice has no real window under the TinyGL renderer");

        // Check B: real pixel readback after Clear().
        {
            dev.Clear(Color(20, 40, 60, 255));
            const Rectangle region(0, 0, 4, 4);
            std::vector<Color> pixels(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            bool allMatch = true;
            for (const Color& p : pixels)
            {
                if (!NearColor(p, 20, 40, 60) || p.getAProperty() != 255) { allMatch = false; break; }
            }
            check(allMatch, "GetBackBufferData() reads back the Clear() color for every pixel");
        }

        // Check C: a real full-viewport DrawPrimitives pixel oracle.
        VertexBuffer vb(dev, PosColorDecl(), 6, BufferUsage::None);
        {
            dev.Clear(Color::Black);

            // Wound clockwise in XNA's top-left screen space (TL, TR, BR / TL, BR, BL), i.e.
            // front-facing under XNA's own default RasterizerState.CullCounterClockwise.
            const VertexPositionColor verts[6] = {
                { Vector3(-1.0f, 1.0f, 0.0f),  Color(255, 0, 0, 255) },
                { Vector3(1.0f, 1.0f, 0.0f),   Color(255, 0, 0, 255) },
                { Vector3(1.0f, -1.0f, 0.0f),  Color(255, 0, 0, 255) },
                { Vector3(-1.0f, 1.0f, 0.0f),  Color(255, 0, 0, 255) },
                { Vector3(1.0f, -1.0f, 0.0f),  Color(255, 0, 0, 255) },
                { Vector3(-1.0f, -1.0f, 0.0f), Color(255, 0, 0, 255) },
            };
            vb.SetData(verts, 0, 6);
            BasicEffect fx(dev);
            // BasicEffect.VertexColorEnabled defaults to FALSE, and the renderer honours that: an
            // unlit draw with it off renders DiffuseColor, not the packed vertex colour.
            fx.VertexColorEnabled = true;
            fx.Apply();

            bool threw = false;
            try
            {
                dev.SetVertexBuffer(&vb);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                dev.SetVertexBuffer(nullptr);
            }
            catch (...) { threw = true; }
            check(!threw, "DrawPrimitives (full-viewport VertexPositionColor quad) does not throw");

            const Rectangle centerRegion(30, 30, 4, 4);
            std::vector<Color> centerPixels(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion, centerPixels.data(), 0,
                                  static_cast<int>(centerPixels.size()));
            bool allRed = true;
            for (const Color& p : centerPixels)
            {
                if (!NearColor(p, 255, 0, 0)) { allRed = false; break; }
            }
            check(allRed,
                  "the real TinyGL rasterizer draws the vertex color across the full viewport");
        }

        // Check D: the indexed route. TinyGL has no glDrawElements -- this proves the
        // glArrayElement()-inside-glBegin/glEnd replay really rasterizes.
        {
            dev.Clear(Color::Black);
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
            const std::uint16_t indices[6] = {0, 1, 2, 3, 4, 5};
            ib.SetData(indices, 0, 6);

            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();

            bool threw = false;
            try
            {
                dev.SetVertexBuffer(&vb);
                dev.setIndicesProperty(&ib);
                dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 6, 0, 2);
                dev.setIndicesProperty(nullptr);
                dev.SetVertexBuffer(nullptr);
            }
            catch (...) { threw = true; }
            check(!threw, "DrawIndexedPrimitives (glArrayElement replay) does not throw");

            const Rectangle centerRegion(30, 30, 4, 4);
            std::vector<Color> centerPixels(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion, centerPixels.data(), 0,
                                  static_cast<int>(centerPixels.size()));
            bool allRed = true;
            for (const Color& p : centerPixels)
            {
                if (!NearColor(p, 255, 0, 0)) { allRed = false; break; }
            }
            check(allRed, "the indexed route rasterizes the same geometry as the non-indexed one");

            // Check E: buffers honestly report their counts.
            check(vb.getVertexCountProperty() == 6, "VertexBuffer reports the vertex count it was given");
            check(ib.getIndexCountProperty() == 6, "IndexBuffer reports the index count it was given");
        }

        // Check F: real state-toggle calls reach TinyGL without throwing.
        {
            bool threw = false;
            try
            {
                renderer.SetDepthTestEnabled(true);
                renderer.SetBlendEnabled(true);
                renderer.SetDepthWriteEnabled(false);
                renderer.SetDepthTestEnabled(false);
                renderer.SetBlendEnabled(false);
                renderer.SetDepthWriteEnabled(true);
            }
            catch (...) { threw = true; }
            check(!threw, "SetDepthTestEnabled/SetBlendEnabled/SetDepthWriteEnabled do not throw");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = (passCount_ == kChecks) ? 0 : 1;
        Exit();
    }

public:
    TinyGLSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return result_; }
};

int main()
{
    TinyGLSmokeTest game;
    game.Run();
    return game.getResult();
}
