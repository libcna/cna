// SPDX-License-Identifier: MS-PL
// End-to-end smoke test for the PortableGL (rswinkle/PortableGL, CPU software OpenGL 3.x-ish)
// graphics renderer. Constructs a real Game, runs a few frames entirely under
// CNA_GRAPHICS_RENDERER=PORTABLEGL, and asserts real, byte-exact pixel results -- not just "did
// not throw" -- proving the renderer genuinely rasterizes through PortableGL's own GPU-shaped
// pipeline (real glBufferData/glVertexAttribPointer/pglCreateProgram/glDrawArrays calls).
//
// Check A -- SDL's video subsystem was never initialized and GameWindow handle is null -- this
//   renderer needs no display server or window, matching HEADLESS/SOFTWARE/STUB.
// Check B -- Clear(r,g,b,a) followed by GetBackBufferData() reads back the exact clear color for
//   every pixel in the requested region -- a real backbuffer, not a fiction.
// Check C -- a full-viewport DrawPrimitives (VertexBuffer + BasicEffect, VertexPositionColor,
//   2 triangles tiling NDC -1..1) produces the exact vertex color at the backbuffer's center
//   pixel -- proves the real PortableGL vertex/fragment shader pair and glDrawArrays path.
// Check D -- CreateVertexBuffer()/CreateIndexBuffer16() honestly report back the vertex/index
//   count they were given.
// Check E -- SetDepthTestEnabled/SetBlendEnabled/SetDepthWriteEnabled (real glEnable/glDisable/
//   glDepthMask calls) do not throw.
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

#include "CNA/Internal/Renderers/PortableGL/PortableGLRenderer.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::PortableGL;

namespace
{
    VertexDeclaration PosColorDecl()
    {
        return VertexDeclaration(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
        });
    }
}

class PortableGLSmokeTest : public Game
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
        auto& renderer = static_cast<PortableGLRenderer&>(dev.GetRenderer());

        // Check A: no real window/video subsystem anywhere.
        check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
              "SDL_INIT_VIDEO was never initialized under the PortableGL renderer");
        check(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty()) == nullptr,
              "GraphicsDevice has no real window under the PortableGL renderer");

        // Check B: real, correct pixel readback after Clear().
        {
            dev.Clear(Color(20, 40, 60, 255));
            const Rectangle region(0, 0, 4, 4);
            std::vector<Color> pixels(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            bool allMatch = true;
            for (const Color& p : pixels)
            {
                if (p.getRProperty() != 20 || p.getGProperty() != 40 || p.getBProperty() != 60 ||
                    p.getAProperty() != 255)
                {
                    allMatch = false;
                    break;
                }
            }
            check(allMatch, "GetBackBufferData() reads back the exact Clear() color for every pixel");
        }

        // Check C: a real full-viewport DrawPrimitives pixel oracle.
        {
            dev.Clear(Color::Black);

            VertexBuffer vb(dev, PosColorDecl(), 6, BufferUsage::None);
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
            // unlit draw with it off renders DiffuseColor, not the packed vertex colour. A test
            // that wants the vertex colour has to ask for it, exactly as a real game does.
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
            dev.GetBackBufferData(&centerRegion, centerPixels.data(), 0, static_cast<int>(centerPixels.size()));
            bool allRed = true;
            for (const Color& p : centerPixels)
            {
                if (p.getRProperty() != 255 || p.getGProperty() != 0 || p.getBProperty() != 0)
                {
                    allRed = false;
                    break;
                }
            }
            check(allRed,
                  "the real PortableGL rasterizer draws the exact vertex color across the full viewport");

            // Check D: the vertex buffer honestly reports back its vertex count.
            check(vb.getVertexCountProperty() == 6, "VertexBuffer reports the vertex count it was given");

            bool indexThrew = false;
            try
            {
                IndexBuffer ib(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
                const std::uint16_t indices[3] = {0, 1, 2};
                ib.SetData(indices, 0, 3);
                check(ib.getIndexCountProperty() == 3, "IndexBuffer reports the index count it was given");
            }
            catch (...) { indexThrew = true; }
            check(!indexThrew, "IndexBuffer round-trips real data without throwing");
        }

        // Check E: real state-toggle calls (glEnable/glDisable/glDepthMask) do not throw.
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

        std::printf("=== %d/%d PASS ===\n", passCount_, 9);
        result_ = (passCount_ == 9) ? 0 : 1;
        Exit();
    }

public:
    PortableGLSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    PortableGLSmokeTest game;
    game.Run();
    return game.getResult();
}
