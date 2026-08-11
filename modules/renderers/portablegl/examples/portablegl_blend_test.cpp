// SPDX-License-Identifier: MS-PL
// BlendState regression test for the PortableGL renderer.
//
// PortableGL used to hard-code glBlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA) at construction and
// never implement ApplyBlendState at all, so BlendState.AlphaBlend (which is PREMULTIPLIED --
// One / InverseSourceAlpha) and BlendState.NonPremultiplied (SourceAlpha / InverseSourceAlpha)
// produced identical pixels, Additive composited wrongly, BlendFactor did nothing and
// ColorWriteChannels was ignored. It also biased every fragment by half an LSB before blending,
// which made a fully transparent source visibly darken its destination.
//
// The oracles below are chosen so that the discriminating channel is EXACT: the source factors of
// AlphaBlend and NonPremultiplied differ by the source alpha itself, so over a black destination
// the red channel is 255 for one and 128 for the other with no rounding involved. Where a
// destination term is genuinely part of the answer, PortableGL truncates the blended float result
// (its documented v4_to_Color conversion) and the check allows exactly one LSB, never more.
//
// Check A -- BlendState.Opaque replaces the destination regardless of source alpha.
// Check B -- AlphaBlend and NonPremultiplied are genuinely different blends.
// Check C -- Additive accumulates onto the destination instead of replacing it.
// Check D -- alpha 0 leaves the destination bit-for-bit unchanged (the rounding-bias oracle).
// Check E -- partial alpha composites, and repeated translucent overdraw accumulates monotonically.
// Check F -- BlendState.BlendFactor drives the constant-colour blend factors.
// Check G -- ColorWriteChannels masks the channels it names, and Clear ignores the mask.
// Check H -- state transitions: AlphaBlend -> Opaque -> AlphaBlend each take effect.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "portablegl_test_support.hpp"

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <exception>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace portablegl_test;

namespace
{
    constexpr int kSize = 32;
}

class PortableGLBlendTest : public PortableGLTestGame
{
    Rectangle Whole() const { return Rectangle(0, 0, kSize, kSize); }

    /// Draws one full-viewport quad of @p vertexColor through the vertex-coloured unlit route.
    void DrawQuad(const Color& vertexColor)
    {
        auto& dev = getGraphicsDeviceProperty();
        const auto verts = FullQuad(vertexColor);
        VertexBuffer vb(dev, PosColorDecl(), 6, BufferUsage::None);
        vb.SetData(verts.data(), 0, 6);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);

        // Check A -- Opaque is (One, Zero): the source wins whole, alpha and all.
        {
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.Clear(Color(40, 80, 120, 255));
            DrawQuad(Color(200, 30, 90, 64));
            check(RegionIsRgba(Whole(), Color(200, 30, 90, 64)),
                  "check A: BlendState.Opaque writes the source verbatim, ignoring its alpha");
        }

        // Check B -- the discriminator. AlphaBlend's source factor is One (the colour is assumed
        // premultiplied) while NonPremultiplied's is SourceAlpha, so over black an alpha-128 red
        // source lands on 255 for the first and 128 for the second. A renderer that used
        // SRC_ALPHA for both -- which is exactly what the fixed glBlendFunc did -- cannot pass
        // both halves.
        {
            dev.setBlendStateProperty(BlendState::AlphaBlend);
            dev.Clear(Color(0, 0, 0, 255));
            DrawQuad(Color(255, 0, 0, 128));
            check(RegionIs(Whole(), Color(255, 0, 0, 255)),
                  "check B: AlphaBlend takes the source colour whole (premultiplied convention)");

            dev.setBlendStateProperty(BlendState::NonPremultiplied);
            dev.Clear(Color(0, 0, 0, 255));
            DrawQuad(Color(255, 0, 0, 128));
            check(RegionIs(Whole(), Color(128, 0, 0, 255)),
                  "check B: NonPremultiplied scales the source colour by its own alpha");
        }

        // Check C -- Additive is (SourceAlpha, One): the destination survives undiminished.
        {
            dev.setBlendStateProperty(BlendState::Additive);
            dev.Clear(Color(0, 0, 60, 255));
            DrawQuad(Color(255, 0, 0, 128));
            const Color result = PixelAt(kSize / 2, kSize / 2);
            check(result.getRProperty() == 128,
                  "check C: Additive scales the source by its alpha");
            check(result.getBProperty() == 60,
                  "check C: Additive keeps the destination at full strength (factor One)");
        }

        // Check D -- the rounding-bias oracle. A fully transparent source must leave the
        // destination BIT-FOR-BIT unchanged under both alpha blends: AlphaBlend multiplies the
        // destination by 1 - 0 and NonPremultiplied multiplies the source by 0. The half-LSB
        // fragment bias this renderer used to add made both of these fail.
        {
            const Color backdrop(200, 30, 90, 255);
            // AlphaBlend is premultiplied, so a fully transparent source is (0,0,0,0): the source
            // term contributes nothing and the destination term is multiplied by 1 - 0.
            dev.setBlendStateProperty(BlendState::AlphaBlend);
            dev.Clear(backdrop);
            DrawQuad(Color(0, 0, 0, 0));
            check(RegionIsRgba(Whole(), backdrop),
                  "check D: an alpha-0 source under AlphaBlend leaves the destination untouched");

            // NonPremultiplied multiplies the source colour by its own alpha, so a transparent
            // source is any colour at alpha 0 -- white here, the least forgiving choice.
            dev.setBlendStateProperty(BlendState::NonPremultiplied);
            dev.Clear(backdrop);
            DrawQuad(Color(255, 255, 255, 0));
            check(RegionIsRgba(Whole(), backdrop),
                  "check D: an alpha-0 source under NonPremultiplied leaves the destination untouched");

            // Five times over, because a bias would accumulate where a correct no-op does not.
            dev.Clear(backdrop);
            for (int i = 0; i < 5; ++i)
                DrawQuad(Color(255, 255, 255, 0));
            check(RegionIsRgba(Whole(), backdrop),
                  "check D: repeated alpha-0 overdraw still leaves the destination untouched");
        }

        // Check E -- partial alpha composites, and a second identical pass moves further toward
        // the source. Under NonPremultiplied over black, one pass of white at alpha 128 gives
        // 128/255 exactly; a second gives 128/255 + (128/255)(1 - 128/255) = 0.75195..., which
        // PortableGL's truncating conversion writes as 191 (a rounding device would write 192).
        {
            dev.setBlendStateProperty(BlendState::NonPremultiplied);
            dev.Clear(Color(0, 0, 0, 255));
            DrawQuad(Color(255, 255, 255, 128));
            check(RegionIs(Whole(), Color(128, 128, 128, 255)),
                  "check E: a partial-alpha source composites against the destination");

            DrawQuad(Color(255, 255, 255, 128));
            // Exact, not RegionNear: blending runs in full float precision (confirmed against
            // PortableGL's own blend_pixel -- the fragment's quantized output is blended, then
            // truncated to bytes exactly once, at the very end), so 0.751925... * 255 = 191.74...
            // truncates to precisely 191 with no float-noise margin anywhere near the 191/192
            // boundary. A tolerant check here would accept 192 too, silently passing a renderer
            // that rounds instead of truncates -- exactly the distinction this check exists to catch.
            check(RegionIs(Whole(), Color(191, 191, 191, 255)),
                  "check E: a second translucent pass accumulates on top of the first (exact truncation)");
        }

        // Check F -- BlendFactor. (BlendFactor, Zero) makes the result the source scaled by the
        // constant colour alone, so each channel reads back as that constant.
        {
            BlendState constantScale;
            constantScale.setColorSourceBlendProperty(Blend::BlendFactor);
            constantScale.setAlphaSourceBlendProperty(Blend::One);
            constantScale.setColorDestinationBlendProperty(Blend::Zero);
            constantScale.setAlphaDestinationBlendProperty(Blend::Zero);
            constantScale.setBlendFactorProperty(Color(64, 128, 192, 255));
            dev.setBlendStateProperty(constantScale);
            dev.Clear(Color(0, 0, 0, 255));
            DrawQuad(Color(255, 255, 255, 255));
            check(RegionIs(Whole(), Color(64, 128, 192, 255)),
                  "check F: BlendState.BlendFactor drives the constant-colour blend factor");

            // And GraphicsDevice.BlendFactor set on its own, after the state, must win.
            dev.setBlendFactorProperty(Color(255, 0, 0, 255));
            dev.Clear(Color(0, 0, 0, 255));
            DrawQuad(Color(255, 255, 255, 255));
            check(RegionIs(Whole(), Color(255, 0, 0, 255)),
                  "check F: GraphicsDevice.BlendFactor takes effect on its own");
        }

        // Check G -- ColorWriteChannels. Green is masked out, so the destination's green survives
        // a draw that would otherwise overwrite it; and Clear ignores the mask entirely (XNA's
        // Clear is not subject to the write mask, while a raw glClear would be).
        {
            BlendState masked;   // Opaque factors by default
            masked.setColorWriteChannelsProperty(ColorWriteChannels::Red | ColorWriteChannels::Blue);
            dev.setBlendStateProperty(masked);
            dev.Clear(Color(10, 20, 30, 255));
            DrawQuad(Color(255, 255, 255, 255));
            check(RegionIs(Whole(), Color(255, 20, 255, 255)),
                  "check G: ColorWriteChannels masks exactly the channels it excludes");

            dev.Clear(Color(1, 2, 3, 255));
            check(RegionIs(Whole(), Color(1, 2, 3, 255)),
                  "check G: Clear ignores ColorWriteChannels, as XNA's Clear does");

            dev.setBlendStateProperty(BlendState::Opaque);
            dev.Clear(Color(10, 20, 30, 255));
            DrawQuad(Color(255, 255, 255, 255));
            check(RegionIs(Whole(), Color(255, 255, 255, 255)),
                  "check G: restoring the default mask restores full-channel writes");
        }

        // Check H -- transitions, not just isolated initial states.
        {
            dev.setBlendStateProperty(BlendState::AlphaBlend);
            dev.Clear(Color(0, 0, 0, 255));
            DrawQuad(Color(255, 0, 0, 128));
            check(RegionIs(Whole(), Color(255, 0, 0, 255)), "check H: AlphaBlend applies");

            dev.setBlendStateProperty(BlendState::Opaque);
            dev.Clear(Color(0, 0, 0, 255));
            DrawQuad(Color(255, 0, 0, 128));
            check(RegionIsRgba(Whole(), Color(255, 0, 0, 128)),
                  "check H: switching to Opaque really disables the previous blend");

            dev.setBlendStateProperty(BlendState::AlphaBlend);
            dev.Clear(Color(0, 0, 0, 255));
            DrawQuad(Color(0, 255, 0, 64));
            check(RegionIs(Whole(), Color(0, 255, 0, 255)),
                  "check H: switching back to AlphaBlend re-enables it with the right factors");
        }

        Finish();
    }

public:
    PortableGLBlendTest() : PortableGLTestGame(kSize) {}
};

int main()
{
    PortableGLBlendTest game;
    game.Run();
    return game.getResult();
}
