// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-173 (harness: WEBGPU-207): `EnableDefaultLighting`, the way `Alpha`
// composes with `DiffuseColor`, and whether a large world transform costs precision.
//
// THE ALPHA LEG IS THE INTERESTING ONE, because the obvious implementation is wrong in a way no
// blended test would catch. XNA does NOT simply put `Alpha` in the alpha channel: `EffectHelpers`
// premultiplies, `diffuse.rgb = DiffuseColor * Alpha` and `diffuse.a = Alpha`, before the value
// ever reaches the shader. So `Alpha` darkens the RGB even under an OPAQUE blend state, where
// nothing is blended at all and the alpha channel is discarded. A renderer that routed `Alpha` to
// the alpha channel alone renders columns 1 and 2 identically; this fixture draws them opaque
// precisely so that the alpha channel cannot rescue it.
//
//   0  EnableDefaultLighting  -- the three-light rig XNA ships, not a hand-built one
//   1  Alpha 1.0              -- the reference for column 2
//   2  Alpha 0.5, opaque      -- must be half of column 1's RGB
//   3  world scale 1000       -- geometry pre-divided by 1000, so it lands where column 1 does
//
// Column 3 is the precision leg. Its vertices are a thousandth of column 1's and its world matrix
// scales by a thousand, so the composed transform is mathematically identical -- but only if the
// renderer keeps the world matrix at full precision through to the shader. A renderer that folded
// the transform at reduced precision, or that normalised the matrix somewhere, produces a quad
// that is offset or the wrong size, and the comparison against column 1 catches it.

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
    constexpr int kColumns = 4;
    constexpr int kCell = 64;
    constexpr int kWidth = kColumns * kCell;
    constexpr int kHeight = 64;
    /// Big enough that a float that lost precision would show, small enough to stay exact in
    /// binary floating point (1000 is not a power of two, which is the point -- a power of two
    /// would be exactly representable at every step and prove less).
    constexpr float kWorldScale = 1000.0f;

    const Color kClearColor(9, 13, 17, 255);

    enum class Leg { DefaultLighting, AlphaOne, AlphaHalf, LargeWorldScale };
}

/// WEBGPU-173: the default light rig, Alpha's premultiply, and large-world-transform precision.
class BasicEffectAlphaScaleParityFixture : public CNA::Parity::ParityFixture
{
public:
    BasicEffectAlphaScaleParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, 1};

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);
        // OPAQUE on purpose -- see this file's header. Nothing is blended, so the alpha channel is
        // discarded and only the premultiplied RGB can carry `Alpha`'s effect.
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(kClearColor);

        const auto drawColumn = [&](int column, Leg leg)
        {
            BasicEffect effect(device);
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::getIdentityProperty());
            effect.setTextureEnabledProperty(false);
            effect.setVertexColorEnabledProperty(false);

            float shrink = 1.0f;
            if (leg == Leg::LargeWorldScale)
            {
                effect.setWorldProperty(Matrix::CreateScale(kWorldScale));
                shrink = 1.0f / kWorldScale;
            }
            else
            {
                effect.setWorldProperty(Matrix::getIdentityProperty());
            }

            if (leg == Leg::DefaultLighting)
            {
                // The rig XNA ships, not one assembled here. A renderer that implemented
                // EnableDefaultLighting as "turn lighting on" without the three lights and the
                // ambient term produces a visibly different surface.
                effect.setDiffuseColorProperty(Vector3(0.6f, 0.6f, 0.6f));
                effect.EnableDefaultLighting();
            }
            else
            {
                effect.setLightingEnabledProperty(false);
                effect.setEmissiveColorProperty(Vector3::Zero);
                effect.setDiffuseColorProperty(Vector3(0.8f, 0.6f, 0.4f));
                effect.setAlphaProperty(leg == Leg::AlphaHalf ? 0.5f : 1.0f);
            }

            const auto corners = grid.QuadCorners(column, 0);
            struct Vertex { float x, y, z; float nx, ny, nz; };
            // Triangle STRIP order TL, BL, TR, BR. Normals face the camera so the default rig's
            // key light lands on the surface.
            const std::array<Vertex, 4> verts{
                Vertex{corners[0].X * shrink, corners[0].Y * shrink, 0.0f, 0.0f, 0.0f, 1.0f},
                Vertex{corners[1].X * shrink, corners[1].Y * shrink, 0.0f, 0.0f, 0.0f, 1.0f},
                Vertex{corners[3].X * shrink, corners[3].Y * shrink, 0.0f, 0.0f, 0.0f, 1.0f},
                Vertex{corners[2].X * shrink, corners[2].Y * shrink, 0.0f, 0.0f, 0.0f, 1.0f}};
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

        drawColumn(0, Leg::DefaultLighting);
        drawColumn(1, Leg::AlphaOne);
        drawColumn(2, Leg::AlphaHalf);
        drawColumn(3, Leg::LargeWorldScale);

        const Color defaultLit = Average(grid.Interior(0, 0));
        const Color full = Average(grid.Interior(1, 0));
        const Color half = Average(grid.Interior(2, 0));
        std::printf("[INFO] defaultLighting=(%d,%d,%d) alpha1=(%d,%d,%d) alpha0.5=(%d,%d,%d)\n",
                    defaultLit.getRProperty(), defaultLit.getGProperty(), defaultLit.getBProperty(),
                    full.getRProperty(), full.getGProperty(), full.getBProperty(),
                    half.getRProperty(), half.getGProperty(), half.getBProperty());

        // EnableDefaultLighting must actually light: not black, not the unlit surface.
        ExpectDistinct("EnableDefaultLighting produces a lit surface, not the clear colour",
                       grid.Interior(0, 0), Rectangle(0, 0, 1, 1), /*minDelta=*/40);
        ExpectFlat("the default-lit surface is flat across a face-on quad",
                   grid.Interior(0, 0), /*maxSpread=*/4);

        // Alpha premultiplies into RGB even under an opaque blend. Each channel must halve.
        {
            const auto halves = [](int whole, int part) {
                const int expected = whole / 2;
                return std::abs(part - expected) <= 3;
            };
            const bool pass = halves(full.getRProperty(), half.getRProperty()) &&
                              halves(full.getGProperty(), half.getGProperty()) &&
                              halves(full.getBProperty(), half.getBProperty());
            std::printf("[%s] Alpha premultiplies into RGB: (%d,%d,%d) at 1.0 becomes (%d,%d,%d) "
                        "at 0.5, under an OPAQUE blend where the alpha channel is discarded\n",
                        pass ? "PASS" : "FAIL", full.getRProperty(), full.getGProperty(),
                        full.getBProperty(), half.getRProperty(), half.getGProperty(),
                        half.getBProperty());
            if (!pass) MarkFailedEXT();
        }
        // ... and the two columns really are different, so the check above is not comparing a
        // colour with itself.
        ExpectDistinct("Alpha changes the opaque result at all",
                       grid.Interior(1, 0), grid.Interior(2, 0), /*minDelta=*/40);

        // Precision: a thousand-fold world scale over thousand-fold smaller geometry must land
        // exactly where the identity-world column did.
        ExpectSameRegion("a world scale of 1000 over 1/1000 geometry reproduces the identity "
                         "transform exactly",
                         grid.Interior(1, 0), grid.Interior(3, 0), /*tolerance=*/2);
    }
};

CNA_PARITY_FIXTURE_MAIN(BasicEffectAlphaScaleParityFixture)
