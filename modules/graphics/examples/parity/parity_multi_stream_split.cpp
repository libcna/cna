// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-172 (harness: WEBGPU-207): a vertex split across two
// VertexBufferBindings renders the same picture as the same vertex in one buffer.
//
// XNA's `SetVertexBuffers` takes an ARRAY because a vertex's elements may live in several buffers;
// each binding carries its own `VertexDeclaration`, stride and `VertexOffset`, and the semantics
// -- not the byte layout -- say which shader input reads which bytes. The split here is the
// sharpest one available: stream 0 is POSITION only at stride 12 and stream 1 is COLOR only at
// stride 4. Neither stride is a layout any renderer recognises on its own, and their concatenation
// is exactly the packed 16-byte `VertexPositionColor` the left column draws, so:
//
//   0  one combined stride-16 buffer                    -> the reference picture
//   1  the same vertices split 12 + 4 across two slots  -> must be the SAME picture
//
// A renderer that ignored the second binding would render column 1 from a 12-byte stride that
// matches nothing (an empty column, or a colour it never supplied); one that read the colour with
// stream 0's stride would land past the end of a four-byte-per-record buffer. Neither can produce
// the left column's picture by accident, which is what makes "the same picture" a real assertion
// rather than a tolerance.
//
// The third leg is the part a two-column comparison alone would miss: `VertexOffset` is a vertex
// ELEMENT offset converted with THAT stream's own stride, so both buffers get a decoy prefix and
// column 2 addresses past it with per-stream offsets that differ in bytes (2 * 12 vs 2 * 4) while
// naming the same element.

#include "parity/ParityFixture.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 192;
    constexpr int kHeight = 64;
    constexpr int kColumns = 3;

    constexpr int kCombinedStride = 16;
    constexpr int kPositionStride = 12;
    constexpr int kColorStride = 4;
    /// Elements of decoy prefix in front of the real vertices, in the offset leg's buffers.
    constexpr int kPrefix = 2;

    const Color kClearColor(9, 13, 17, 255);
    /// Distinct in all three channels, so a wrong stream cannot land on it by rounding.
    const Color kVertexColor(40, 200, 120, 255);
    /// The decoy the prefix carries. Nowhere near kVertexColor in any channel.
    const Color kDecoyColor(220, 30, 210, 255);

    [[nodiscard]] VertexDeclaration CombinedDeclaration()
    {
        return VertexDeclaration(kCombinedStride, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
    }

    /// Stream 0 of the split pair: POSITION only. Twelve bytes is not a layout any renderer has.
    [[nodiscard]] VertexDeclaration PositionOnlyDeclaration()
    {
        return VertexDeclaration(kPositionStride, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        });
    }

    /// Stream 1 of the split pair: COLOR only, at its OWN offset zero.
    [[nodiscard]] VertexDeclaration ColorOnlyDeclaration()
    {
        return VertexDeclaration(kColorStride, {
            VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
    }
}

/// WEBGPU-172: one vertex, two bindings, the same picture.
class MultiStreamSplitParityFixture : public CNA::Parity::ParityFixture
{
public:
    MultiStreamSplitParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, 1};

        // A parity fixture asserts, it does not skip: both renderers this fixture runs against
        // implement the capability, and a renderer that stopped would be the finding.
        Require(device.SupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput),
                "the renderer must support MultiStreamVertexInput to run this fixture");

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setVertexColorEnabledProperty(true);
        effect.setDiffuseColorProperty(Vector3::One);
        effect.setEmissiveColorProperty(Vector3::Zero);
        effect.setSpecularColorProperty(Vector3::Zero);

        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.SetDepthTestEnabled(false);
        device.Clear(kClearColor);

        const std::array<std::uint16_t, 6> indexData{0, 1, 2, 0, 2, 3};
        IndexBuffer indices(device, IndexElementSize::SixteenBits,
                            static_cast<int>(indexData.size()), BufferUsage::None);
        indices.SetData(indexData.data(), static_cast<int>(indexData.size()));
        device.setIndicesProperty(&indices);

        // Column 0 -- the reference: one packed VertexPositionColor buffer.
        {
            const auto corners = grid.QuadCorners(0, 0);
            std::vector<std::uint8_t> bytes(
                static_cast<std::size_t>(4 * kCombinedStride), 0u);
            for (int corner = 0; corner < 4; ++corner)
            {
                std::uint8_t* record = bytes.data() +
                    static_cast<std::size_t>(corner) * kCombinedStride;
                const Vector3& p = corners[static_cast<std::size_t>(corner)];
                const std::array<float, 3> position{p.X, p.Y, p.Z};
                const std::uint32_t packed = kVertexColor.getPackedValueProperty();
                std::memcpy(record + 0, position.data(), sizeof(position));
                std::memcpy(record + 12, &packed, sizeof(packed));
            }
            const VertexDeclaration declaration = CombinedDeclaration();
            VertexBuffer vertices(device, declaration, 4, BufferUsage::None);
            vertices.SetDataRaw(bytes.data(), 4, kCombinedStride);
            device.SetVertexBuffer(&vertices);
            effect.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            device.SetVertexBuffer(nullptr);
        }

        // Column 1 -- the same vertices, split 12 + 4 across two bindings at offset 0.
        {
            const auto corners = grid.QuadCorners(1, 0);
            std::vector<std::uint8_t> positionBytes(
                static_cast<std::size_t>(4 * kPositionStride), 0u);
            std::vector<std::uint8_t> colorBytes(
                static_cast<std::size_t>(4 * kColorStride), 0u);
            for (int corner = 0; corner < 4; ++corner)
            {
                const Vector3& p = corners[static_cast<std::size_t>(corner)];
                const std::array<float, 3> position{p.X, p.Y, p.Z};
                const std::uint32_t packed = kVertexColor.getPackedValueProperty();
                std::memcpy(positionBytes.data() +
                                static_cast<std::size_t>(corner) * kPositionStride,
                            position.data(), sizeof(position));
                std::memcpy(colorBytes.data() + static_cast<std::size_t>(corner) * kColorStride,
                            &packed, sizeof(packed));
            }
            VertexBuffer positions(device, PositionOnlyDeclaration(), 4, BufferUsage::None);
            positions.SetDataRaw(positionBytes.data(), 4, kPositionStride);
            VertexBuffer colors(device, ColorOnlyDeclaration(), 4, BufferUsage::None);
            colors.SetDataRaw(colorBytes.data(), 4, kColorStride);
            device.SetVertexBuffers({VertexBufferBinding(&positions),
                                     VertexBufferBinding(&colors)});
            effect.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            device.SetVertexBuffer(nullptr);
        }

        // Column 2 -- split again, but each binding skips its OWN decoy prefix. The two offsets
        // name the same element and differ in bytes (2 * 12 against 2 * 4), so a renderer that
        // converted either one with the other stream's stride reads the decoy or reads past the
        // buffer.
        {
            const auto corners = grid.QuadCorners(2, 0);
            const int records = kPrefix + 4;
            std::vector<std::uint8_t> positionBytes(
                static_cast<std::size_t>(records * kPositionStride), 0u);
            std::vector<std::uint8_t> colorBytes(
                static_cast<std::size_t>(records * kColorStride), 0u);
            // The decoy prefix: a degenerate position off-screen and the decoy colour, so a
            // dropped offset is visible as either a missing quad or the wrong colour.
            for (int prefix = 0; prefix < kPrefix; ++prefix)
            {
                const std::array<float, 3> offscreen{-4.0f, -4.0f, 0.0f};
                const std::uint32_t decoy = kDecoyColor.getPackedValueProperty();
                std::memcpy(positionBytes.data() +
                                static_cast<std::size_t>(prefix) * kPositionStride,
                            offscreen.data(), sizeof(offscreen));
                std::memcpy(colorBytes.data() + static_cast<std::size_t>(prefix) * kColorStride,
                            &decoy, sizeof(decoy));
            }
            for (int corner = 0; corner < 4; ++corner)
            {
                const Vector3& p = corners[static_cast<std::size_t>(corner)];
                const std::array<float, 3> position{p.X, p.Y, p.Z};
                const std::uint32_t packed = kVertexColor.getPackedValueProperty();
                std::memcpy(positionBytes.data() +
                                static_cast<std::size_t>(kPrefix + corner) * kPositionStride,
                            position.data(), sizeof(position));
                std::memcpy(colorBytes.data() +
                                static_cast<std::size_t>(kPrefix + corner) * kColorStride,
                            &packed, sizeof(packed));
            }
            VertexBuffer positions(device, PositionOnlyDeclaration(), records, BufferUsage::None);
            positions.SetDataRaw(positionBytes.data(), records, kPositionStride);
            VertexBuffer colors(device, ColorOnlyDeclaration(), records, BufferUsage::None);
            colors.SetDataRaw(colorBytes.data(), records, kColorStride);
            device.SetVertexBuffers({VertexBufferBinding(&positions, kPrefix),
                                     VertexBufferBinding(&colors, kPrefix)});
            effect.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            device.SetVertexBuffer(nullptr);
        }

        ExpectAverage("the combined stride-16 vertex renders its declared colour",
                      grid.Interior(0, 0), kVertexColor, 2);
        ExpectAverage("the same vertex split across two bindings renders identically",
                      grid.Interior(1, 0), kVertexColor, 2);
        ExpectAverage("each binding's own VertexOffset selects the same element",
                      grid.Interior(2, 0), kVertexColor, 2);
        ExpectFlat("the split interior is flat", grid.Interior(1, 0), /*maxSpread=*/2);
        ExpectFlat("the offset interior is flat", grid.Interior(2, 0), /*maxSpread=*/2);
    }
};

CNA_PARITY_FIXTURE_MAIN(MultiStreamSplitParityFixture)
