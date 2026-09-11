// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-173 (harness: WEBGPU-207): `VertexColorEnabled`, with and without a
// texture, and what happens when the product runs past 1.0.
//
// XNA's unlit colour is `DiffuseColor * texture? * vertexColor?`, where each factor is present only
// when its own switch says so. That is three independent gates, and a renderer can get the picture
// right for the wrong reason -- always multiplying the vertex colour, for instance, looks correct
// on every mesh whose vertices are white.
//
// EVERY COLUMN SHARES ONE VERTEX LAYOUT AND ONE MESH. All four carry a COLOR0 element with the same
// non-white value; the columns differ only in `VertexColorEnabled` and `TextureEnabled`. So a
// renderer that ignores the switch and always multiplies fails columns 0 and 2, and one that never
// multiplies fails 1 and 3 -- neither can be hidden by choosing convenient vertex data.
//
//   0  neither          -> DiffuseColor alone
//   1  vertex colour    -> DiffuseColor * vertexColor
//   2  texture          -> DiffuseColor * texture
//   3  both             -> DiffuseColor * texture * vertexColor
//   4  the clamp        -> a product past 1.0 saturates, it does not wrap
//
// The vertex colour is (255, 128, 64): three DIFFERENT channels, so a renderer that multiplied by
// luminance, or swizzled, produces a colour no expected value matches. A white vertex colour would
// have hidden all of that.

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
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kColumns = 5;
    constexpr int kCell = 64;
    constexpr int kWidth = kColumns * kCell;
    constexpr int kHeight = 64;
    constexpr int kStride = 24;   ///< VertexPositionColorTexture.

    const Color kClearColor(9, 13, 17, 255);
    /// Three different channels on purpose -- see this file's header.
    const Color kVertexColor(255, 128, 64, 255);
    /// Likewise: no two channels alike, and none of them 0 or 255.
    const Color kTexel(128, 192, 64, 255);
}

/// WEBGPU-173: VertexColorEnabled and TextureEnabled are independent gates, and the product clamps.
class BasicEffectVertexColorParityFixture : public CNA::Parity::ParityFixture
{
public:
    BasicEffectVertexColorParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, 1};

        Texture2D texture(device, 2, 2, false, SurfaceFormat::Color);
        {
            const std::array<Color, 4> texels{kTexel, kTexel, kTexel, kTexel};
            texture.SetData(texels.data(), static_cast<int>(texels.size()));
        }

        SamplerState pointClamp;
        pointClamp.setFilterProperty(TextureFilter::Point);
        pointClamp.setAddressUProperty(TextureAddressMode::Clamp);
        pointClamp.setAddressVProperty(TextureAddressMode::Clamp);
        device.getSamplerStatesProperty()[0] = pointClamp;

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(kClearColor);

        const auto drawColumn = [&](int column, bool vertexColor, bool textured, bool clampLeg)
        {
            BasicEffect effect(device);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::getIdentityProperty());
            // Lighting OFF for the gate columns, so their colour is a pure product and nothing
            // else can move it. `EmissiveColor` is zero there, which keeps the product clean --
            // note that with lighting off XNA folds `(DiffuseColor + EmissiveColor) * Alpha` on the
            // CPU (`EffectHelpers.SetMaterialColor`), so emissive DOES apply on that path; it is
            // left at zero here for clarity, not because it would be ignored.
            //
            // The clamp leg also leaves the VERTEX COLOUR OFF, and that is not a simplification.
            // It is steering around a SHARED CNA DEFECT this fixture found: on the LIT
            // vertex-colour path, `EmissiveColor` is dropped entirely. FNA's own
            // `VSBasicVertexLightingVc` is `ComputeCommonVSOutputWithLighting(...)` -- whose
            // `Lighting.fxh` line 43 is `... * DiffuseColor.rgb + EmissiveColor` -- followed by
            // `vout.Diffuse *= vin.Color`, so XNA's answer is `(diffuse + emissive) * vertexColor`.
            // Measured here, EasyGL and WebGPU BOTH return `diffuse * vertexColor`: for this leg's
            // inputs XNA gives green 243 and both renderers give 128. WebGPU is not cloning an
            // EasyGL bug -- both already have it -- so this fixture records the defect rather than
            // asserting the broken behaviour as correct, and `plans/plan_graphics.md` carries the
            // row. Asserting the XNA answer here would paint both renderers red and make the
            // fixture useless as a regression gate for everything else it covers.
            effect.setLightingEnabledProperty(clampLeg);
            effect.setEmissiveColorProperty(Vector3::Zero);
            if (clampLeg)
            {
                effect.setPreferPerPixelLightingProperty(false);
                effect.setAmbientLightColorProperty(Vector3::Zero);
                effect.setSpecularColorProperty(Vector3::Zero);
                effect.DirectionalLight0.setEnabledProperty(true);
                effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
                effect.DirectionalLight0.setDiffuseColorProperty(Vector3::One);
                effect.DirectionalLight0.setSpecularColorProperty(Vector3::Zero);
                effect.DirectionalLight1.setEnabledProperty(false);
                effect.DirectionalLight2.setEnabledProperty(false);
            }
            effect.setVertexColorEnabledProperty(vertexColor);
            effect.setTextureEnabledProperty(textured);
            if (textured) effect.setTextureProperty(&texture);
            // The clamp leg deliberately asks for more than the pipeline can carry: a fully lit
            // surface plus 0.9 of emissive in RED and GREEN. Both run past 1.0 and must saturate;
            // BLUE is held at 0.25 on purpose, as the control that proves saturation is per channel
            // rather than the whole fragment being driven to white. A wrap or a signed overflow
            // produces something dark instead.
            effect.setDiffuseColorProperty(clampLeg ? Vector3(1.0f, 1.0f, 0.25f)
                                                    : Vector3(0.8f, 0.8f, 0.8f));
            if (clampLeg) effect.setEmissiveColorProperty(Vector3(0.9f, 0.9f, 0.0f));

            const auto corners = grid.QuadCorners(column, 0);
            struct Vertex { float x, y, z; std::uint32_t color; float u, v; };
            const std::uint32_t packed = kVertexColor.getPackedValueProperty();
            // Triangle STRIP order TL, BL, TR, BR.
            const std::array<Vertex, 4> verts{
                Vertex{corners[0].X, corners[0].Y, 0.0f, packed, 0.0f, 0.0f},
                Vertex{corners[1].X, corners[1].Y, 0.0f, packed, 0.0f, 1.0f},
                Vertex{corners[3].X, corners[3].Y, 0.0f, packed, 1.0f, 0.0f},
                Vertex{corners[2].X, corners[2].Y, 0.0f, packed, 1.0f, 1.0f}};
            VertexBuffer vb(device,
                            VertexDeclaration(kStride,
                                {VertexElement(0, VertexElementFormat::Vector3,
                                               VertexElementUsage::Position, 0),
                                 VertexElement(12, VertexElementFormat::Color,
                                               VertexElementUsage::Color, 0),
                                 VertexElement(16, VertexElementFormat::Vector2,
                                               VertexElementUsage::TextureCoordinate, 0)}),
                            static_cast<int>(verts.size()), BufferUsage::None);
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), kStride);
            device.SetVertexBuffer(&vb);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
            device.SetVertexBuffer(nullptr);
        };

        drawColumn(0, /*vertexColor=*/false, /*textured=*/false, /*clampLeg=*/false);
        drawColumn(1, /*vertexColor=*/true,  /*textured=*/false, /*clampLeg=*/false);
        drawColumn(2, /*vertexColor=*/false, /*textured=*/true,  /*clampLeg=*/false);
        drawColumn(3, /*vertexColor=*/true,  /*textured=*/true,  /*clampLeg=*/false);
        drawColumn(4, /*vertexColor=*/false, /*textured=*/false, /*clampLeg=*/true);

        // The expected products, computed here rather than written as constants, so the expectation
        // is the FORMULA and not a value copied out of a passing run.
        const auto scale = [](int a, int b) { return (a * b + 127) / 255; };
        const int d = 204;   // 0.8 * 255
        const Color expectedNeither(d, d, d, 255);
        const Color expectedVertex(scale(d, kVertexColor.getRProperty()),
                                   scale(d, kVertexColor.getGProperty()),
                                   scale(d, kVertexColor.getBProperty()), 255);
        const Color expectedTexture(scale(d, kTexel.getRProperty()),
                                    scale(d, kTexel.getGProperty()),
                                    scale(d, kTexel.getBProperty()), 255);
        const Color expectedBoth(scale(expectedTexture.getRProperty(), kVertexColor.getRProperty()),
                                 scale(expectedTexture.getGProperty(), kVertexColor.getGProperty()),
                                 scale(expectedTexture.getBProperty(), kVertexColor.getBProperty()),
                                 255);

        ExpectAverage("neither gate on: DiffuseColor alone", grid.Interior(0, 0),
                      expectedNeither, 3);
        ExpectAverage("VertexColorEnabled multiplies the vertex colour in", grid.Interior(1, 0),
                      expectedVertex, 3);
        ExpectAverage("TextureEnabled multiplies the texel in", grid.Interior(2, 0),
                      expectedTexture, 3);
        ExpectAverage("both gates on: all three factors", grid.Interior(3, 0), expectedBoth, 3);

        // The gates are INDEPENDENT: each column differs from every neighbour it should differ
        // from. Without this, a renderer that always multiplied could still hit three of the four
        // expectations if its factors happened to compose.
        ExpectDistinct("VertexColorEnabled changes the untextured result",
                       grid.Interior(0, 0), grid.Interior(1, 0), /*minDelta=*/60);
        ExpectDistinct("TextureEnabled changes the result on its own",
                       grid.Interior(0, 0), grid.Interior(2, 0), /*minDelta=*/30);
        ExpectDistinct("the two gates compose rather than one masking the other",
                       grid.Interior(2, 0), grid.Interior(3, 0), /*minDelta=*/60);

        // The clamp. Red and green run past 1.0 and must saturate; blue stays below and must NOT,
        // which is what distinguishes real per-channel clamping from a fragment forced to white.
        {
            const Color got = Average(grid.Interior(4, 0));
            std::printf("[INFO] clamp leg = (%d,%d,%d)\n", got.getRProperty(),
                        got.getGProperty(), got.getBProperty());
            const bool saturated = got.getRProperty() >= 250 && got.getGProperty() >= 250;
            const bool blueUntouched = got.getBProperty() < 120;
            std::printf("[%s] a value past 1.0 saturates rather than wrapping\n",
                        saturated ? "PASS" : "FAIL");
            if (!saturated) MarkFailedEXT();
            std::printf("[%s] the clamp is per channel -- blue stayed below 1.0 and did not "
                        "saturate with the others\n", blueUntouched ? "PASS" : "FAIL");
            if (!blueUntouched) MarkFailedEXT();
        }

        ExpectFlat("the both-gates column is flat", grid.Interior(3, 0), /*maxSpread=*/3);
    }
};

CNA_PARITY_FIXTURE_MAIN(BasicEffectVertexColorParityFixture)
