// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-173 (harness: WEBGPU-207): every term `BasicEffect` adds to a lit
// surface, each proven to change the picture in the direction it is supposed to.
//
// XNA's lit colour is
//
//     DiffuseColor * (AmbientLightColor + sum over lights of LightDiffuse * max(0, N.L))
//         + EmissiveColor + specular
//
// so each term enters at a different place, and a renderer can drop any one of them while still
// producing a plausible lit surface. The columns below turn on exactly one term each, against a
// shared baseline, so a dropped term is a column that failed to brighten rather than a frame that
// merely looks wrong somewhere:
//
//   0  baseline -- one directional light, nothing else
//   1  + EmissiveColor        (added AFTER the light sum, so it survives even an unlit surface)
//   2  + AmbientLightColor    (inside the sum, multiplied by DiffuseColor)
//   3  three lights, not one  (the sum is a sum, not "the first light")
//   4  + specular             (a separate term needing the eye vector, not just N.L)
//
// EVERY COLUMN USES A COLOUR NO OTHER COLUMN USES for its added term, so a renderer that applied
// the right magnitude through the wrong term still fails: emissive is red, ambient is green,
// the extra lights are blue, specular is white. A brightness check alone could not tell those
// apart, so each column asserts its own channel moved and the baseline's did not.
//
// The geometry is deliberately flat and face-on: N.L is exactly 1 for the primary light, so the
// baseline is an exact product rather than something that depends on interpolation. `DiffuseColor`
// is well below 1 to leave headroom -- a term that pushed a saturated surface would be invisible.

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <array>
#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kColumns = 5;
    constexpr int kCell = 64;
    constexpr int kWidth = kColumns * kCell;
    constexpr int kHeight = 64;

    const Color kClearColor(9, 13, 17, 255);

    /// Which term a column adds, and which channel that term is the only source of.
    enum class Term { Baseline, Emissive, Ambient, ThreeLights, Specular };
}

/// WEBGPU-173: each of BasicEffect's lighting terms reaches the surface, through its own channel.
class BasicEffectLightTermsParityFixture : public CNA::Parity::ParityFixture
{
public:
    BasicEffectLightTermsParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, 1};

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(kClearColor);

        const auto drawColumn = [&](int column, Term term)
        {
            BasicEffect effect(device);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::getIdentityProperty());
            effect.setTextureEnabledProperty(false);
            effect.setVertexColorEnabledProperty(false);
            effect.setLightingEnabledProperty(true);
            effect.setPreferPerPixelLightingProperty(false);

            // The shared baseline every column starts from: mid grey lit by one white light
            // straight on. Well below 1 so every added term has room to show.
            effect.setDiffuseColorProperty(Vector3(0.35f, 0.35f, 0.35f));
            effect.setAmbientLightColorProperty(Vector3::Zero);
            effect.setEmissiveColorProperty(Vector3::Zero);
            effect.setSpecularColorProperty(Vector3::Zero);
            effect.setSpecularPowerProperty(16.0f);
            effect.DirectionalLight0.setEnabledProperty(true);
            effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
            effect.DirectionalLight0.setDiffuseColorProperty(Vector3::One);
            effect.DirectionalLight0.setSpecularColorProperty(Vector3::Zero);
            effect.DirectionalLight1.setEnabledProperty(false);
            effect.DirectionalLight2.setEnabledProperty(false);

            switch (term)
            {
            case Term::Baseline:
                break;
            case Term::Emissive:
                // RED only. Added after the light sum, so it is the one term that would survive
                // even with lighting off -- and the only source of red above the baseline.
                effect.setEmissiveColorProperty(Vector3(0.45f, 0.0f, 0.0f));
                break;
            case Term::Ambient:
                // GREEN only, and it enters INSIDE the sum, so it is multiplied by DiffuseColor --
                // a renderer that added it afterwards produces a visibly different magnitude.
                effect.setAmbientLightColorProperty(Vector3(0.0f, 1.0f, 0.0f));
                break;
            case Term::ThreeLights:
                // BLUE only, from the two lights the baseline leaves off. Proves the shader sums
                // the rig rather than reading light 0 alone.
                effect.DirectionalLight1.setEnabledProperty(true);
                effect.DirectionalLight1.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
                effect.DirectionalLight1.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 0.5f));
                effect.DirectionalLight1.setSpecularColorProperty(Vector3::Zero);
                effect.DirectionalLight2.setEnabledProperty(true);
                effect.DirectionalLight2.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
                effect.DirectionalLight2.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 0.5f));
                effect.DirectionalLight2.setSpecularColorProperty(Vector3::Zero);
                break;
            case Term::Specular:
                // WHITE, and the only term that needs the eye vector rather than just N.L.
                effect.setSpecularColorProperty(Vector3(0.6f, 0.6f, 0.6f));
                effect.setSpecularPowerProperty(4.0f);
                effect.DirectionalLight0.setSpecularColorProperty(Vector3::One);
                break;
            }

            const auto corners = grid.QuadCorners(column, 0);
            struct Vertex { float x, y, z; float nx, ny, nz; };
            // Triangle STRIP order TL, BL, TR, BR -- QuadCorners returns the ring TL, BL, BR, TR.
            // Normals face the camera, so N.L is exactly 1 for every light in the rig.
            const std::array<Vertex, 4> verts{
                Vertex{corners[0].X, corners[0].Y, 0.0f, 0.0f, 0.0f, 1.0f},
                Vertex{corners[1].X, corners[1].Y, 0.0f, 0.0f, 0.0f, 1.0f},
                Vertex{corners[3].X, corners[3].Y, 0.0f, 0.0f, 0.0f, 1.0f},
                Vertex{corners[2].X, corners[2].Y, 0.0f, 0.0f, 0.0f, 1.0f}};
            VertexBuffer vb(device,
                            VertexDeclaration(24,
                                {VertexElement(0, VertexElementFormat::Vector3,
                                               VertexElementUsage::Position, 0),
                                 VertexElement(12, VertexElementFormat::Vector3,
                                               VertexElementUsage::Normal, 0)}),
                            static_cast<int>(verts.size()), BufferUsage::None);
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), 24);
            device.SetVertexBuffer(&vb);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
            device.SetVertexBuffer(nullptr);
        };

        drawColumn(0, Term::Baseline);
        drawColumn(1, Term::Emissive);
        drawColumn(2, Term::Ambient);
        drawColumn(3, Term::ThreeLights);
        drawColumn(4, Term::Specular);

        const Color baseline = Average(grid.Interior(0, 0));
        std::printf("[INFO] baseline = (%d,%d,%d)\n", baseline.getRProperty(),
                    baseline.getGProperty(), baseline.getBProperty());

        // The baseline must be a real lit surface: not black (lighting dropped) and not saturated
        // (nothing above it could then be seen). Both would make every check below vacuous.
        ExpectDistinct("the baseline is lit, not black", grid.Interior(0, 0),
                       Rectangle(0, 0, 1, 1), /*minDelta=*/40);
        Require(baseline.getRProperty() < 200 && baseline.getGProperty() < 200 &&
                    baseline.getBProperty() < 200,
                "the baseline leaves headroom for the terms above it");

        // Each term brightens ITS OWN channel and leaves the others where the baseline had them.
        // A renderer that routed a term through the wrong slot passes a brightness check and fails
        // this one.
        const auto expectChannel = [&](int column, const char* label, int channel)
        {
            const Color got = Average(grid.Interior(column, 0));
            const auto ch = [](const Color& c, int i) {
                return i == 0 ? c.getRProperty() : (i == 1 ? c.getGProperty() : c.getBProperty());
            };
            const int gained = ch(got, channel) - ch(baseline, channel);
            std::printf("[%s] %s: (%d,%d,%d), channel %d gained %d\n",
                        gained >= 25 ? "PASS" : "FAIL", label, got.getRProperty(),
                        got.getGProperty(), got.getBProperty(), channel, gained);
            if (gained < 25) MarkFailedEXT();
            for (int other = 0; other < 3; ++other)
            {
                if (other == channel) continue;
                const int drift = std::abs(ch(got, other) - ch(baseline, other));
                if (drift > 12)
                {
                    std::printf("[FAIL] %s: channel %d drifted %d from the baseline -- the term "
                                "reached a channel it does not feed\n", label, other, drift);
                    MarkFailedEXT();
                }
            }
        };

        expectChannel(1, "EmissiveColor adds its own colour after the light sum", 0);
        expectChannel(2, "AmbientLightColor adds inside the sum", 1);
        expectChannel(3, "the second and third lights are summed, not ignored", 2);

        // Specular is white, so it lifts every channel -- which is exactly why it cannot be checked
        // the same way. Its discriminator is that it lifts them TOGETHER.
        {
            const Color got = Average(grid.Interior(4, 0));
            const int dr = got.getRProperty() - baseline.getRProperty();
            const int dg = got.getGProperty() - baseline.getGProperty();
            const int db = got.getBProperty() - baseline.getBProperty();
            const bool pass = dr >= 25 && dg >= 25 && db >= 25 &&
                              std::abs(dr - dg) <= 12 && std::abs(dg - db) <= 12;
            std::printf("[%s] SpecularColor lifts every channel together: (%d,%d,%d), gains "
                        "(%d,%d,%d)\n", pass ? "PASS" : "FAIL", got.getRProperty(),
                        got.getGProperty(), got.getBProperty(), dr, dg, db);
            if (!pass) MarkFailedEXT();
        }

        ExpectFlat("the baseline is flat", grid.Interior(0, 0), /*maxSpread=*/3);
    }
};

CNA_PARITY_FIXTURE_MAIN(BasicEffectLightTermsParityFixture)
