// SPDX-License-Identifier: MS-PL
// SDLGPU-64 / plans/plan_graphics.md 1119: every `SamplerState` field supplied to
// `SpriteBatch::Begin` must reach the sprite draw. FNA's `SpriteBatch.PrepRenderState` assigns the
// complete state to `GraphicsDevice.SamplerStates[0]`; the assignment is also observable after the
// batch. CNA formerly forwarded only filter and U/V address modes through `ISpriteBatchRenderer`,
// silently discarding mip bias, mip clamp, anisotropy and AddressW on every renderer.
//
// Row 1 is the CONTROL and is deliberately read first: the same texture, the same 1/4 scale and the
// same three biases on a 3D quad, where `WEBGPU-205` did land. If the control is flat the fixture is
// broken -- a chain without distinct levels, or a scale that never leaves level 0 -- and nothing
// row 0 shows would mean anything.
//
// Row 0 is the sprite route: the same three biases handed to `SpriteBatch::Begin`, and a fourth
// column with no bias at all as the reference the other three are compared against.
//
// Row 1's last column deliberately restores bias 0 before the sprites. That keeps the row-0 result
// attributable to each batch's own state instead of to state leaked from the preceding 3D control.
//
// The mip levels are flat, distinct colours for `parity_sampler_max_mip_level`'s reason: a correct
// chain is nearly self-similar, so a test built on one cannot tell level 2 from level 3. The texture
// is 256x256 drawn into a 64x64 cell, i.e. minified 4x, so the natural level of detail is 2 and a
// bias has room to move DOWN as well as up -- a magnified quad would pin every column to level 0 and
// measure nothing.

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
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
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
    constexpr int kWidth = 256;
    constexpr int kHeight = 128;
    constexpr int kColumns = 4;
    constexpr int kRows = 2;
    /// 256x256 with a full chain: 256,128,64,32,16,8,4,2,1. Drawn into a 64x64 cell, so the natural
    /// level of detail is exactly 2 and a bias can move in either direction from it.
    constexpr int kTextureSize = 256;
    constexpr int kLevels = 9;
    constexpr int kNaturalLevel = 2;

    const Color kClearColor(9, 13, 17, 255);
    /// One flat colour per mip level, so "which level was read" is a colour, not a sharpness.
    const std::array<Color, kLevels> kLevelColors{
        Color(220, 40, 40, 255),    // level 0  (256)
        Color(40, 200, 60, 255),    // level 1  (128)
        Color(50, 80, 230, 255),    // level 2  (64)   <- the natural level here
        Color(240, 200, 30, 255),   // level 3  (32)
        Color(200, 60, 200, 255),   // level 4  (16)
        Color(60, 200, 200, 255),   // level 5  (8)
        Color(255, 255, 255, 255),  // level 6  (4)
        Color(120, 120, 120, 255),  // level 7  (2)
        Color(0, 0, 0, 255)};       // level 8  (1)

    SamplerState PointClampWithLodBias(float bias)
    {
        SamplerState state;
        state.setFilterProperty(TextureFilter::Point);
        state.setAddressUProperty(TextureAddressMode::Clamp);
        state.setAddressVProperty(TextureAddressMode::Clamp);
        state.setAddressWProperty(TextureAddressMode::Mirror);
        state.setMaxAnisotropyProperty(7);
        if (bias == 1.0f) state.setMaxMipLevelProperty(1);
        state.setMipMapLevelOfDetailBiasProperty(bias);
        return state;
    }
}

/// WEBGPU-205: whether a SamplerState field beyond filter/address reaches a SpriteBatch draw.
class SpriteSamplerStateParityFixture : public CNA::Parity::ParityFixture
{
public:
    SpriteSamplerStateParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, kRows};

        Texture2D texture(device, kTextureSize, kTextureSize, true, SurfaceFormat::Color);
        for (int level = 0; level < kLevels; ++level)
        {
            const int extent = kTextureSize >> level;
            std::vector<Color> pixels(static_cast<std::size_t>(extent) * extent,
                                      kLevelColors[static_cast<std::size_t>(level)]);
            texture.SetData(level, nullptr, pixels.data(), 0, static_cast<int>(pixels.size()));
        }

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(kClearColor);

        // --- Row 1: the 3D control, where WEBGPU-205 did land ---------------------------------
        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(true);
        effect.setTextureProperty(&texture);
        effect.setVertexColorEnabledProperty(false);

        const auto drawQuad = [&](int column, float bias)
        {
            device.getSamplerStatesProperty()[0] = PointClampWithLodBias(bias);
            // QuadCorners returns TL, BL, BR, TR -- ring order; a triangle STRIP needs the "Z"
            // order TL, BL, TR, BR.
            const auto corners = grid.QuadCorners(column, 1);
            struct Vertex { float x, y, z; float u, v; };
            const std::array<Vertex, 4> verts{
                Vertex{corners[0].X, corners[0].Y, 0.0f, 0.0f, 0.0f},
                Vertex{corners[1].X, corners[1].Y, 0.0f, 0.0f, 1.0f},
                Vertex{corners[3].X, corners[3].Y, 0.0f, 1.0f, 0.0f},
                Vertex{corners[2].X, corners[2].Y, 0.0f, 1.0f, 1.0f}};
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

        drawQuad(0,  0.0f);
        drawQuad(1,  1.0f);
        drawQuad(2, -2.0f);
        // Bias 0, and load-bearing rather than filler: this is the sampler state still bound when
        // the sprite row below is drawn. See the header.
        drawQuad(3,  0.0f);

        // --- Row 0: the sprite route ----------------------------------------------------------
        bool completeStateRetained = true;
        const auto drawSprite = [&](int column, const SamplerState& batchSampler)
        {
            const int cellW = grid.getCellWidthProperty();
            const int cellH = grid.getCellHeightProperty();
            const Rectangle destination(column * cellW, 0, cellW, cellH);
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &batchSampler, nullptr,
                        nullptr);
            batch.Draw(texture, destination, Rectangle(0, 0, kTextureSize, kTextureSize),
                       Color::White);
            batch.End();

            const SamplerState& retained = device.getSamplerStatesProperty()[0];
            completeStateRetained = completeStateRetained &&
                retained.getFilterProperty() == batchSampler.getFilterProperty() &&
                retained.getAddressUProperty() == batchSampler.getAddressUProperty() &&
                retained.getAddressVProperty() == batchSampler.getAddressVProperty() &&
                retained.getAddressWProperty() == batchSampler.getAddressWProperty() &&
                retained.getMaxAnisotropyProperty() == batchSampler.getMaxAnisotropyProperty() &&
                retained.getMaxMipLevelProperty() == batchSampler.getMaxMipLevelProperty() &&
                retained.getMipMapLevelOfDetailBiasProperty() ==
                    batchSampler.getMipMapLevelOfDetailBiasProperty();
        };

        // Columns 0-2: the bias handed to Begin(), which is where a game would put it.
        drawSprite(0, PointClampWithLodBias(0.0f));
        drawSprite(1, PointClampWithLodBias(1.0f));
        drawSprite(2, PointClampWithLodBias(-2.0f));
        // Column 3: no bias at all, the reference the other three are read against.
        drawSprite(3, SamplerState::PointClamp);

        std::printf("[%s] SpriteBatch leaves its complete sampler in GraphicsDevice slot 0\n",
                    completeStateRetained ? "PASS" : "FAIL");
        if (!completeStateRetained) MarkFailedEXT();

        // --- The control, read first ----------------------------------------------------------
        ExpectAverage("control: a 3D quad at 1/4 scale samples the natural level 2",
                      grid.Interior(0, 1), kLevelColors[kNaturalLevel], 2);
        ExpectAverage("control: bias +1 moves the 3D quad to level 3",
                      grid.Interior(1, 1), kLevelColors[kNaturalLevel + 1], 2);
        ExpectAverage("control: bias -2 moves the 3D quad to level 0",
                      grid.Interior(2, 1), kLevelColors[0], 2);
        ExpectDistinct("control: the three biases are three different levels",
                       grid.Interior(0, 1), grid.Interior(1, 1), /*minDelta=*/100);

        // --- The sprite route -----------------------------------------------------------------
        ExpectAverage("a sprite at 1/4 scale samples the natural level 2",
                      grid.Interior(0, 0), kLevelColors[kNaturalLevel], 2);
        ExpectAverage("SpriteBatch.Begin bias +1 moves the sprite to level 3",
                      grid.Interior(1, 0), kLevelColors[kNaturalLevel + 1], 2);
        ExpectAverage("SpriteBatch.Begin bias -2 moves the sprite to level 0",
                      grid.Interior(2, 0), kLevelColors[0], 2);
        ExpectDistinct("biased and unbiased batches sample different levels",
                       grid.Interior(1, 0), grid.Interior(3, 0), /*minDelta=*/100);
        ExpectSameRegion("sprite and 3D bias +1 select the same level",
                         grid.Interior(1, 0), grid.Interior(1, 1), 2);
        ExpectSameRegion("sprite and 3D bias -2 select the same level",
                         grid.Interior(2, 0), grid.Interior(2, 1), 2);
        ExpectSameRegion("an unbiased sprite and an unbiased 3D quad agree on the natural level",
                         grid.Interior(0, 0), grid.Interior(0, 1), 2);
    }
};

CNA_PARITY_FIXTURE_MAIN(SpriteSamplerStateParityFixture)
