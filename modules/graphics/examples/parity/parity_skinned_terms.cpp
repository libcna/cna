// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-177 (harness: WEBGPU-207): `SkinnedEffect`'s untested terms -- an
// identity bone, a pure translation bone, a two-bone blend at 50/50, the far end of the bone
// palette, several lights, specular, and `VertexColorEnabled`.
//
// FNA's `Skin()` is one line that everything here is about:
//
//     skinning += Bones[vin.Indices[i]] * vin.Weights[i];   for i < WeightsPerVertex
//     vin.Position.xyz = mul(vin.Position, skinning);
//     vin.Normal       = mul(vin.Normal, (float3x3)skinning);
//
// The matrix is a WEIGHTED SUM of palette entries, not a selection, and that is what makes the
// 50/50 cell the sharp one: blending identity with a translation of `2d` at half weight each must
// land the quad in exactly the same place as a single bone translating by `d`. A renderer that
// picked the highest-weighted bone, or normalized the weights differently, or summed only the first
// influence, misses that -- and it misses it by a whole quad width, not by a rounding step. The two
// cells are therefore compared to EACH OTHER, pixel for pixel, rather than to a constant.
//
// EACH QUAD IS HALF ITS CELL. The translation cells move their quad by a quarter of a cell, so it
// stays inside its own cell and can never bleed into a neighbour and make a difference look like a
// shading difference. That also means "the quad moved" is directly visible as the cell's painted
// half changing side, which is what the left/right half assertions read.
//
// THE BONE-PALETTE CELL uses index `MaxBones - 1` (71), the last slot XNA allows, with every other
// bone left identity. A renderer that uploaded a truncated palette, or indexed it from the wrong
// end, renders that cell unmoved -- and "unmoved" is exactly the identity cell, which is why this
// cell translates: an unmoved quad and a moved one are different pictures.
//
// The vertex is CNA's stride-52 skinned record (position, normal, uv, four float weights, four
// ubyte indices) and its stride-56 twin with a trailing ubyte4 colour for the `VertexColorEnabled`
// cell.
//
// THE FIFTH COLUMN DECLARES ITS BLEND INDICES AS `Vector4`, not `Byte4`, at stride 64. XNA's
// `VertexElementFormat` describes the BYTES in the buffer, not the register the semantic arrives
// in, so a content processor may write either -- the stock `SkinnedModelProcessor` writes
// `ConvertChannelContent<Vector4>("BlendIndices0")` and real XNA draws it (`plans/plan_fx.md`
// FX-127). Both column-4 cells draw the SAME scene as the `Byte4` cell beside them and must land on
// the same pixels: row 0 repeats the palette's last slot (index 71 as a float), row 1 repeats the
// one-light shading. A renderer that read the float lane as raw bytes selects bone 0 and the quad
// does not move; one that refused the declaration does not draw at all.

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
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
    constexpr int kRows = 2;
    constexpr int kCell = 32;
    constexpr int kWidth = kColumns * kCell;
    constexpr int kHeight = kRows * kCell;

    const Color kClearColor(9, 13, 17, 255);

    /// Clip-space half-width of a cell, and of the quad drawn inside it.
    constexpr float kCellClipHalfWidth = 1.0f / static_cast<float>(kColumns);
    constexpr float kCellClipHalfHeight = 1.0f / static_cast<float>(kRows);
    /// The quad is a QUARTER of its cell wide (8 of 32 pixels) and the translation moves it by
    /// exactly its own width. That is what makes "moved" readable as two fixed probe strips either
    /// side of the resting position: unshifted the quad covers neither, shifted it covers the right
    /// one and still never leaves its cell.
    constexpr float kQuadHalf = kCellClipHalfWidth * 0.25f;
    constexpr float kShift = kQuadHalf * 2.0f;
    /// The quad sits at z = 0.5 with its normal pointing back at the eye, which View = identity
    /// puts at the origin -- the same convention `webgpu_skinned3d_test.cpp` uses, and the reason
    /// the directional light below points along +Z: `NdotL = max(dot(N, -L), 0)` needs `-L` to
    /// face the surface, so a light at (0,0,1) is the one that lights a normal of (0,0,-1).
    constexpr float kQuadZ = 0.5f;

    struct SkinnedVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedVertex) == 52, "CNA's skinned vertex is 52 bytes");

    struct SkinnedColorVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(SkinnedColorVertex) == 56, "the skinned+colour vertex is 56 bytes");

    [[nodiscard]] VertexDeclaration SkinnedDeclaration(bool withColor)
    {
        std::vector<VertexElement> elements{
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Vector4,
                          VertexElementUsage::BlendWeight, 0),
            VertexElement(48, VertexElementFormat::Byte4,
                          VertexElementUsage::BlendIndices, 0),
        };
        if (withColor)
            elements.push_back(VertexElement(52, VertexElementFormat::Color,
                                             VertexElementUsage::Color, 0));
        return VertexDeclaration(withColor ? 56 : 52, elements);
    }

    /// The same five semantics with BLENDINDICES0 declared `Vector4` -- four floats where the
    /// canonical record puts four bytes, so the stride is 64 rather than 52.
    struct FloatIndexSkinnedVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        float i0, i1, i2, i3;
    };
    static_assert(sizeof(FloatIndexSkinnedVertex) == 64, "the Vector4-index vertex is 64 bytes");

    [[nodiscard]] VertexDeclaration FloatIndexSkinnedDeclaration()
    {
        return VertexDeclaration(64, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::BlendIndices, 0),
        });
    }
}

/// WEBGPU-177: identity/translation/blended bones, the palette's far end, lights, specular, colour.
class SkinnedTermsParityFixture : public CNA::Parity::ParityFixture
{
public:
    SkinnedTermsParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, kRows};

        Texture2D white(device, 2, 2, false, SurfaceFormat::Color);
        const Color w(255, 255, 255, 255);
        const std::array<Color, 4> whiteTexels{w, w, w, w};
        white.SetData(whiteTexels.data(), static_cast<int>(whiteTexels.size()));

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

        enum class BoneCase { Identity, TranslateOne, BlendTwoHalves, LastPaletteSlot };
        struct Cell
        {
            const char* label;
            BoneCase bones;
            int weightsPerVertex;
            int lightCount;
            bool specular;
            bool vertexColor;
            bool floatIndices;   ///< Declare BLENDINDICES0 as Vector4 at stride 64.
        };
        const std::array<Cell, kColumns * kRows> cells{{
            {"identity bones leave the quad where it was", BoneCase::Identity, 1, 1, false, false,
             false},
            {"one bone translating by d", BoneCase::TranslateOne, 1, 1, false, false, false},
            {"two bones at 50/50: identity and 2d", BoneCase::BlendTwoHalves, 2, 1, false, false,
             false},
            {"bone index 71, the last slot XNA allows", BoneCase::LastPaletteSlot, 1, 1, false,
             false, false},
            {"bone index 71 declared Vector4, at stride 64", BoneCase::LastPaletteSlot, 1, 1, false,
             false, true},
            {"one directional light", BoneCase::Identity, 1, 1, false, false, false},
            {"three directional lights", BoneCase::Identity, 1, 3, false, false, false},
            {"specular highlight", BoneCase::Identity, 1, 1, true, false, false},
            {"VertexColorEnabled on the stride-56 record", BoneCase::Identity, 1, 1, false, true,
             false},
            {"identity bones declared Vector4, at stride 64", BoneCase::Identity, 1, 1, false, false,
             true},
        }};

        for (std::size_t index = 0; index < cells.size(); ++index)
        {
            const Cell& cell = cells[index];
            const int column = static_cast<int>(index) % kColumns;
            const int row = static_cast<int>(index) / kColumns;

            std::vector<Matrix> bones(static_cast<std::size_t>(SkinnedEffect::MaxBones),
                                      Matrix::getIdentityProperty());
            std::uint8_t index0 = 0;
            std::uint8_t index1 = 0;
            float weight0 = 1.0f;
            float weight1 = 0.0f;
            switch (cell.bones)
            {
            case BoneCase::Identity:
                break;
            case BoneCase::TranslateOne:
                bones[0] = Matrix::CreateTranslation(kShift, 0.0f, 0.0f);
                break;
            case BoneCase::BlendTwoHalves:
                // Identity at half weight plus a DOUBLE translation at half weight. FNA sums the
                // weighted matrices, so the result is a translation by exactly `kShift` -- the same
                // matrix the cell to the left builds from one bone, reached a different way.
                bones[0] = Matrix::getIdentityProperty();
                bones[1] = Matrix::CreateTranslation(kShift * 2.0f, 0.0f, 0.0f);
                index0 = 0;
                index1 = 1;
                weight0 = 0.5f;
                weight1 = 0.5f;
                break;
            case BoneCase::LastPaletteSlot:
                bones[static_cast<std::size_t>(SkinnedEffect::MaxBones - 1)] =
                    Matrix::CreateTranslation(kShift, 0.0f, 0.0f);
                index0 = static_cast<std::uint8_t>(SkinnedEffect::MaxBones - 1);
                break;
            }

            SkinnedEffect effect(device);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::getIdentityProperty());
            effect.setTextureProperty(&white);
            effect.SetBoneTransforms(bones);
            effect.setWeightsPerVertexProperty(cell.weightsPerVertex);
            effect.setDiffuseColorProperty(Vector3(0.8f, 0.4f, 0.2f));
            effect.setEmissiveColorProperty(Vector3::Zero);
            effect.setAmbientLightColorProperty(Vector3::Zero);
            effect.setAlphaProperty(1.0f);
            effect.setSpecularColorProperty(cell.specular ? Vector3(0.6f, 0.6f, 0.6f)
                                                          : Vector3::Zero);
            effect.setSpecularPowerProperty(4.0f);
            effect.VertexColorEnabled = cell.vertexColor;

            effect.getDirectionalLight0Property().setEnabledProperty(true);
            effect.getDirectionalLight0Property().setDirectionProperty(Vector3(0.0f, 0.0f, 1.0f));
            effect.getDirectionalLight0Property().setDiffuseColorProperty(Vector3(0.5f, 0.5f, 0.5f));
            effect.getDirectionalLight0Property().setSpecularColorProperty(
                cell.specular ? Vector3(1.0f, 1.0f, 1.0f) : Vector3::Zero);
            const bool three = cell.lightCount == 3;
            for (int light = 1; light <= 2; ++light)
            {
                DirectionalLight& dl = light == 1 ? effect.getDirectionalLight1Property()
                                                  : effect.getDirectionalLight2Property();
                dl.setEnabledProperty(three);
                dl.setDirectionProperty(Vector3(0.0f, 0.0f, 1.0f));
                dl.setDiffuseColorProperty(three ? (light == 1 ? Vector3(0.25f, 0.0f, 0.0f)
                                                               : Vector3(0.0f, 0.0f, 0.25f))
                                                 : Vector3::Zero);
                dl.setSpecularColorProperty(Vector3::Zero);
            }

            // The cell's own clip-space centre, with the quad at half the cell's size.
            const float cx = -1.0f + (2.0f * static_cast<float>(column) + 1.0f) * kCellClipHalfWidth;
            const float cy = 1.0f - (2.0f * static_cast<float>(row) + 1.0f) * kCellClipHalfHeight;
            const float halfW = kQuadHalf;
            const float halfH = kCellClipHalfHeight;
            const auto makeVertex = [&](float x, float y, float u, float v) {
                return SkinnedVertex{x, y, kQuadZ, 0.0f, 0.0f, -1.0f, u, v,
                                     weight0, weight1, 0.0f, 0.0f, index0, index1, 0, 0};
            };
            // Triangle STRIP order TL, BL, TR, BR.
            const std::array<SkinnedVertex, 4> verts{
                makeVertex(cx - halfW, cy + halfH, 0.0f, 0.0f),
                makeVertex(cx - halfW, cy - halfH, 0.0f, 1.0f),
                makeVertex(cx + halfW, cy + halfH, 1.0f, 0.0f),
                makeVertex(cx + halfW, cy - halfH, 1.0f, 1.0f)};

            if (cell.floatIndices)
            {
                std::array<FloatIndexSkinnedVertex, 4> floatIndexed{};
                for (std::size_t i = 0; i < floatIndexed.size(); ++i)
                {
                    const SkinnedVertex& v = verts[i];
                    floatIndexed[i] = FloatIndexSkinnedVertex{
                        v.px, v.py, v.pz, v.nx, v.ny, v.nz, v.u, v.v,
                        v.w0, v.w1, v.w2, v.w3,
                        static_cast<float>(v.i0), static_cast<float>(v.i1),
                        static_cast<float>(v.i2), static_cast<float>(v.i3)};
                }
                VertexBuffer vb(device, FloatIndexSkinnedDeclaration(),
                                static_cast<int>(floatIndexed.size()), BufferUsage::None);
                vb.SetDataRaw(floatIndexed.data(), static_cast<int>(floatIndexed.size()), 64);
                device.SetVertexBuffer(&vb);
                effect.Apply();
                device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
                device.SetVertexBuffer(nullptr);
            }
            else if (cell.vertexColor)
            {
                // A non-white, three-different-channel vertex colour, so an ignored one and an
                // applied one differ in every channel.
                const Color vertexColor(255, 128, 64, 255);
                std::array<SkinnedColorVertex, 4> colored{};
                for (std::size_t i = 0; i < colored.size(); ++i)
                {
                    const SkinnedVertex& s = verts[i];
                    colored[i] = SkinnedColorVertex{s.px, s.py, s.pz, s.nx, s.ny, s.nz, s.u, s.v,
                                                    s.w0, s.w1, s.w2, s.w3, s.i0, s.i1, s.i2, s.i3,
                                                    vertexColor.getRProperty(),
                                                    vertexColor.getGProperty(),
                                                    vertexColor.getBProperty(),
                                                    vertexColor.getAProperty()};
                }
                VertexBuffer vb(device, SkinnedDeclaration(true),
                                static_cast<int>(colored.size()), BufferUsage::None);
                vb.SetDataRaw(colored.data(), static_cast<int>(colored.size()), 56);
                device.SetVertexBuffer(&vb);
                effect.Apply();
                device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
                device.SetVertexBuffer(nullptr);
            }
            else
            {
                VertexBuffer vb(device, SkinnedDeclaration(false),
                                static_cast<int>(verts.size()), BufferUsage::None);
                vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), 52);
                device.SetVertexBuffer(&vb);
                effect.Apply();
                device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
                device.SetVertexBuffer(nullptr);
            }
        }

        // Three fixed strips per cell, each one quad-width (8 px) wide and spanning the cell's
        // middle rows. The quad rests on the CENTRE strip; a translation of exactly one quad width
        // moves it onto the RIGHT strip and off the centre. `ParityGrid::Interior` is not used for
        // these, because its 8-pixel inset leaves a 16-pixel window that the shift would walk
        // straight out of -- these probes are cut from the full cell.
        const auto strip = [](int column, int row, int offsetInQuadWidths) {
            constexpr int kQuadPixels = kCell / 4;
            const int centreX = column * kCell + kCell / 2 - kQuadPixels / 2;
            return Rectangle(centreX + offsetInQuadWidths * kQuadPixels,
                             row * kCell + kCell / 4, kQuadPixels, kCell / 2);
        };
        const auto quadRect = [&strip](int column, int row) { return strip(column, row, 0); };
        const auto rightQuarter = [&strip](int column, int row) { return strip(column, row, 1); };
        const auto leftQuarter = [&strip](int column, int row) { return strip(column, row, -1); };
        const auto painted = [this](const Rectangle& r) {
            const Color got = Average(r);
            return std::abs(got.getRProperty() - kClearColor.getRProperty()) > 8
                || std::abs(got.getGProperty() - kClearColor.getGProperty()) > 8
                || std::abs(got.getBProperty() - kClearColor.getBProperty()) > 8;
        };

        for (std::size_t index = 0; index < cells.size(); ++index)
        {
            const int column = static_cast<int>(index) % kColumns;
            const int row = static_cast<int>(index) / kColumns;
            const Color got = Average(quadRect(column, row));
            std::printf("[info] %-46s quad (%d,%d,%d) left=%d right=%d\n", cells[index].label,
                        got.getRProperty(), got.getGProperty(), got.getBProperty(),
                        painted(leftQuarter(column, row)) ? 1 : 0,
                        painted(rightQuarter(column, row)) ? 1 : 0);
        }

        // --- Row 0: the skinning matrix -------------------------------------------------------
        Require(!painted(leftQuarter(0, 0)) && !painted(rightQuarter(0, 0)),
                "identity bones leave the quad centred -- neither outer quarter is painted, so a "
                "renderer that applied a stray transform is caught before anything else is read");
        Require(!painted(leftQuarter(1, 0)) && painted(rightQuarter(1, 0)),
                "a single translating bone moves the quad right by a quarter cell");
        ExpectSameRegion("a 50/50 blend of identity and a DOUBLE translation lands exactly where "
                         "one bone translating by half of it does -- the palette is summed by "
                         "weight, not selected from",
                         rightQuarter(1, 0), rightQuarter(2, 0), 2);
        Require(!painted(leftQuarter(2, 0)) && painted(rightQuarter(2, 0)),
                "and it lands there by MOVING -- the blended quad left its resting strip");
        ExpectSameRegion("bone index 71, the last slot XNA allows, moves the quad the same way "
                         "bone 0 does -- the whole palette reaches the shader",
                         rightQuarter(1, 0), rightQuarter(3, 0), 2);
        Require(painted(rightQuarter(3, 0)) && !painted(quadRect(3, 0)),
                "and that palette cell really moved rather than repeating the identity picture");

        // --- Row 1: shading -------------------------------------------------------------------
        ExpectDistinct("three directional lights differ from one", quadRect(0, 1),
                       quadRect(1, 1), 20);
        ExpectBrighter("three lights are brighter than one, since the extra two only add",
                       quadRect(1, 1), quadRect(0, 1), 10);
        ExpectBrighter("a specular highlight adds light on top of the diffuse term",
                       quadRect(2, 1), quadRect(0, 1), 10);
        ExpectDistinct("VertexColorEnabled tints the surface -- the stride-56 record's colour "
                       "reaches the shader",
                       quadRect(0, 1), quadRect(3, 1), 20);

        // --- Column 4: BLENDINDICES declared Vector4 -----------------------------------------
        ExpectSameRegion("a Vector4-declared BLENDINDICES at stride 64 selects the SAME bone as the "
                         "Byte4 one beside it -- index 71 reaches the palette either way",
                         rightQuarter(3, 0), rightQuarter(4, 0), 2);
        Require(painted(rightQuarter(4, 0)) && !painted(quadRect(4, 0)),
                "and it really moved: a renderer reading the float lane as raw bytes would select "
                "bone 0 and leave the quad on its resting strip");
        ExpectSameRegion("and the same declaration shades identically once the bones are identity, "
                         "so the rewrite carries position, normal, uv and weights unharmed",
                         quadRect(0, 1), quadRect(4, 1), 2);
    }
};

CNA_PARITY_FIXTURE_MAIN(SkinnedTermsParityFixture)
