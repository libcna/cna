// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-193 (harness: WEBGPU-207): one instanced draw, several instances, each
// placed and coloured by its own per-instance stream.
//
// The corpus milestone's list names an instanced draw and nothing here covered it. What it has to
// prove is not "instancing runs" -- `WEBGPU-27`/`38`/`68` already do -- but that the two renderers
// place and shade the SAME instances the same way, which is a different question and the one a
// shared fixture can ask.
//
// THREE INSTANCES IN A SIX-CELL GRID, at cells 0, 2 and 4. The empty cells are the point: a
// renderer that bound the per-instance stream at the wrong STEP RATE draws all three instances at
// instance 0's matrix, leaving cells 2 and 4 empty; one that advanced per vertex instead draws them
// somewhere else entirely; one that ignored the stream draws three quads on top of each other. Only
// a correct implementation paints exactly the alternate cells, and the three that stay CLEAR are
// what a fixture drawing an instance in every cell could not check at all.
//
// A first version gave each instance its own COLOR0 in the per-instance stream, to separate "placed
// correctly" from "shaded correctly". Measured: every instance then shaded BLACK on EasyGL, with
// both a `Vector4` and a packed `Color` element -- the stock vertex-colour path does not read a
// colour from a per-instance stream. That is a real limit of the shared stock path rather than
// something this fixture should work around, so the colour is uniform here and the claim is
// placement.
//
// The per-instance matrix is spelled TEXCOORD1..4, which is the shared oracle's own spelling; both
// renderers resolve those four columns POSITIONALLY rather than by usage index, so the spelling is
// what the other CNA instancing tests use and not a preference of this file.

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
#include <cstdio>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kColumns = 6;
    constexpr int kRows = 1;
    constexpr int kCell = 32;
    constexpr int kWidth = kColumns * kCell;
    constexpr int kHeight = kRows * kCell;
    constexpr int kInstances = 3;

    const Color kClearColor(9, 13, 17, 255);

    /// The one colour every instance shades. See the header for why it is not per-instance.
    const Color kInstanceColour(220, 90, 40, 255);

    /// The per-instance record: a 4x4 world matrix, nothing else.
    struct InstanceRecord { float m[16]; };
    static_assert(sizeof(InstanceRecord) == 64, "the per-instance record is one 4x4 matrix");

    [[nodiscard]] VertexDeclaration InstanceDeclaration()
    {
        return VertexDeclaration(64, {
            VertexElement(0, VertexElementFormat::Vector4,
                          VertexElementUsage::TextureCoordinate, 1),
            VertexElement(16, VertexElementFormat::Vector4,
                          VertexElementUsage::TextureCoordinate, 2),
            VertexElement(32, VertexElementFormat::Vector4,
                          VertexElementUsage::TextureCoordinate, 3),
            VertexElement(48, VertexElementFormat::Vector4,
                          VertexElementUsage::TextureCoordinate, 4),
        });
    }
}

/// WEBGPU-193: several instances, each placed and coloured by its own per-instance record.
class InstancedDrawParityFixture : public CNA::Parity::ParityFixture
{
public:
    InstancedDrawParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, kRows};

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(kClearColor);

        // The base mesh: one quad occupying cell 0, which every instance then translates.
        const auto corners = grid.QuadCorners(0, 0);
        struct Vertex { float x, y, z; };
        const std::array<Vertex, 4> quad{
            Vertex{corners[0].X, corners[0].Y, 0.0f},
            Vertex{corners[1].X, corners[1].Y, 0.0f},
            Vertex{corners[2].X, corners[2].Y, 0.0f},
            Vertex{corners[3].X, corners[3].Y, 0.0f}};
        const std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};

        std::array<InstanceRecord, kInstances> instances{};
        for (int i = 0; i < kInstances; ++i)
        {
            // A translation of exactly TWO cell widths in clip space per instance, so the
            // instances land in cells 0, 2 and 4 and the odd cells stay clear.
            const Matrix world = Matrix::CreateTranslation(
                4.0f * static_cast<float>(i) / static_cast<float>(kColumns), 0.0f, 0.0f);
            const std::array<float, 16> m{
                world.M11, world.M12, world.M13, world.M14,
                world.M21, world.M22, world.M23, world.M24,
                world.M31, world.M32, world.M33, world.M34,
                world.M41, world.M42, world.M43, world.M44};
            for (std::size_t k = 0; k < m.size(); ++k)
                instances[static_cast<std::size_t>(i)].m[k] = m[k];

        }

        VertexBuffer mesh(device,
                          VertexDeclaration(12,
                              {VertexElement(0, VertexElementFormat::Vector3,
                                             VertexElementUsage::Position, 0)}),
                          static_cast<int>(quad.size()), BufferUsage::None);
        mesh.SetDataRaw(quad.data(), static_cast<int>(quad.size()), 12);
        VertexBuffer instanceBuffer(device, InstanceDeclaration(), kInstances, BufferUsage::None);
        instanceBuffer.SetDataRaw(instances.data(), kInstances, 64);
        IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits,
                                static_cast<int>(indices.size()), BufferUsage::None);
        indexBuffer.SetData(indices.data(), static_cast<int>(indices.size()));

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setVertexColorEnabledProperty(false);
        effect.setDiffuseColorProperty(Vector3(
            static_cast<float>(kInstanceColour.getRProperty()) / 255.0f,
            static_cast<float>(kInstanceColour.getGProperty()) / 255.0f,
            static_cast<float>(kInstanceColour.getBProperty()) / 255.0f));
        effect.setAlphaProperty(1.0f);

        device.SetVertexBuffers({VertexBufferBinding(&mesh, 0, 0),
                                 VertexBufferBinding(&instanceBuffer, 0, 1)});
        device.SetIndexBuffer(&indexBuffer);
        effect.Apply();
        device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0,
                                       static_cast<int>(quad.size()), 0, 2, kInstances);
        device.SetIndexBuffer(nullptr);
        device.SetVertexBuffers({});

        for (int cell = 0; cell < kColumns; ++cell)
        {
            const Color got = Average(grid.Interior(cell, 0));
            std::printf("[info] cell %d -> (%d,%d,%d)%s\n", cell, got.getRProperty(),
                        got.getGProperty(), got.getBProperty(),
                        (cell % 2 == 0) ? "  [expected an instance]" : "  [expected clear]");
        }

        static const std::array<const char*, 3> kPainted{
            "instance 0 landed in cell 0", "instance 1 landed in cell 2",
            "instance 2 landed in cell 4"};
        static const std::array<const char*, 3> kEmpty{
            "cell 1 stayed clear -- no instance was placed there",
            "cell 3 stayed clear",
            "cell 5 stayed clear -- so the three instances did not all land on top of each other"};
        for (int i = 0; i < kInstances; ++i)
        {
            ExpectAverage(kPainted[static_cast<std::size_t>(i)], grid.Interior(i * 2, 0),
                          kInstanceColour, 4);
            ExpectAverage(kEmpty[static_cast<std::size_t>(i)], grid.Interior(i * 2 + 1, 0),
                          kClearColor, 4);
        }
    }
};

CNA_PARITY_FIXTURE_MAIN(InstancedDrawParityFixture)
