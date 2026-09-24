// SPDX-License-Identifier: MS-PL
//
// plans/plan_street_perf.md STREETPERF-0002: a draw renders the vertex and index data its buffers
// held when the draw was issued, even when the game rewrites those buffers before the frame is
// presented. Immediate renderers get this for free; a renderer that records draws and replays
// them at Present() has to keep what each draw was issued with. SDL_GPU used to copy every
// draw's whole buffer to do it (~430 MB a frame in cna-street) and now binds the buffers' own
// storage, moving a buffer to fresh storage when it is rewritten under a queued draw. These
// cases are what that move exists for, on every renderer.

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    constexpr int kWidth = 8;
    constexpr int kHeight = 2;

    /// Two triangles covering x in [left, right] of clip space, in @p color.
    std::array<VertexPositionColor, 6> Quad(const float left, const float right, const Color& color)
    {
        return {VertexPositionColor(Vector3(left, -1.0f, 0.0f), color),
                VertexPositionColor(Vector3(left, 1.0f, 0.0f), color),
                VertexPositionColor(Vector3(right, 1.0f, 0.0f), color),
                VertexPositionColor(Vector3(left, -1.0f, 0.0f), color),
                VertexPositionColor(Vector3(right, 1.0f, 0.0f), color),
                VertexPositionColor(Vector3(right, -1.0f, 0.0f), color)};
    }

    class BufferRewriteWithinFrameTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            device_ = std::make_unique<GraphicsDevice>();
            if (!device_->SupportsCapability(CNA::GraphicsCapability::ThreeD))
                GTEST_SKIP() << "this renderer draws no 3D primitives";
            target_ = std::make_unique<RenderTarget2D>(*device_, kWidth, kHeight);
            effect_ = std::make_unique<BasicEffect>(*device_);
            effect_->setVertexColorEnabledProperty(true);
            effect_->setWorldProperty(Matrix::getIdentityProperty());
            effect_->setViewProperty(Matrix::getIdentityProperty());
            effect_->setProjectionProperty(Matrix::getIdentityProperty());
            device_->SetRenderTarget(target_.get());
            device_->setBlendStateProperty(BlendState::Opaque);
            device_->setDepthStencilStateProperty(DepthStencilState::None);
            device_->setRasterizerStateProperty(RasterizerState::CullNone);
            device_->Clear(Color::Black);
        }

        /// Unbinds the target and returns its pixels, row by row.
        std::vector<Color> Read()
        {
            device_->SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::vector<Color> pixels(static_cast<std::size_t>(kWidth * kHeight));
            target_->GetData(pixels.data(), static_cast<int>(pixels.size()));
            return pixels;
        }

        /// Whether the left half is @p left and the right half is @p right.
        static void ExpectHalves(const std::vector<Color>& pixels, const Color& left,
                                 const Color& right)
        {
            for (int y = 0; y < kHeight; ++y)
                for (int x = 0; x < kWidth; ++x)
                {
                    const Color& pixel = pixels[static_cast<std::size_t>(y * kWidth + x)];
                    const Color& expected = x < kWidth / 2 ? left : right;
                    EXPECT_EQ(pixel, expected) << "pixel (" << x << ", " << y << ")";
                }
        }

        std::unique_ptr<GraphicsDevice> device_;
        std::unique_ptr<RenderTarget2D> target_;
        std::unique_ptr<BasicEffect> effect_;
    };
}

TEST_F(BufferRewriteWithinFrameTest, ADrawKeepsTheVerticesItWasIssuedWith)
{
    VertexBuffer vertices(*device_, VertexPositionColor::getVertexDeclarationStatic(), 6,
                          BufferUsage::WriteOnly);
    const auto left = Quad(-1.0f, 0.0f, Color::Red);
    const auto right = Quad(0.0f, 1.0f, Color::Lime);

    vertices.SetData(left.data(), 6);
    device_->SetVertexBuffer(&vertices);
    effect_->Apply();
    device_->DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

    // Rewritten before the frame is presented: the draw above must still read the red quad.
    // XNA refuses SetData on a bound buffer, so the game unbinds it first.
    device_->SetVertexBuffer(nullptr);
    vertices.SetData(right.data(), 6);
    device_->SetVertexBuffer(&vertices);
    device_->DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

    ExpectHalves(Read(), Color::Red, Color::Lime);
}

TEST_F(BufferRewriteWithinFrameTest, AnIndexedDrawKeepsTheIndicesItWasIssuedWith)
{
    VertexBuffer vertices(*device_, VertexPositionColor::getVertexDeclarationStatic(), 12,
                          BufferUsage::WriteOnly);
    std::array<VertexPositionColor, 12> both{};
    const auto left = Quad(-1.0f, 0.0f, Color::Red);
    const auto right = Quad(0.0f, 1.0f, Color::Lime);
    std::copy(left.begin(), left.end(), both.begin());
    std::copy(right.begin(), right.end(), both.begin() + 6);
    vertices.SetData(both.data(), 12);

    IndexBuffer indices(*device_, IndexElementSize::SixteenBits, 6, BufferUsage::WriteOnly);
    const std::array<std::uint16_t, 6> leftIndices{0, 1, 2, 3, 4, 5};
    const std::array<std::uint16_t, 6> rightIndices{6, 7, 8, 9, 10, 11};

    indices.SetData(leftIndices.data(), 6);
    device_->SetVertexBuffer(&vertices);
    device_->setIndicesProperty(&indices);
    effect_->Apply();
    device_->DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 12, 0, 2);

    // Same buffers, new indices: the first draw must still select the left quad.
    device_->setIndicesProperty(nullptr);
    indices.SetData(rightIndices.data(), 6);
    device_->setIndicesProperty(&indices);
    device_->DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 12, 0, 2);

    ExpectHalves(Read(), Color::Red, Color::Lime);
}

TEST_F(BufferRewriteWithinFrameTest, ABufferDrawnAgainAfterAFlushShowsItsNewContents)
{
    // The other direction: once a frame has been flushed, rewriting the buffer in place is
    // correct, and the next draw must see the new contents rather than the old storage.
    VertexBuffer vertices(*device_, VertexPositionColor::getVertexDeclarationStatic(), 6,
                          BufferUsage::WriteOnly);
    const auto red = Quad(-1.0f, 1.0f, Color::Red);
    const auto blue = Quad(-1.0f, 1.0f, Color::Blue);

    vertices.SetData(red.data(), 6);
    device_->SetVertexBuffer(&vertices);
    effect_->Apply();
    device_->DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
    ExpectHalves(Read(), Color::Red, Color::Red);

    device_->SetRenderTarget(target_.get());
    device_->SetVertexBuffer(nullptr);
    vertices.SetData(blue.data(), 6);
    device_->SetVertexBuffer(&vertices);
    effect_->Apply();
    device_->DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
    ExpectHalves(Read(), Color::Blue, Color::Blue);
}
