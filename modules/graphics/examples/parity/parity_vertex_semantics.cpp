// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-155 (harness: WEBGPU-207): one mesh, four vertex declarations that
// differ only in element ORDER, element OFFSET and buffer STRIDE, and therefore must render
// identically.
//
// The four legs are the four independent ways an XNA vertex declaration can legally describe the
// same semantic content:
//
//   A  canonical VertexPositionColorTexture placement -- Position@0, Color@12, TexCoord@16, stride 24
//   B  the same stride with the semantics MOVED       -- TexCoord@0, Position@8, Color@20, stride 24
//   C  the same placement PADDED to another stride    -- Position@0, Color@12, TexCoord@16, stride 32
//   D  the same placement, declaration list REORDERED -- elements listed Color, TexCoord, Position
//
// A renderer that derives its input layout from the byte stride gets A right and B, C and D wrong:
// B is read from the wrong offsets, C is a different stride's canonical layout entirely (32 is
// VertexPositionNormalTexture's), and D proves the binding uses the element's usage rather than its
// index in the list. A renderer that follows the declaration gets four identical columns.
//
// Each column draws four flat, single-colour quads -- every vertex of a quad carries that quad's
// colour, so there is no interpolation and the expected value is exact rather than a shade.

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 256;
    constexpr int kHeight = 128;
    constexpr int kColumns = 4;
    constexpr int kRows = 4;

    /// Non-neutral in every channel, so a colour read from the wrong offset cannot coincide.
    constexpr std::array<std::array<int, 3>, kRows> kRowColors{{
        {{220,  40,  40}},
        {{ 40, 200,  60}},
        {{ 50,  80, 230}},
        {{240, 200,  30}},
    }};

    const Color kClearColor(9, 13, 17, 255);

    /// Where one semantic sits inside one leg's vertex record.
    struct Placement { int position, color, texCoord, stride; };

    /// One leg: how the bytes are laid out, and how the declaration spells that layout.
    struct Leg
    {
        const char* name;
        Placement placement;
        /// The order the elements appear in the VertexDeclaration's own list.
        std::array<int, 3> declarationOrder;  // 0 = Position, 1 = Color, 2 = TextureCoordinate
    };

    constexpr std::array<Leg, kColumns> kLegs{{
        {"A-canonical",   {0, 12, 16, 24}, {{0, 1, 2}}},
        {"B-moved",       {8, 20,  0, 24}, {{0, 1, 2}}},
        {"C-padded",      {0, 12, 16, 32}, {{0, 1, 2}}},
        {"D-reordered",   {0, 12, 16, 24}, {{1, 2, 0}}},
    }};

    [[nodiscard]] VertexDeclaration DeclarationFor(const Leg& leg)
    {
        const std::array<VertexElement, 3> byUsage{{
            VertexElement(leg.placement.position, VertexElementFormat::Vector3,
                          VertexElementUsage::Position, 0),
            VertexElement(leg.placement.color, VertexElementFormat::Color,
                          VertexElementUsage::Color, 0),
            VertexElement(leg.placement.texCoord, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        }};
        std::vector<VertexElement> elements;
        elements.reserve(byUsage.size());
        for (int index : leg.declarationOrder) elements.push_back(byUsage[static_cast<std::size_t>(index)]);
        return VertexDeclaration(leg.placement.stride, elements);
    }

    /// Writes one vertex into @p record at this leg's own offsets. Padding stays zero.
    void WriteVertex(std::uint8_t* record, const Leg& leg, const Vector3& position,
                     const Color& color, const Vector2& texCoord)
    {
        const std::array<float, 3> pos{position.X, position.Y, position.Z};
        std::memcpy(record + leg.placement.position, pos.data(), sizeof(pos));
        const std::uint32_t packed = color.getPackedValueProperty();
        std::memcpy(record + leg.placement.color, &packed, sizeof(packed));
        const std::array<float, 2> uv{texCoord.X, texCoord.Y};
        std::memcpy(record + leg.placement.texCoord, uv.data(), sizeof(uv));
    }
}

/// WEBGPU-155: declaration order, element offsets and buffer stride must not change the pixels.
class VertexSemanticsParityFixture : public CNA::Parity::ParityFixture
{
public:
    VertexSemanticsParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, kRows};

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setVertexColorEnabledProperty(true);

        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.SetDepthTestEnabled(false);
        device.Clear(kClearColor);

        // 4 quads per column, 4 vertices and 6 indices each.
        std::array<std::uint16_t, kRows * 6> indexData{};
        for (int quad = 0; quad < kRows; ++quad)
        {
            const std::uint16_t base = static_cast<std::uint16_t>(quad * 4);
            const std::array<std::uint16_t, 6> quadIndices{
                base, static_cast<std::uint16_t>(base + 1), static_cast<std::uint16_t>(base + 2),
                base, static_cast<std::uint16_t>(base + 2), static_cast<std::uint16_t>(base + 3)};
            std::copy(quadIndices.begin(), quadIndices.end(),
                      indexData.begin() + static_cast<std::ptrdiff_t>(quad) * 6);
        }
        IndexBuffer indices(device, IndexElementSize::SixteenBits,
                            static_cast<int>(indexData.size()), BufferUsage::None);
        indices.SetData(indexData.data(), static_cast<int>(indexData.size()));
        device.setIndicesProperty(&indices);

        for (int column = 0; column < kColumns; ++column)
        {
            const Leg& leg = kLegs[static_cast<std::size_t>(column)];
            const int vertexCount = kRows * 4;
            std::vector<std::uint8_t> bytes(
                static_cast<std::size_t>(vertexCount) * static_cast<std::size_t>(leg.placement.stride), 0u);
            for (int row = 0; row < kRows; ++row)
            {
                const auto corners = grid.QuadCorners(column, row);
                const auto& rgb = kRowColors[static_cast<std::size_t>(row)];
                const Color color(rgb[0], rgb[1], rgb[2], 255);
                // Deliberately non-trivial UVs: an implementation that binds TEXCOORD where the
                // colour lives would corrupt the colour, and vice versa.
                const std::array<Vector2, 4> uvs{
                    Vector2(0.0f, 0.0f), Vector2(0.0f, 1.0f),
                    Vector2(1.0f, 1.0f), Vector2(1.0f, 0.0f)};
                for (int corner = 0; corner < 4; ++corner)
                {
                    std::uint8_t* record = bytes.data() +
                        static_cast<std::size_t>(row * 4 + corner) *
                        static_cast<std::size_t>(leg.placement.stride);
                    WriteVertex(record, leg, corners[static_cast<std::size_t>(corner)], color,
                                uvs[static_cast<std::size_t>(corner)]);
                }
            }

            const VertexDeclaration declaration = DeclarationFor(leg);
            VertexBuffer vertices(device, declaration, vertexCount, BufferUsage::None);
            vertices.SetDataRaw(bytes.data(), vertexCount, leg.placement.stride);
            device.SetVertexBuffer(&vertices);
            effect.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, vertexCount, 0,
                                         kRows * 2);
            device.SetVertexBuffer(nullptr);
        }

        // Every cell must carry its own row colour, exactly. Tolerance 1 covers only the
        // unorm8 -> f32 -> unorm8 round trip a colour attribute makes on the way to the
        // framebuffer; nothing in this scene is lit, filtered or blended.
        for (int column = 0; column < kColumns; ++column)
        {
            for (int row = 0; row < kRows; ++row)
            {
                const auto& rgb = kRowColors[static_cast<std::size_t>(row)];
                const std::string label = std::string(kLegs[static_cast<std::size_t>(column)].name) +
                                          " row " + std::to_string(row);
                ExpectAverage(label.c_str(), grid.Interior(column, row),
                              Color(rgb[0], rgb[1], rgb[2], 255), /*tolerance=*/1);
                ExpectFlat((label + " is a flat interior").c_str(), grid.Interior(column, row),
                           /*maxSpread=*/1);
            }
        }

        // A leg that rendered nothing would pass no ExpectAverage above, but say so explicitly:
        // the cleared background must still be visible in the margin between two columns, which
        // proves the quads are where the fixture thinks they are rather than covering the frame.
        ExpectAverage("column margin keeps the clear colour",
                      Rectangle(grid.getCellWidthProperty() - 1, grid.getCellHeightProperty() - 1, 2, 2),
                      kClearColor, /*tolerance=*/1);
    }
};

CNA_PARITY_FIXTURE_MAIN(VertexSemanticsParityFixture)
