// SPDX-License-Identifier: MS-PL
// SOFTWARE-106: deterministic public-API proof for the complete XNA/D3D homogeneous clip volume.
// Identity WVP makes each submitted Position equal to clip XYZ with W=1, so the six boundaries are
// directly observable: -1 <= X,Y <= 1 and 0 <= Z <= 1. Depth is disabled deliberately; otherwise
// its comparison could hide missing near/far geometry clipping and make a broken implementation pass.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    bool IsRed(const Color& color)
    {
        return color.getRProperty() > 200 &&
               color.getGProperty() < 60 &&
               color.getBProperty() < 60;
    }

    struct Snapshot
    {
        std::vector<Color> pixels;
        int redCount = 0;
        int minX = std::numeric_limits<int>::max();
        int minY = std::numeric_limits<int>::max();
        int maxX = std::numeric_limits<int>::min();
        int maxY = std::numeric_limits<int>::min();

        bool RedAt(int x, int y) const
        {
            return IsRed(pixels[static_cast<std::size_t>(y * kSize + x)]);
        }
    };

    Snapshot ReadSnapshot(GraphicsDevice& device)
    {
        Snapshot result;
        result.pixels.resize(static_cast<std::size_t>(kSize * kSize));
        const Rectangle whole(0, 0, kSize, kSize);
        device.GetBackBufferData(&whole, result.pixels.data(), 0,
                                 static_cast<int>(result.pixels.size()));
        for (int y = 0; y < kSize; ++y)
        {
            for (int x = 0; x < kSize; ++x)
            {
                if (!result.RedAt(x, y))
                    continue;
                ++result.redCount;
                result.minX = std::min(result.minX, x);
                result.minY = std::min(result.minY, y);
                result.maxX = std::max(result.maxX, x);
                result.maxY = std::max(result.maxY, y);
            }
        }
        return result;
    }
}

class SoftwareClippingTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
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

    void DrawTriangle(GraphicsDevice& device,
                      const Vector3& a, const Vector3& b, const Vector3& c)
    {
        const VertexPositionColor vertices[3] = {
            {a, Color::Red}, {b, Color::Red}, {c, Color::Red},
        };
        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 1);
    }

    void DrawIndexedTriangle(GraphicsDevice& device,
                             const Vector3& a, const Vector3& b, const Vector3& c)
    {
        const VertexPositionColor vertices[3] = {
            {a, Color::Red}, {b, Color::Red}, {c, Color::Red},
        };
        const std::uint16_t indices[3] = {0, 1, 2};
        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.Apply();
        device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleList, vertices, 0, 3, indices, 0, 1);
    }

    void SetFill(GraphicsDevice& device, FillMode fillMode)
    {
        RasterizerState state;
        state.setCullModeProperty(CullMode::None);
        state.setFillModeProperty(fillMode);
        device.setRasterizerStateProperty(state);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        device.setDepthStencilStateProperty(DepthStencilState::None);
        SetFill(device, FillMode::Solid);

        // Each triangle has one vertex outside one plane and two inside. The visible bounds and a
        // stable interior probe prove that the crossing was clipped rather than wholly discarded.
        device.Clear(Color::Black, 1.0f);
        DrawTriangle(device, Vector3(-1.5f, 0.0f, 0.5f), Vector3(0.0f, 0.75f, 0.5f),
                     Vector3(0.0f, -0.75f, 0.5f));
        Snapshot shot = ReadSnapshot(device);
        Check(shot.redCount > 0 && shot.maxX <= 32 && shot.RedAt(16, 32),
              "left plane: crossing polygon survives only at X >= -W");

        device.Clear(Color::Black, 1.0f);
        DrawIndexedTriangle(device, Vector3(1.5f, 0.0f, 0.5f), Vector3(0.0f, -0.75f, 0.5f),
                            Vector3(0.0f, 0.75f, 0.5f));
        shot = ReadSnapshot(device);
        Check(shot.redCount > 0 && shot.minX >= 31 && shot.RedAt(48, 32),
              "right plane: indexed crossing polygon survives only at X <= W");

        device.Clear(Color::Black, 1.0f);
        DrawTriangle(device, Vector3(0.0f, -1.5f, 0.5f), Vector3(-0.75f, 0.0f, 0.5f),
                     Vector3(0.75f, 0.0f, 0.5f));
        shot = ReadSnapshot(device);
        Check(shot.redCount > 0 && shot.minY >= 31 && shot.RedAt(32, 48),
              "bottom plane: crossing polygon survives only at Y >= -W");

        device.Clear(Color::Black, 1.0f);
        DrawTriangle(device, Vector3(0.0f, 1.5f, 0.5f), Vector3(0.75f, 0.0f, 0.5f),
                     Vector3(-0.75f, 0.0f, 0.5f));
        shot = ReadSnapshot(device);
        Check(shot.redCount > 0 && shot.maxY <= 32 && shot.RedAt(32, 16),
              "top plane: crossing polygon survives only at Y <= W");

        device.Clear(Color::Black, 1.0f);
        DrawTriangle(device, Vector3(0.0f, 0.75f, -0.5f), Vector3(0.75f, -0.75f, 0.5f),
                     Vector3(-0.75f, -0.75f, 0.5f));
        shot = ReadSnapshot(device);
        Check(shot.redCount > 0 && shot.minY >= 31 && shot.RedAt(32, 48),
              "near plane: Z < 0 is clipped even when depth testing is disabled");

        device.Clear(Color::Black, 1.0f);
        DrawIndexedTriangle(device, Vector3(0.0f, 0.75f, 1.5f),
                            Vector3(0.75f, -0.75f, 0.5f),
                            Vector3(-0.75f, -0.75f, 0.5f));
        shot = ReadSnapshot(device);
        Check(shot.redCount > 0 && shot.minY >= 31 && shot.RedAt(32, 48),
              "far plane: Z > W is clipped even when depth testing is disabled");

        // Six fully outside triangles submitted together must all be rejected.
        device.Clear(Color::Black, 1.0f);
        DrawTriangle(device, Vector3(-2.0f, -0.4f, 0.5f), Vector3(-1.2f, 0.4f, 0.5f),
                     Vector3(-1.2f, -0.4f, 0.5f));
        DrawTriangle(device, Vector3(2.0f, -0.4f, 0.5f), Vector3(1.2f, -0.4f, 0.5f),
                     Vector3(1.2f, 0.4f, 0.5f));
        DrawTriangle(device, Vector3(-0.4f, -2.0f, 0.5f), Vector3(0.4f, -1.2f, 0.5f),
                     Vector3(-0.4f, -1.2f, 0.5f));
        DrawTriangle(device, Vector3(-0.4f, 2.0f, 0.5f), Vector3(-0.4f, 1.2f, 0.5f),
                     Vector3(0.4f, 1.2f, 0.5f));
        DrawTriangle(device, Vector3(-0.4f, -0.4f, -0.5f), Vector3(0.4f, -0.4f, -0.5f),
                     Vector3(0.0f, 0.4f, -0.5f));
        DrawTriangle(device, Vector3(-0.4f, -0.4f, 1.5f), Vector3(0.0f, 0.4f, 1.5f),
                     Vector3(0.4f, -0.4f, 1.5f));
        Check(ReadSnapshot(device).redCount == 0,
              "fully outside geometry is rejected by every frustum plane");

        // This triangle intersects several planes and requires repeated polygon clipping.
        device.Clear(Color::Black, 1.0f);
        DrawTriangle(device, Vector3(0.0f, -0.4f, 0.5f), Vector3(-2.5f, 2.0f, -0.5f),
                     Vector3(2.5f, 2.0f, 1.5f));
        shot = ReadSnapshot(device);
        Check(shot.redCount > 0 && shot.redCount < kSize * kSize && shot.RedAt(32, 38),
              "multi-plane polygon clipping produces bounded finite output");

        // Lines use the same six planes. With depth disabled the old W-only path painted the upper
        // half too; the real near-plane intersection begins at screen Y=32.
        device.Clear(Color::Black, 1.0f);
        const VertexPositionColor line[2] = {
            {Vector3(-0.5f, 0.8f, -0.5f), Color::Red},
            {Vector3(-0.5f, -0.8f, 0.5f), Color::Red},
        };
        BasicEffect lineEffect(device);
        lineEffect.VertexColorEnabled = true;
        lineEffect.Apply();
        device.DrawUserPrimitives(PrimitiveType::LineList, line, 0, 1);
        shot = ReadSnapshot(device);
        Check(!shot.RedAt(16, 16) && shot.RedAt(16, 48) && shot.minY >= 31,
              "line segment is clipped against the near plane before rasterization");

        // PointListEXT is outside this campaign's completion scope, but the existing Software path
        // shares this primitive clip decision and must not regress while the common code changes.
        device.Clear(Color::Black, 1.0f);
        const VertexPositionColor points[2] = {
            {Vector3(0.0f, 0.0f, 0.5f), Color::Red},
            {Vector3(0.5f, 0.0f, -0.5f), Color::Red},
        };
        BasicEffect pointEffect(device);
        pointEffect.VertexColorEnabled = true;
        pointEffect.Apply();
        device.DrawUserPrimitives(PrimitiveType::PointListEXT, points, 0, 2);
        shot = ReadSnapshot(device);
        Check(shot.RedAt(32, 32) && !shot.RedAt(48, 32) && shot.redCount == 1,
              "point coverage accepts only positions inside all six planes");

        // A clipped quad is fan-triangulated. Its internal fan diagonal must remain absent from
        // WireFrame, while the same location is covered under Solid.
        device.Clear(Color::Black, 1.0f);
        SetFill(device, FillMode::Solid);
        DrawTriangle(device, Vector3(-1.5f, 0.0f, 0.5f), Vector3(0.0f, 0.75f, 0.5f),
                     Vector3(0.0f, -0.75f, 0.5f));
        const bool solidInterior = ReadSnapshot(device).RedAt(16, 24);

        device.Clear(Color::Black, 1.0f);
        SetFill(device, FillMode::WireFrame);
        DrawTriangle(device, Vector3(-1.5f, 0.0f, 0.5f), Vector3(0.0f, 0.75f, 0.5f),
                     Vector3(0.0f, -0.75f, 0.5f));
        shot = ReadSnapshot(device);
        Check(solidInterior && !shot.RedAt(16, 24) && shot.redCount > 0,
              "wireframe exposes the clipped polygon boundary without fan diagonals");

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    SoftwareClippingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    SoftwareClippingTest game;
    game.Run();
    return game.getResult();
}
