// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-206 (harness: WEBGPU-207): a block-compressed `TextureCube` stores
// blocks, reads back decoded, and SAMPLES like the uncompressed cube it encodes.
//
// WHY THE COMPARISON IS AGAINST A SECOND CUBE, not against a hand-derived reflection vector. What
// this fixture has to prove is that a BC cube reaches the shader as the same content an RGBA8 cube
// would -- that the block format survives allocation, upload, the cube view and the sampler. Fixing
// a reflection direction by hand would prove that too, but it would also make the fixture depend on
// the exact pixel-centre and face-selection arithmetic of whichever renderer runs it, which is a
// different subject (`WEBGPU-187`) and one this fixture would then fail for the wrong reason.
//
// So both columns draw the SAME geometry through the SAME `EnvironmentMapEffect`, and differ only
// in which cube is bound:
//
//   0  the cube built from DXT1 blocks
//   1  the same six colours as an ordinary RGBA8 cube
//
// Whatever direction the reflection happens to take, both columns must take it identically. A BC
// cube that allocated RGBA8 and swallowed the blocks renders column 0 as whatever the zeroed
// allocation contains; one whose view or sampler mis-declared the format renders it differently
// from column 1. Neither can match, and the fixture does not care where the reflection points.
//
// WHICH IS EXACTLY WHY EACH QUAD IS RENDERED OFFSCREEN FIRST. A reflection depends on the surface's
// position, and two columns are at different positions by construction -- drawing them straight
// into the backbuffer made them sample two DIFFERENT faces (measured: green against red) and the
// comparison was meaningless. Each cube's quad is therefore drawn into its own `RenderTarget2D` at
// identical clip-space geometry, and the two targets are then blitted into their columns. The
// reflection maths sees one geometry; only the cube differs, which is the variable under test.
//
// THIS FIXTURE'S FRAMES ARE NOT BYTE-IDENTICAL ACROSS RENDERERS, and that is correct rather than a
// tolerance to loosen. Its oracle is INTERNAL: within one renderer, the BC cube must match the RGBA8
// cube. Across renderers the reflection DIRECTION differs -- measured, EasyGL (125,130,64) against
// WebGPU (128,128,64), and at the frame's edge a whole face apart -- because face selection depends
// on the pixel-centre convention, which is `WEBGPU-187`'s subject and the one thing this fixture
// deliberately does not test. Both renderers pass every assertion below; only a raw frame diff
// between them is inapplicable. Do not "fix" that by widening a tolerance until it passes: the
// assertions are the contract, and the frame dump is a diagnostic.
//
// The DXT1 blocks are solid-colour: colour0 == colour1 and all sixteen index bits zero, so every
// texel of the block is colour0. That makes the decoded value exact rather than interpolated, which
// is what lets the two columns be compared at a tolerance that would not hide a real difference.

#include "parity/ParityFixture.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 128;
    constexpr int kHeight = 64;
    constexpr int kColumns = 2;
    /// One 4x4 DXT1 block per face -- the smallest legal block-compressed cube.
    constexpr int kCubeSize = 4;

    const Color kClearColor(9, 13, 17, 255);

    /// A DXT1 block whose two endpoints are the same RGB565 value and whose index bits are all
    /// zero: every one of its sixteen texels decodes to that colour exactly.
    [[nodiscard]] std::array<std::uint8_t, 8> SolidDxt1Block(std::uint16_t rgb565)
    {
        const auto lo = static_cast<std::uint8_t>(rgb565 & 0xFF);
        const auto hi = static_cast<std::uint8_t>((rgb565 >> 8) & 0xFF);
        return {lo, hi, lo, hi, 0, 0, 0, 0};
    }

    /// The six faces, as RGB565 and as the RGBA8 those blocks decode to. 5- and 6-bit channels
    /// expand by replication (31 -> 255, 63 -> 255), so these pairs are exact, not approximate.
    struct FaceColor { std::uint16_t packed565; Color decoded; };
    const std::array<FaceColor, 6> kFaces{
        FaceColor{0xF800, Color(255, 0, 0, 255)},      // +X red
        FaceColor{0x07E0, Color(0, 255, 0, 255)},      // -X green
        FaceColor{0x001F, Color(0, 0, 255, 255)},      // +Y blue
        FaceColor{0xFFE0, Color(255, 255, 0, 255)},    // -Y yellow
        FaceColor{0xF81F, Color(255, 0, 255, 255)},    // +Z magenta
        FaceColor{0x07FF, Color(0, 255, 255, 255)},    // -Z cyan
    };

    constexpr std::array<CubeMapFace, 6> kFaceOrder{
        CubeMapFace::PositiveX, CubeMapFace::NegativeX, CubeMapFace::PositiveY,
        CubeMapFace::NegativeY, CubeMapFace::PositiveZ, CubeMapFace::NegativeZ};
}

/// WEBGPU-206: a DXT1 cube samples identically to the RGBA8 cube it encodes.
class CompressedCubeParityFixture : public CNA::Parity::ParityFixture
{
public:
    CompressedCubeParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, 1};

        Require(device.GetRenderer().IsCompressedCubeTransferFormatEXT(
                    static_cast<int>(SurfaceFormat::Dxt1)),
                "the renderer must store a compressed TextureCube to run this fixture");

        // The compressed cube, from blocks.
        TextureCube compressed(device, kCubeSize, false, SurfaceFormat::Dxt1);
        for (std::size_t face = 0; face < kFaceOrder.size(); ++face)
        {
            const auto block = SolidDxt1Block(kFaces[face].packed565);
            compressed.SetData(kFaceOrder[face], block.data(),
                               static_cast<int>(block.size()));
        }

        // The same six colours, uncompressed. This is the oracle: not a value written into this
        // file, but the content the blocks encode, built through the path that has always worked.
        TextureCube plain(device, kCubeSize, false, SurfaceFormat::Color);
        for (std::size_t face = 0; face < kFaceOrder.size(); ++face)
        {
            std::array<Color, kCubeSize * kCubeSize> texels{};
            texels.fill(kFaces[face].decoded);
            plain.SetData(kFaceOrder[face], texels.data(), static_cast<int>(texels.size()));
        }

        // Leg 1, before anything is drawn: the blocks read back DECODED, matching the oracle. This
        // is the half a rendered comparison cannot isolate -- two columns could agree because both
        // cubes are wrong in the same way.
        {
            std::array<Color, kCubeSize * kCubeSize> readBack{};
            compressed.GetData(CubeMapFace::NegativeZ, readBack.data(),
                               static_cast<int>(readBack.size()));
            bool exact = true;
            for (const Color& texel : readBack)
                if (texel != kFaces[5].decoded) exact = false;
            Require(exact, "a DXT1 face reads back as the colour its blocks encode");
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

        // One geometry, rendered offscreen, so the reflection is identical for both cubes.
        const auto renderReflection = [&](TextureCube& cube, RenderTarget2D& into)
        {
            device.SetRenderTarget(&into);
            device.setRasterizerStateProperty(rs);
            device.setBlendStateProperty(BlendState::Opaque);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.getSamplerStatesProperty()[0] = pointClamp;
            device.Clear(kClearColor);

            EnvironmentMapEffect effect(device);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::getIdentityProperty());
            effect.setEnvironmentMapProperty(&cube);
            effect.setEnvironmentMapAmountProperty(1.0f);
            effect.setFresnelFactorProperty(0.0f);
            effect.setDiffuseColorProperty(Vector3::Zero);

            struct Vertex { float x, y, z; float nx, ny, nz; float u, v; };
            // A full-target quad in clip space, identical for both cubes. Normals face the camera.
            const std::array<Vertex, 4> verts{
                Vertex{-1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
                Vertex{-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
                Vertex{ 1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f},
                Vertex{ 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f}};
            VertexBuffer vb(device,
                            VertexDeclaration(32,
                                {VertexElement(0, VertexElementFormat::Vector3,
                                               VertexElementUsage::Position, 0),
                                 VertexElement(12, VertexElementFormat::Vector3,
                                               VertexElementUsage::Normal, 0),
                                 VertexElement(24, VertexElementFormat::Vector2,
                                               VertexElementUsage::TextureCoordinate, 0)}),
                            static_cast<int>(verts.size()), BufferUsage::None);
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), 32);
            device.SetVertexBuffer(&vb);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
            device.SetVertexBuffer(nullptr);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        };

        const int cell = grid.getCellWidthProperty();
        RenderTarget2D fromBlocks(device, cell, kHeight, false, SurfaceFormat::Color,
                                  DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        RenderTarget2D fromTexels(device, cell, kHeight, false, SurfaceFormat::Color,
                                  DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        renderReflection(compressed, fromBlocks);
        renderReflection(plain, fromTexels);

        device.Clear(kClearColor);
        {
            SpriteBatch batch(device);
            batch.Begin();
            batch.Draw(fromBlocks, Rectangle(0, 0, cell, kHeight),
                       Rectangle(0, 0, cell, kHeight), Color::White);
            batch.Draw(fromTexels, Rectangle(cell, 0, cell, kHeight),
                       Rectangle(0, 0, cell, kHeight), Color::White);
            batch.End();
        }

        // Both columns must have rendered something: two empty columns compare equal, so the
        // agreement below would pass on a pair of blank frames. Checked against the CLEAR COLOUR
        // rather than against another region -- the two blits cover the whole frame, so there is no
        // background region left to compare with, and a check that compared one part of the
        // reflection to another would be measuring nothing.
        {
            const Color reflected = Average(grid.Interior(0, 0));
            const int delta = std::max({std::abs(reflected.getRProperty() - kClearColor.getRProperty()),
                                        std::abs(reflected.getGProperty() - kClearColor.getGProperty()),
                                        std::abs(reflected.getBProperty() - kClearColor.getBProperty())});
            std::printf("[%s] the compressed cube reflected something: (%d,%d,%d) against clear "
                        "(%d,%d,%d), delta %d\n", delta >= 40 ? "PASS" : "FAIL",
                        reflected.getRProperty(), reflected.getGProperty(), reflected.getBProperty(),
                        kClearColor.getRProperty(), kClearColor.getGProperty(),
                        kClearColor.getBProperty(), delta);
            if (delta < 40) MarkFailedEXT();
        }
        // THE ASSERTION. Same geometry, same effect, same sampler -- only the cube's storage
        // differs, so any difference here is the block format failing to reach the shader.
        ExpectSameRegion("a DXT1 cube samples exactly like the RGBA8 cube it encodes",
                         grid.Interior(0, 0), grid.Interior(1, 0), /*tolerance=*/2);
    }
};

CNA_PARITY_FIXTURE_MAIN(CompressedCubeParityFixture)
