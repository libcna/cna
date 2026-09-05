// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-161 (harness: WEBGPU-207): `SamplerState.MaxMipLevel` must reach the
// sampler and change which mip level is read.
//
// XNA's `MaxMipLevel` is the HIGHEST-RESOLUTION mip level sampling may use -- 0 means no
// restriction, 2 means "never read level 0 or 1". It is therefore a lower clamp on the computed
// level of detail: `GL_TEXTURE_MIN_LOD` on the reference renderer, `min_lod` in FNA3D's SDL_GPU
// driver, `WGPUSamplerDescriptor::lodMinClamp` here. `ApplySamplerMipState` was not overridden on
// WebGPU, so the value was discarded in silence.
//
// HOW THE LEVELS ARE MADE DISTINGUISHABLE. Each mip level is filled with its own flat colour, so
// "which level was read" is answered by a colour rather than by sharpness -- level 0 red, level 1
// green, level 2 blue, level 3 yellow. That is not what a real mip chain looks like, and that is
// the point: a correct chain is nearly self-similar, so a test built on one cannot tell level 1
// from level 0 at all.
//
// THE QUAD IS DRAWN AT 1:1, so the natural level of detail is 0 and every difference below is
// attributable to the clamp and to nothing else. A minified quad would confound the clamp with the
// renderer's own LOD computation.
//
// The filter is Point (mip Nearest) so a level is SELECTED rather than blended with its neighbour;
// with a linear mip filter both columns would read a mixture and neither colour would be exact.

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
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 128;
    constexpr int kHeight = 64;
    constexpr int kColumns = 2;
    /// 8x8 with a full chain: 8, 4, 2, 1.
    constexpr int kTextureSize = 8;
    constexpr int kLevels = 4;

    const Color kClearColor(9, 13, 17, 255);
    /// One flat colour per mip level, so "which level was read" is a colour, not a sharpness.
    const std::array<Color, kLevels> kLevelColors{
        Color(220, 40, 40, 255),    // level 0
        Color(40, 200, 60, 255),    // level 1
        Color(50, 80, 230, 255),    // level 2
        Color(240, 200, 30, 255)};  // level 3

    SamplerState PointClampWithMaxMipLevel(int maxMipLevel)
    {
        SamplerState state;
        state.setFilterProperty(TextureFilter::Point);
        state.setAddressUProperty(TextureAddressMode::Clamp);
        state.setAddressVProperty(TextureAddressMode::Clamp);
        state.setMaxMipLevelProperty(maxMipLevel);
        return state;
    }
}

/// WEBGPU-161: MaxMipLevel selects which mip level a sample comes from.
class SamplerMaxMipLevelParityFixture : public CNA::Parity::ParityFixture
{
public:
    SamplerMaxMipLevelParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, 1};

        Texture2D texture(device, kTextureSize, kTextureSize, true, SurfaceFormat::Color);
        for (int level = 0; level < kLevels; ++level)
        {
            const int extent = kTextureSize >> level;
            std::vector<Color> pixels(static_cast<std::size_t>(extent) * extent,
                                      kLevelColors[static_cast<std::size_t>(level)]);
            texture.SetData(level, nullptr, pixels.data(), 0, static_cast<int>(pixels.size()));
        }

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(true);
        effect.setTextureProperty(&texture);
        effect.setVertexColorEnabledProperty(false);

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(kClearColor);

        const auto drawColumn = [&](int column, int maxMipLevel)
        {
            device.getSamplerStatesProperty()[0] = PointClampWithMaxMipLevel(maxMipLevel);

            // ParityGrid::QuadCorners returns TL, BL, BR, TR -- ring order. A triangle STRIP needs
            // the "Z" order TL, BL, TR, BR; feeding it the ring draws (TL,BL,BR) and (BL,BR,TR),
            // which is not the quad and leaves a quarter of it uncovered.
            const auto corners = grid.QuadCorners(column, 0);
            struct Vertex { float x, y, z; float u, v; };
            const std::array<Vertex, 4> verts{
                Vertex{corners[0].X, corners[0].Y, 0.0f, 0.0f, 0.0f},   // top-left
                Vertex{corners[1].X, corners[1].Y, 0.0f, 0.0f, 1.0f},   // bottom-left
                Vertex{corners[3].X, corners[3].Y, 0.0f, 1.0f, 0.0f},   // top-right
                Vertex{corners[2].X, corners[2].Y, 0.0f, 1.0f, 1.0f}};  // bottom-right
            VertexBuffer vb(device,
                            VertexDeclaration(20,
                                {VertexElement(0, VertexElementFormat::Vector3,
                                               VertexElementUsage::Position, 0),
                                 VertexElement(12, VertexElementFormat::Vector2,
                                               VertexElementUsage::TextureCoordinate, 0)}),
                            static_cast<int>(verts.size()), BufferUsage::None);
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), 20);
            device.SetVertexBuffer(&vb);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
            device.SetVertexBuffer(nullptr);
        };

        drawColumn(0, 0);   // no restriction -- the natural level, which at 1:1 is 0
        drawColumn(1, 2);   // never finer than level 2

        // Each level is a flat colour and the filter selects rather than blends, so both are exact;
        // tolerance 2 covers the unorm8 round trip and nothing else.
        ExpectAverage("MaxMipLevel 0 samples level 0", grid.Interior(0, 0), kLevelColors[0], 2);
        ExpectAverage("MaxMipLevel 2 samples level 2", grid.Interior(1, 0), kLevelColors[2], 2);
        // The state has to MATTER: a renderer that discarded MaxMipLevel produces the same colour
        // in both columns and passes neither of the two checks above by accident.
        ExpectDistinct("MaxMipLevel materially changes which level is read",
                       grid.Interior(0, 0), grid.Interior(1, 0), /*minDelta=*/100);
        ExpectFlat("the clamped column is a flat level, not a blend of two",
                   grid.Interior(1, 0), /*maxSpread=*/2);
    }
};

CNA_PARITY_FIXTURE_MAIN(SamplerMaxMipLevelParityFixture)
