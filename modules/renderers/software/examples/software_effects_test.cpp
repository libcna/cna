// SPDX-License-Identifier: MS-PL
// plans/plan_software.md Phase S5/S6 (SOFTWARE-40..43, 50/51): pixel shading and effect/SpriteBatch
// integration -- texture sampling, diffuseColor modulation, Opaque/AlphaBlend, and a real,
// pixel-correct SpriteBatch path, all going through the normal GraphicsDevice public API.
//
// Check A -- nearest-neighbor texture sampling: a full-screen quad textured with a 2x2 checker
//   (Red/Green/Blue/White) samples the correct texel near each of two opposite corners.
// Check B -- BasicEffect.DiffuseColor modulates the sampled texture color (a white texture tinted
//   by DiffuseColor=(0.5,0,0) renders as ~half-intensity red).
// Check C -- BlendState::Opaque ignores source alpha entirely: a half-alpha red quad drawn over a
//   solid blue background renders as pure opaque red.
// Check D -- BlendState::AlphaBlend actually blends: the same half-alpha red quad over the same
//   blue background renders as a real mix of red and blue, not pure red.
// Check E -- SpriteBatch::Draw() (going through ISpriteBatchRenderer, not DrawPrimitivesEx)
//   renders a solid-color 1x1 texture at the exact requested screen position.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    int g_passCount = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++g_passCount;
    }

    bool Close(int a, int b, int tolerance) { return std::abs(a - b) <= tolerance; }

    /// REMED-GFX-150: point sampling returns a STORED texel, so its probes are byte-exact rather
    /// than tolerant -- a tolerance is exactly what let a blended value pass for a selected one.
    bool Exactly(const Color& c, int r, int g, int b)
    {
        return c.getRProperty() == r && c.getGProperty() == g && c.getBProperty() == b;
    }
}

class SoftwareEffectsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 1;

    Color ReadPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        // These checks' quads were authored for pixel-correctness, not to match XNA's winding
        // convention, and are back-facing under the real default (CullCounterClockwise) --
        // disable culling (SOFTWARE-81) so this file keeps testing what it was designed to test.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        // Check A: nearest-neighbor texture sampling via a 2x2 checker texture on a full-screen quad.
        {
            dev.Clear(Color::Black, 1.0f);
            const std::vector<std::uint8_t> checkerPixels = {
                255, 0, 0, 255,    0, 255, 0, 255,     // row 0: Red, Green
                0, 0, 255, 255,    255, 255, 255, 255, // row 1: Blue, White
            };
            Texture2D checker = Texture2D::CreateFromPixels(dev, 2, 2, checkerPixels);
            // REMED-GFX-150: this check is titled "nearest-neighbor texture sampling", so it must
            // SELECT nearest-neighbor sampling. It used to leave SamplerState implicit, which means
            // the device default -- SamplerState.LinearWrap -- and passed only because the renderer
            // discarded the filter and the address mode alike. Two of the three probes below are
            // additionally in the texture's corner cells, where clamping collapses both bilinear
            // endpoints onto one texel, so the original pair could not have seen the defect even
            // with an explicit sampler.
            dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;

            const VertexPositionTexture verts[6] = {
                { Vector3(-1.0f,  1.0f, 0.5f), Vector2(0.0f, 0.0f) }, // TL
                { Vector3(-1.0f, -1.0f, 0.5f), Vector2(0.0f, 1.0f) }, // BL
                { Vector3( 1.0f,  1.0f, 0.5f), Vector2(1.0f, 0.0f) }, // TR
                { Vector3( 1.0f,  1.0f, 0.5f), Vector2(1.0f, 0.0f) }, // TR
                { Vector3(-1.0f, -1.0f, 0.5f), Vector2(0.0f, 1.0f) }, // BL
                { Vector3( 1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f) }, // BR
            };
            VertexBuffer vb(dev, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(verts, 6);
            BasicEffect fx(dev);
            fx.setTextureProperty(&checker);
            fx.setTextureEnabledProperty(true);
            fx.Apply();
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);

            const Color nearTopLeft = ReadPixel(dev, 2, 2);
            const Color nearBottomRight = ReadPixel(dev, 61, 61);
            Check(Exactly(nearTopLeft, 255, 0, 0) && Exactly(nearBottomRight, 255, 255, 255),
                  "textured quad samples the correct texel near opposite corners (top-left=Red, bottom-right=White)");

            // REMED-GFX-150: the probe that can actually fail. Pixel (31,31) is the last pixel of
            // the texture's top-left cell, so point sampling owes exactly Red -- while bilinear
            // filtering there is a near-even blend of all four texels, which is what this renderer
            // used to return whatever SamplerState was selected. (2,2) and (61,61) above sit where
            // clamping collapses the interpolation, so they agree either way.
            const Color interior = ReadPixel(dev, 31, 31);
            Check(Exactly(interior, 255, 0, 0),
                  "PointClamp selects ONE texel away from the clamped corners: (31,31) is Red, not a "
                  "blend of the four checker texels");

            // The differential, so "Red" above cannot be satisfied by a renderer that ignores the
            // sampler and happens to clamp: with LinearClamp the SAME pixel must NOT be pure Red.
            dev.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
            fx.Apply();
            dev.Clear(Color::Black, 1.0f);
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            const Color interiorLinear = ReadPixel(dev, 31, 31);
            Check(!Exactly(interiorLinear, 255, 0, 0),
                  "LinearClamp still interpolates at that same pixel, so the check above is measuring "
                  "the filter rather than a coincidence");
            dev.getSamplerStatesProperty()[0] = SamplerState::LinearWrap;
        }

        // Check B: DiffuseColor modulates the sampled texture color.
        {
            dev.Clear(Color::Black, 1.0f);
            const std::vector<std::uint8_t> whitePixel = {255, 255, 255, 255};
            Texture2D white = Texture2D::CreateFromPixels(dev, 1, 1, whitePixel);

            const VertexPositionTexture verts[6] = {
                { Vector3(-1.0f,  1.0f, 0.5f), Vector2(0.0f, 0.0f) },
                { Vector3(-1.0f, -1.0f, 0.5f), Vector2(0.0f, 1.0f) },
                { Vector3( 1.0f,  1.0f, 0.5f), Vector2(1.0f, 0.0f) },
                { Vector3( 1.0f,  1.0f, 0.5f), Vector2(1.0f, 0.0f) },
                { Vector3(-1.0f, -1.0f, 0.5f), Vector2(0.0f, 1.0f) },
                { Vector3( 1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f) },
            };
            VertexBuffer vb(dev, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(verts, 6);
            BasicEffect fx(dev);
            fx.setTextureProperty(&white);
            fx.setTextureEnabledProperty(true);
            fx.setDiffuseColorProperty(Vector3(0.5f, 0.0f, 0.0f));
            fx.setAlphaProperty(1.0f);
            fx.Apply();
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);

            const Color center = ReadPixel(dev, 32, 32);
            Check(Close(center.getRProperty(), 127, 10) && Close(center.getGProperty(), 0, 5) &&
                  Close(center.getBProperty(), 0, 5),
                  "BasicEffect.DiffuseColor=(0.5,0,0) modulates a white texture to half-intensity red");
        }

        // Checks C/D: Opaque ignores source alpha; AlphaBlend consumes premultiplied colour.
        {
            const VertexPositionColor halfAlphaRed[6] = {
                { Vector3(-1.0f,  1.0f, 0.5f), Color(255, 0, 0, 128) },
                { Vector3(-1.0f, -1.0f, 0.5f), Color(255, 0, 0, 128) },
                { Vector3( 1.0f,  1.0f, 0.5f), Color(255, 0, 0, 128) },
                { Vector3( 1.0f,  1.0f, 0.5f), Color(255, 0, 0, 128) },
                { Vector3(-1.0f, -1.0f, 0.5f), Color(255, 0, 0, 128) },
                { Vector3( 1.0f, -1.0f, 0.5f), Color(255, 0, 0, 128) },
            };
            const VertexPositionColor premultipliedHalfAlphaRed[6] = {
                { Vector3(-1.0f,  1.0f, 0.5f), Color(128, 0, 0, 128) },
                { Vector3(-1.0f, -1.0f, 0.5f), Color(128, 0, 0, 128) },
                { Vector3( 1.0f,  1.0f, 0.5f), Color(128, 0, 0, 128) },
                { Vector3( 1.0f,  1.0f, 0.5f), Color(128, 0, 0, 128) },
                { Vector3(-1.0f, -1.0f, 0.5f), Color(128, 0, 0, 128) },
                { Vector3( 1.0f, -1.0f, 0.5f), Color(128, 0, 0, 128) },
            };
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();

            // Check C: Opaque.
            dev.Clear(Color::Blue, 1.0f);
            {
                VertexBuffer vb(dev, 6);
                vb.SetData(halfAlphaRed, 6);
                dev.setBlendStateProperty(BlendState::Opaque);
                dev.SetVertexBuffer(&vb);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                dev.SetVertexBuffer(nullptr);
            }
            const Color opaqueResult = ReadPixel(dev, 32, 32);
            Check(Close(opaqueResult.getRProperty(), 255, 5) && Close(opaqueResult.getGProperty(), 0, 5) &&
                  Close(opaqueResult.getBProperty(), 0, 5),
                  "BlendState::Opaque ignores source alpha -- a half-alpha red quad renders fully opaque red");

            // Check D: AlphaBlend.
            dev.Clear(Color::Blue, 1.0f);
            {
                VertexBuffer vb(dev, 6);
                vb.SetData(premultipliedHalfAlphaRed, 6);
                dev.setBlendStateProperty(BlendState::AlphaBlend);
                dev.SetVertexBuffer(&vb);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                dev.SetVertexBuffer(nullptr);
            }
            const Color blendedResult = ReadPixel(dev, 32, 32);
            std::printf("[INFO] AlphaBlend exact pixel (%d,%d,%d,%d)\n",
                        blendedResult.getRProperty(), blendedResult.getGProperty(),
                        blendedResult.getBProperty(), blendedResult.getAProperty());
            // AlphaBlend is One/InverseSourceAlpha for both colour and alpha. The source colour is
            // therefore premultiplied exactly once before submission, and the byte result is exact.
            Check(blendedResult.getRProperty() == 128 && blendedResult.getGProperty() == 0 &&
                  blendedResult.getBProperty() == 126 && blendedResult.getAProperty() == 255,
                  "BlendState::AlphaBlend applies premultiplied source + inverse-alpha destination exactly");
            dev.setBlendStateProperty(BlendState::Opaque);
        }

        // Check E: SpriteBatch renders a solid-color texture at the exact requested position.
        {
            dev.Clear(Color::Black, 1.0f);
            const std::vector<std::uint8_t> solidYellow = {255, 255, 0, 255};
            Texture2D yellow = Texture2D::CreateFromPixels(dev, 1, 1, solidYellow);

            SpriteBatch sb(dev);
            sb.Begin();
            sb.Draw(yellow, Vector2(10.0f, 20.0f), Color::White);
            sb.End();

            const Color spritePixel = ReadPixel(dev, 10, 20);
            Check(Close(spritePixel.getRProperty(), 255, 5) && Close(spritePixel.getGProperty(), 255, 5) &&
                  Close(spritePixel.getBProperty(), 0, 5),
                  "SpriteBatch::Draw() renders a solid-color texture at the exact requested screen position");
        }

        // REMED-GFX-150 added two checks to Check A: the interior point probe that can
        // actually fail, and its LinearClamp differential.
        std::printf("=== %d/%d PASS ===\n", g_passCount, 7);
        result_ = (g_passCount == 7) ? 0 : 1;
        Exit();
    }

public:
    SoftwareEffectsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    SoftwareEffectsTest game;
    game.Run();
    return game.getResult();
}
