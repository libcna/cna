// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-153 (harness: WEBGPU-207): `FillMode::WireFrame` draws a triangle's
// EDGES and leaves its interior empty -- and `FillMode::Solid`, the same geometry through the same
// route, fills it.
//
// Both columns are in one frame on purpose. "The interior is empty" is not by itself evidence of a
// wireframe: a draw that was dropped, culled or refused produces the same empty interior. The Solid
// column is the control that separates them, and it is asserted as hard as the wireframe column.
//
// THE TRIANGLE IS ASYMMETRIC ON PURPOSE, following REMED-GFX-209's oracle: no two edges share a
// slope, none is axis-aligned, and the three edge probes are mutually disjoint and each contains
// exactly one edge -- so a single missing edge drives a KNOWN probe to zero instead of merely
// changing a total. A symmetric triangle would let one dropped edge hide behind the other two.
//
// The clear colour is deliberately not black: against black, "the draw was dropped" and "the
// geometry rendered with a lost colour attribute" are the same picture.
//
// THIS FIXTURE IS JUDGED BY ITS ASSERTIONS, NOT BY THE WHOLE-FRAME DUMP. Measured 2026-09-05,
// EasyGL vs WebGPU: 367 of 32768 pixels differ (1.12%) -- 120 on the solid triangle's edges and 247
// on the wireframe's lines, and NONE anywhere else. A picture made entirely of one-pixel lines is
// all boundary, and boundary is exactly where two conforming rasterizers are allowed to disagree
// (WEBGPU-123 measured the same thing about a filled triangle). So `run-parity-fixture.sh` on this
// fixture would need a tolerance wide enough to be meaningless; the interior probes and the
// per-edge coverage counts below are the comparison instead, and they agree exactly.

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <array>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 256;
    constexpr int kHeight = 128;
    constexpr int kCell = 128;

    const Color kClearColor(9, 13, 17, 255);
    /// Non-neutral in every channel, so a colour read from the wrong place is measurable.
    const Color kInk(255, 96, 32, 255);

    /// The triangle's corners inside a cell, in cell-local pixels. Centroid (58.7, 73.3).
    constexpr std::array<std::array<int, 2>, 3> kCorners{
        std::array<int, 2>{16, 112}, std::array<int, 2>{112, 96}, std::array<int, 2>{48, 12}};

    /// A 13x13 box on the centroid. Every edge is at least 22 px away, so a one-pixel wireframe
    /// cannot reach it and a solid fill cannot miss it.
    [[nodiscard]] Rectangle Interior(int column)
    {
        return Rectangle(column * kCell + 52, 67, 13, 13);
    }

    /// One 9x9 probe per edge, centred on that edge's midpoint: AB(64,104), BC(80,54), CA(32,62).
    /// Verified disjoint, and verified to contain exactly one edge each.
    [[nodiscard]] Rectangle EdgeProbe(int column, int edge)
    {
        constexpr std::array<std::array<int, 2>, 3> kMid{
            std::array<int, 2>{64, 104}, std::array<int, 2>{80, 54}, std::array<int, 2>{32, 62}};
        return Rectangle(column * kCell + kMid[static_cast<std::size_t>(edge)][0] - 4,
                         kMid[static_cast<std::size_t>(edge)][1] - 4, 9, 9);
    }

    constexpr std::array<const char*, 3> kEdgeNames{"AB", "BC", "CA"};
}

/// WEBGPU-153: a wireframe triangle is its three edges, and a solid one is its interior.
class FillModeWireframeParityFixture : public CNA::Parity::ParityFixture
{
public:
    FillModeWireframeParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setFogEnabledProperty(false);
        effect.setVertexColorEnabledProperty(true);

        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(kClearColor);

        const auto drawColumn = [&](int column, FillMode fill)
        {
            std::array<VertexPositionColor, 3> verts{};
            for (std::size_t i = 0; i < verts.size(); ++i)
            {
                const float px = static_cast<float>(column * kCell + kCorners[i][0]);
                const float py = static_cast<float>(kCorners[i][1]);
                verts[i] = VertexPositionColor(
                    Vector3(2.0f * px / static_cast<float>(kWidth) - 1.0f,
                            1.0f - 2.0f * py / static_cast<float>(kHeight), 0.0f),
                    kInk);
            }
            VertexBuffer vb(device,
                            VertexDeclaration(16,
                                {VertexElement(0, VertexElementFormat::Vector3,
                                               VertexElementUsage::Position, 0),
                                 VertexElement(12, VertexElementFormat::Color,
                                               VertexElementUsage::Color, 0)}),
                            static_cast<int>(verts.size()), BufferUsage::None);
            vb.SetData(verts.data(), static_cast<int>(verts.size()));

            // CullMode::None is load-bearing: culling must never be the reason a pixel is missing,
            // or the two columns would be measuring winding rather than fill mode.
            RasterizerState rs;
            rs.setCullModeProperty(CullMode::None);
            rs.setFillModeProperty(fill);
            device.setRasterizerStateProperty(rs);

            device.SetVertexBuffer(&vb);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            device.SetVertexBuffer(nullptr);
        };

        drawColumn(0, FillMode::Solid);
        drawColumn(1, FillMode::WireFrame);

        // The control: Solid fills its interior completely, with the ink colour.
        ExpectLitCount("Solid fills the whole interior", Interior(0), kClearColor, 169, 169);
        ExpectAverage("Solid's interior carries the ink colour", Interior(0), kInk, 2);

        // The measurement that separates a wireframe from a solid fill, and the only one that
        // can: WireFrame leaves the interior completely untouched.
        ExpectLitCount("WireFrame leaves the interior empty", Interior(1), kClearColor, 0, 0);

        // ...and it is a wireframe rather than a dropped draw: all THREE edges are present. A
        // probe that reads zero names which edge went missing.
        for (int edge = 0; edge < 3; ++edge)
        {
            const std::string solidLabel =
                std::string("Solid covers edge ") + kEdgeNames[static_cast<std::size_t>(edge)];
            const std::string wireLabel =
                std::string("WireFrame draws edge ") + kEdgeNames[static_cast<std::size_t>(edge)];
            ExpectLitCount(solidLabel.c_str(), EdgeProbe(0, edge), kClearColor, 4, 81);
            ExpectLitCount(wireLabel.c_str(), EdgeProbe(1, edge), kClearColor, 4, 81);
        }

        // The two columns must not be the same picture. A renderer that accepted the wireframe
        // request and quietly filled anyway passes every edge check above and fails here.
        ExpectDistinct("Solid and WireFrame are different pictures",
                       Interior(0), Interior(1), /*minDelta=*/100);
    }
};

CNA_PARITY_FIXTURE_MAIN(FillModeWireframeParityFixture)
