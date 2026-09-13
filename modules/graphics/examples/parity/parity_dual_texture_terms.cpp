// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-175 (harness: WEBGPU-207): every factor in `DualTextureEffect`'s
// blend, each one able to change the pixel on its own.
//
// FNA's `PSDualTexture` is four multiplications and one of them is easy to lose:
//
//     float4 color   = SAMPLE_TEXTURE(Texture,  pin.TexCoord);
//     float4 overlay = SAMPLE_TEXTURE(Texture2, pin.TexCoord2);
//     color.rgb *= 2;
//     color *= overlay * pin.Diffuse;
//
// so the surviving RGB is `2 * texture0 * texture2 * DiffuseColor * Alpha`, RGB only -- the `*2`
// never touches alpha. Every texel here is deliberately UNSATURATED: a missing `*2` is invisible
// when 1*2 clamps straight back to 1, which is exactly how three renderers shipped without the
// doubling until EasyGL's own Task 383 caught it. The three channels also differ in every texture,
// so a renderer that collapsed a texture to luminance, or swapped the two layers, lands somewhere
// this table does not contain.
//
// Row 0 isolates one factor per column against a white second layer:
//   * the `*2` doubling      -- (100,90,60) must come back (200,180,120), not (100,90,60);
//   * the overlay            -- multiplying in the second layer;
//   * `DiffuseColor`         -- (1, 0.5, 0.25), so each channel is scaled differently;
//   * `Alpha`                -- which XNA folds into RGB on the CPU (`EffectHelpers.SetMaterialColor`
//                               sets `diffuse.rgb = DiffuseColor * Alpha`), so it DARKENS the pixel
//                               under an opaque blend where the alpha channel is thrown away. A
//                               renderer that routed `Alpha` to the alpha channel alone is caught.
//
// Row 1 is the two null-texture fallbacks and the full composition:
//   * a null `Texture`  -- layer 0 falls back to opaque white, so `2 * white * overlay`;
//   * a null `Texture2` -- layer 1 falls back to opaque white, so `2 * base * white`.
//     These two use DIFFERENT, channel-reversed texels ((100,90,60) against (60,90,100)) precisely
//     because the formula is symmetric in the two layers: with the same texel they would produce
//     the same pixel and a renderer that applied the fallback to the WRONG layer would pass. With
//     these, it does not.
//   * both null -- the control that pins the fallback as WHITE rather than black or transparent:
//     `2 * 1 * 1` saturates to (255,255,255). If either fallback were black this cell is black.
//   * everything at once -- all four factors on the same draw, landing on (118,32,22), a value no
//     dropped factor reaches.

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
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
#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kColumns = 4;
    constexpr int kRows = 2;
    constexpr int kCell = 32;
    constexpr int kWidth = kColumns * kCell;
    constexpr int kHeight = kRows * kCell;

    const Color kClearColor(9, 13, 17, 255);

    /// The base layer. Unsaturated so the `*2` is visible, and three different channels so a
    /// luminance collapse or a channel swizzle matches nothing in the table.
    const Color kBase(100, 90, 60, 255);
    /// The base layer's channel reversal, used only by the null-`Texture2` cell so that cell cannot
    /// be confused with the null-`Texture` one.
    const Color kBaseReversed(60, 90, 100, 255);
    /// The overlay. Its blue is 255 on purpose: `2 * base.b * 1.0` still lands at 120, well inside
    /// range, so the doubling stays visible even on the channel the overlay leaves alone.
    const Color kOverlay(200, 120, 255, 255);
    const Color kWhite(255, 255, 255, 255);

    const Vector3 kDiffuseColor(1.0f, 0.5f, 0.25f);
    constexpr float kAlpha = 0.75f;

    struct Vertex { float x, y, z; float u0, v0; float u1, v1; };
    constexpr int kStride = 28;

    [[nodiscard]] VertexDeclaration DualUvDeclaration()
    {
        return VertexDeclaration(kStride, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
            VertexElement(20, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 1),
        });
    }
}

/// WEBGPU-175: DualTextureEffect's doubling, overlay, DiffuseColor, Alpha and null-texture fallbacks.
class DualTextureTermsParityFixture : public CNA::Parity::ParityFixture
{
public:
    DualTextureTermsParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, kRows};

        const auto makeTexture = [&device](const Color& texel) {
            Texture2D texture(device, 2, 2, false, SurfaceFormat::Color);
            const std::array<Color, 4> texels{texel, texel, texel, texel};
            texture.SetData(texels.data(), static_cast<int>(texels.size()));
            return texture;
        };
        Texture2D base = makeTexture(kBase);
        Texture2D baseReversed = makeTexture(kBaseReversed);
        Texture2D overlay = makeTexture(kOverlay);
        Texture2D white = makeTexture(kWhite);

        SamplerState pointClamp;
        pointClamp.setFilterProperty(TextureFilter::Point);
        pointClamp.setAddressUProperty(TextureAddressMode::Clamp);
        pointClamp.setAddressVProperty(TextureAddressMode::Clamp);
        device.getSamplerStatesProperty()[0] = pointClamp;
        device.getSamplerStatesProperty()[1] = pointClamp;

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);
        // Opaque, so the alpha channel is discarded and `Alpha`'s effect can only be seen in RGB --
        // which is the point of the Alpha column.
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(kClearColor);

        struct Cell
        {
            const char* label;
            Texture2D* texture0;
            Texture2D* texture2;
            Vector3 diffuse;
            float alpha;
            Color expected;
        };
        const std::array<Cell, kColumns * kRows> cells{{
            // Row 0 -- one factor at a time, against a white second layer.
            {"doubling: 2 * base", &base, &white, Vector3::One, 1.0f, Color(200, 180, 120, 255)},
            {"overlay: 2 * base * overlay", &base, &overlay, Vector3::One, 1.0f,
             Color(157, 85, 120, 255)},
            {"DiffuseColor: 2 * base * (1, 0.5, 0.25)", &base, &white, kDiffuseColor, 1.0f,
             Color(200, 90, 30, 255)},
            {"Alpha 0.75 darkens RGB under an opaque blend", &base, &white, Vector3::One, kAlpha,
             Color(150, 135, 90, 255)},
            // Row 1 -- the fallbacks, and the whole chain.
            {"null Texture: 2 * white * base", nullptr, &base, Vector3::One, 1.0f,
             Color(200, 180, 120, 255)},
            {"null Texture2: 2 * reversed base * white", &baseReversed, nullptr, Vector3::One, 1.0f,
             Color(120, 180, 200, 255)},
            {"both null: 2 * white * white saturates", nullptr, nullptr, Vector3::One, 1.0f,
             Color(255, 255, 255, 255)},
            {"all four factors at once", &base, &overlay, kDiffuseColor, kAlpha,
             Color(118, 32, 22, 255)},
        }};

        for (std::size_t index = 0; index < cells.size(); ++index)
        {
            const Cell& cell = cells[index];
            const int column = static_cast<int>(index) % kColumns;
            const int row = static_cast<int>(index) / kColumns;

            DualTextureEffect effect(device);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::getIdentityProperty());
            effect.setTextureProperty(cell.texture0);
            effect.setTexture2Property(cell.texture2);
            effect.setDiffuseColorProperty(cell.diffuse);
            effect.setAlphaProperty(cell.alpha);

            const auto corners = grid.QuadCorners(column, row);
            // Triangle STRIP order TL, BL, TR, BR. Both UV sets are identical here -- WEBGPU-159
            // already proved TEXCOORD0 and TEXCOORD1 are consumed independently, so this fixture
            // varies the FACTORS and holds the coordinates still.
            const std::array<Vertex, 4> verts{
                Vertex{corners[0].X, corners[0].Y, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
                Vertex{corners[1].X, corners[1].Y, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
                Vertex{corners[3].X, corners[3].Y, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
                Vertex{corners[2].X, corners[2].Y, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
            VertexBuffer vb(device, DualUvDeclaration(), static_cast<int>(verts.size()),
                            BufferUsage::None);
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), kStride);
            device.SetVertexBuffer(&vb);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
            device.SetVertexBuffer(nullptr);
        }

        for (std::size_t index = 0; index < cells.size(); ++index)
        {
            const Cell& cell = cells[index];
            ExpectAverage(cell.label,
                          grid.Interior(static_cast<int>(index) % kColumns,
                                        static_cast<int>(index) / kColumns),
                          cell.expected, 3);
        }

        // The doubling's own discriminating claim, stated as a comparison rather than as trust in
        // the constant above: the isolated base layer must come back at TWICE the texel, so it must
        // be strictly brighter than the texel could ever be on its own.
        Require(Average(grid.Interior(0, 0)).getRProperty() > kBase.getRProperty() + 60,
                "the `color.rgb *= 2` doubling actually happened -- the isolated base layer is far "
                "brighter than its own texel, which a renderer missing the *2 cannot be");
        // And the two fallbacks really are distinguishable, so the pair of null cells is a test
        // rather than two spellings of one.
        ExpectDistinct("the null-Texture and null-Texture2 cells differ -- the fallback lands on "
                       "the layer that is actually missing",
                       grid.Interior(0, 1), grid.Interior(1, 1), 40);
    }
};

CNA_PARITY_FIXTURE_MAIN(DualTextureTermsParityFixture)
