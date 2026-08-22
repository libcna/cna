// SPDX-License-Identifier: MS-PL
// BasicEffect-parameter regression test for the PortableGL renderer.
//
// BasicEffect.VertexColorEnabled defaults to FALSE. PortableGL used to consume the packed vertex
// colour unconditionally, so an effect-configured draw rendered something the effect never asked
// for -- and the renderer's own tests hid it by leaving VertexColorEnabled at its default while
// expecting vertex colours. The unlit stock formula every other CNA renderer implements is
//
//     FragColor = (VertexColorEnabled ? vertexColour : white) * DiffuseColor
//
// where DiffuseColor is (BasicEffect.DiffuseColor + EmissiveColor) * Alpha with Alpha in the
// fourth component. This file pins every term of it, and pins that the configurations PortableGL
// does NOT implement are refused rather than approximated.
//
// Check A -- VertexColorEnabled = false renders DiffuseColor, not the packed vertex colour.
// Check B -- VertexColorEnabled = true renders the packed vertex colour.
// Check C -- a non-white DiffuseColor is applied with vertex colour off.
// Check D -- a non-white DiffuseColor MULTIPLIES the vertex colour with it on.
// Check E -- Alpha scales the emitted RGB and the emitted alpha (XNA's premultiplied convention).
// Check F -- Alpha really reaches the output merger: the same draw under BlendState.AlphaBlend
//   composites against the destination by exactly that alpha.
// Check G -- BasicEffect configurations PortableGL does not implement (lighting, texturing) are
//   refused deterministically, and an ordinary draw still works afterwards.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "portablegl_test_support.hpp"

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
#include <exception>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace portablegl_test;

namespace
{
    constexpr int kSize = 32;
}

class PortableGLEffectStateTest : public PortableGLTestGame
{
    std::unique_ptr<Texture2D> texture_;

    Rectangle Whole() const { return Rectangle(0, 0, kSize, kSize); }

    /// Draws one full-viewport quad of @p vertexColor through @p fx.
    void DrawQuad(BasicEffect& fx, const Color& vertexColor)
    {
        auto& dev = getGraphicsDeviceProperty();
        const auto verts = FullQuad(vertexColor);
        VertexBuffer vb(dev, PosColorDecl(), 6, BufferUsage::None);
        vb.SetData(verts.data(), 0, 6);
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

protected:
    void LoadContent() override
    {
        const std::vector<std::uint8_t> pixels = {255, 255, 255, 255};
        texture_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1, pixels));
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        dev.setBlendStateProperty(BlendState::Opaque);

        // Check A -- the default (false) really means "ignore the vertex colour".
        {
            dev.Clear(Color::Black);
            BasicEffect fx(dev);   // VertexColorEnabled defaults to false
            DrawQuad(fx, Color(255, 0, 0, 255));
            check(RegionIs(Whole(), Color(255, 255, 255, 255)),
                  "check A: VertexColorEnabled = false renders DiffuseColor, not the vertex colour");
        }

        // Check B -- and true really means "consume it".
        {
            dev.Clear(Color::Black);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            DrawQuad(fx, Color(200, 30, 90, 255));
            check(RegionIs(Whole(), Color(200, 30, 90, 255)),
                  "check B: VertexColorEnabled = true renders the packed vertex colour exactly");
        }

        // Check C -- DiffuseColor with the vertex colour off. 0.2/0.6/0.8 are exactly 51/153/204
        // eighths-of-a-byte apart, so the expectation is a byte-exact one, not a rounded guess.
        {
            dev.Clear(Color::Black);
            BasicEffect fx(dev);
            fx.setDiffuseColorProperty(Vector3(0.2f, 0.6f, 0.8f));
            DrawQuad(fx, Color(255, 0, 0, 255));
            check(RegionIs(Whole(), Color(51, 153, 204, 255)),
                  "check C: DiffuseColor is applied when VertexColorEnabled is false");
        }

        // Check D -- and multiplies the vertex colour when it is on. A white vertex colour would
        // not distinguish "multiplied" from "replaced", so the vertex colour is half-intensity in
        // the green channel: 0.6 * (128/255) = 0.30117..., i.e. 77 after round-to-nearest.
        {
            dev.Clear(Color::Black);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.setDiffuseColorProperty(Vector3(0.2f, 0.6f, 0.8f));
            DrawQuad(fx, Color(255, 128, 0, 255));
            check(RegionIs(Whole(), Color(51, 77, 0, 255)),
                  "check D: DiffuseColor multiplies the vertex colour, it does not replace it");
        }

        // Check E -- Alpha scales RGB and alpha alike (XNA forwards DiffuseColor * Alpha with
        // Alpha in the fourth component). Under Opaque the fragment reaches the framebuffer
        // unblended, so both are directly readable. 0.5 * 255 = 127.5 -> 128 rounded to nearest.
        {
            dev.Clear(Color(0, 0, 0, 0));
            BasicEffect fx(dev);
            fx.setAlphaProperty(0.5f);
            DrawQuad(fx, Color(255, 255, 255, 255));
            check(RegionIsRgba(Whole(), Color(128, 128, 128, 128)),
                  "check E: BasicEffect.Alpha scales both the emitted RGB and the emitted alpha");
        }

        // Check F -- the same alpha, now driving a real blend against a known destination.
        // BasicEffect already premultiplies its colour by Alpha, so under BlendState.AlphaBlend
        // (One, InverseSourceAlpha) a red source at Alpha 0.5 over an opaque blue destination
        // yields red = 128/255 exactly and blue = 1 - 128/255 = 127/255. The destination term is
        // the one PortableGL truncates rather than rounds (its documented float -> byte
        // conversion), so it is checked to within one LSB while the source term is checked exactly.
        {
            dev.Clear(Color(0, 0, 255, 255));
            dev.setBlendStateProperty(BlendState::AlphaBlend);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.setAlphaProperty(0.5f);
            DrawQuad(fx, Color(255, 0, 0, 255));
            const Color blended = PixelAt(kSize / 2, kSize / 2);
            check(blended.getRProperty() == 128 && blended.getGProperty() == 0,
                  "check F: BasicEffect.Alpha reaches the output merger as the blend's source term");
            check(blended.getBProperty() >= 126 && blended.getBProperty() <= 127,
                  "check F: the destination survives by exactly the complementary alpha");
            dev.setBlendStateProperty(BlendState::Opaque);
        }

        // Check G -- unsupported configurations are refused, not approximated.
        {
            dev.Clear(Color::Black);
            bool lightingThrew = false;
            try
            {
                BasicEffect fx(dev);
                fx.setLightingEnabledProperty(true);
                DrawQuad(fx, Color(255, 0, 0, 255));
            }
            catch (const std::exception&) { lightingThrew = true; }
            check(lightingThrew, "check G: a lit BasicEffect draw is refused deterministically");

            bool textureThrew = false;
            try
            {
                BasicEffect fx(dev);
                fx.setTextureEnabledProperty(true);
                fx.setTextureProperty(texture_.get());
                DrawQuad(fx, Color(255, 0, 0, 255));
            }
            catch (const std::exception&) { textureThrew = true; }
            check(textureThrew,
                  "check G: textured BasicEffect with VertexPositionColor is refused rather than "
                  "reinterpreting color bytes as texture coordinates");

            check(RegionIs(Whole(), Color::Black),
                  "check G: neither refused draw wrote a single pixel");

            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            DrawQuad(fx, Color(0, 0, 255, 255));
            check(RegionIs(Whole(), Color(0, 0, 255, 255)),
                  "check G: an ordinary draw still works after both refusals");
        }

        Finish();
    }

public:
    PortableGLEffectStateTest() : PortableGLTestGame(kSize) {}
};

int main()
{
    PortableGLEffectStateTest game;
    game.Run();
    return game.getResult();
}
