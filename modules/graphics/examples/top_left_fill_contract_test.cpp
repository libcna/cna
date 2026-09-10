// SPDX-License-Identifier: MS-PL
// SOFTWARE-107/131/136: renderer-neutral XNA/D3D fill, pixel-center and constant-varying contract.
// Two additive triangles share the TL->BR diagonal of an integer-coordinate screen-space square.
// Every sample on that edge must belong to exactly one triangle: a crack stays black and an
// all-inclusive edge doubles red. Reversing draw order or winding must not change coverage.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kMin = 8;
    constexpr int kMax = 56;
    const Color kContribution(64, 0, 0, 255);

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() &&
               a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() &&
               a.getAProperty() == b.getAProperty();
    }

    bool Close(const Color& a, const Color& b)
    {
        const auto close = [](int lhs, int rhs) { return std::abs(lhs - rhs) <= 1; };
        return close(a.getRProperty(), b.getRProperty()) &&
               close(a.getGProperty(), b.getGProperty()) &&
               close(a.getBProperty(), b.getBProperty()) &&
               close(a.getAProperty(), b.getAProperty());
    }

    bool OneContribution(const Color& color)
    {
        return color.getRProperty() >= 62 && color.getRProperty() <= 66 &&
               color.getGProperty() == 0 && color.getBProperty() == 0;
    }

    std::string ColorText(const Color& color)
    {
        return "(" + std::to_string(color.getRProperty()) + "," +
               std::to_string(color.getGProperty()) + "," +
               std::to_string(color.getBProperty()) + "," +
               std::to_string(color.getAProperty()) + ")";
    }
}

class TopLeftFillContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok)
            ++passCount_;
    }

    std::vector<Color> ReadBackbuffer(GraphicsDevice& device)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kSize * kSize));
        const Rectangle whole(0, 0, kSize, kSize);
        device.GetBackBufferData(&whole, pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels;
    }

    void DrawPair(GraphicsDevice& device, bool reverseOrder, bool reverseWinding)
    {
        VertexPositionColor upper[3] = {
            {Vector3(static_cast<float>(kMin), static_cast<float>(kMin), 0.0f), kContribution},
            {Vector3(static_cast<float>(kMax), static_cast<float>(kMin), 0.0f), kContribution},
            {Vector3(static_cast<float>(kMax), static_cast<float>(kMax), 0.0f), kContribution},
        };
        VertexPositionColor lower[3] = {
            {Vector3(static_cast<float>(kMin), static_cast<float>(kMin), 0.0f), kContribution},
            {Vector3(static_cast<float>(kMax), static_cast<float>(kMax), 0.0f), kContribution},
            {Vector3(static_cast<float>(kMin), static_cast<float>(kMax), 0.0f), kContribution},
        };
        if (reverseWinding)
        {
            std::swap(upper[1], upper[2]);
            std::swap(lower[1], lower[2]);
        }

        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setProjectionProperty(
            Matrix::CreateOrthographicOffCenter(0.0f, static_cast<float>(kSize),
                                                static_cast<float>(kSize), 0.0f, 0.0f, 1.0f));
        effect.Apply();
        const auto draw = [&](const VertexPositionColor* triangle) {
            device.DrawUserPrimitives(PrimitiveType::TriangleList, triangle, 0, 1);
        };
        if (reverseOrder)
        {
            draw(lower);
            draw(upper);
        }
        else
        {
            draw(upper);
            draw(lower);
        }
    }

    std::vector<Color> RenderBackbuffer(GraphicsDevice& device,
                                        bool reverseOrder, bool reverseWinding)
    {
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        device.Clear(Color::Black, 1.0f);
        DrawPair(device, reverseOrder, reverseWinding);
        return ReadBackbuffer(device);
    }

    int InvalidSquarePixels(const std::vector<Color>& pixels) const
    {
        int invalid = 0;
        for (int y = kMin; y < kMax; ++y)
            for (int x = kMin; x < kMax; ++x)
                invalid += OneContribution(pixels[static_cast<std::size_t>(y * kSize + x)]) ? 0 : 1;
        return invalid;
    }

    int LitOutsideSquare(const std::vector<Color>& pixels) const
    {
        int lit = 0;
        for (int y = 0; y < kSize; ++y)
        {
            for (int x = 0; x < kSize; ++x)
            {
                if (x >= kMin && x < kMax && y >= kMin && y < kMax)
                    continue;
                const Color& color = pixels[static_cast<std::size_t>(y * kSize + x)];
                lit += color.getRProperty() != 0 || color.getGProperty() != 0 ||
                       color.getBProperty() != 0 ? 1 : 0;
            }
        }
        return lit;
    }

    int InvalidInteriorDiagonal(const std::vector<Color>& pixels) const
    {
        int invalid = 0;
        for (int coordinate = kMin + 4; coordinate < kMax - 4; ++coordinate)
        {
            invalid += OneContribution(
                pixels[static_cast<std::size_t>(coordinate * kSize + coordinate)]) ? 0 : 1;
        }
        return invalid;
    }

    int NonConstantSquarePixels(const std::vector<Color>& pixels) const
    {
        const Color expected = pixels[static_cast<std::size_t>((kMin + 4) * kSize + kMin + 8)];
        int different = 0;
        for (int y = kMin; y < kMax; ++y)
            for (int x = kMin; x < kMax; ++x)
                different += Same(
                    pixels[static_cast<std::size_t>(y * kSize + x)], expected) ? 0 : 1;
        return different;
    }

    bool SameImage(const std::vector<Color>& a, const std::vector<Color>& b) const
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (!Close(a[i], b[i]))
                return false;
        return true;
    }

    int DifferenceCount(const std::vector<Color>& a, const std::vector<Color>& b) const
    {
        int differences = 0;
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
            differences += Same(a[i], b[i]) ? 0 : 1;
        return differences + static_cast<int>(std::max(a.size(), b.size()) -
                                              std::min(a.size(), b.size()));
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(BlendState::Additive);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const std::vector<Color> baseline = RenderBackbuffer(device, false, false);
        const int invalidSquare = InvalidSquarePixels(baseline);
        const int outside = LitOutsideSquare(baseline);
        const int invalidDiagonal = InvalidInteriorDiagonal(baseline);
        Check(invalidSquare == 0 && outside == 0,
              "integer-coordinate square covers exactly 48x48 pixels once (invalid=" +
                  std::to_string(invalidSquare) + ", outside=" + std::to_string(outside) +
                  ", interior=" + ColorText(baseline[static_cast<std::size_t>(16 * kSize + 20)]) + ")");
        Check(invalidDiagonal == 0,
              "shared TL->BR edge has neither cracks nor additive double hits (invalid=" +
                  std::to_string(invalidDiagonal) + ", sample=" +
                  ColorText(baseline[static_cast<std::size_t>(20 * kSize + 20)]) + ")");
        const int nonConstant = NonConstantSquarePixels(baseline);
        Check(nonConstant == 0,
              "constant triangle varying stays byte-identical across both halves (different=" +
                  std::to_string(nonConstant) + ", sample=" +
                  ColorText(baseline[static_cast<std::size_t>((kMin + 4) * kSize + kMin + 8)]) +
                  ")");

        const std::vector<Color> reversedOrder = RenderBackbuffer(device, true, false);
        Check(SameImage(baseline, reversedOrder),
              "reversing triangle draw order leaves byte-identical shared-edge coverage");

        const std::vector<Color> reversedWinding = RenderBackbuffer(device, false, true);
        const int reversedDifferences = DifferenceCount(baseline, reversedWinding);
        Check(SameImage(baseline, reversedWinding),
              "reversing both windings under CullNone preserves coverage within one byte (exactly different=" +
                  std::to_string(reversedDifferences) + ", square-invalid=" +
                  std::to_string(InvalidSquarePixels(reversedWinding)) + ", diagonal-invalid=" +
                  std::to_string(InvalidInteriorDiagonal(reversedWinding)) + ")");

        RenderTarget2D multisampled(device, kSize, kSize, false, SurfaceFormat::Color,
                                    DepthFormat::None, 4, RenderTargetUsage::DiscardContents);
        device.SetRenderTarget(&multisampled);
        device.Clear(Color::Black, 1.0f);
        DrawPair(device, false, false);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        std::vector<Color> msaaPixels(static_cast<std::size_t>(kSize * kSize));
        multisampled.GetData(msaaPixels.data(), 0, static_cast<int>(msaaPixels.size()));
        const int invalidMsaaDiagonal = InvalidInteriorDiagonal(msaaPixels);
        Check(invalidMsaaDiagonal == 0,
              "4x MSAA assigns each exact shared-edge sample once before resolve (invalid=" +
                  std::to_string(invalidMsaaDiagonal) + ", sample=" +
                  ColorText(msaaPixels[static_cast<std::size_t>(20 * kSize + 20)]) + ")");

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = passCount_ == totalCount_ ? 0 : 1;
        Exit();
    }

public:
    TopLeftFillContractTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        graphics_->setPreferredBackBufferWidthProperty(kSize);
        graphics_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    TopLeftFillContractTest game;
    game.Run();
    return game.getResult();
}
