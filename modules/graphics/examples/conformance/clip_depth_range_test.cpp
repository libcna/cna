// SPDX-License-Identifier: MS-PL
// XNA's clip-space depth range is Direct3D 9's [0, 1]: z = 0 is the near plane, z = 1 the far
// plane, and the stored depth IS z. Moved from the retired VULKAN suite (VULKAN-098, finding F-19).
//
// Under an identity projection and DepthStencilState.Default (LessEqual, depth write on), a
// full-frame quad at z drawn over a depth buffer cleared to d is visible exactly when z <= d.
// OpenGL's native [-1, 1] range would put z = 0 at depth 0.5, so the endpoints discriminate the two
// conventions where an ordering test could not -- both ranges are monotonic.
//
//   A  the 5 x 4 truth table: z in {0, .25, .5, .75, 1} against cleared depths {.1, .4, .6, .9};
//      z = 0 survives a depth of 0.4 (OpenGL's range would reject it) and z = 0.75 is rejected
//      by 0.6, both called out by name;
//   B  z = -0.5 is clipped at every cleared depth: the near plane is at 0, not at -1;
//   C  z = 1.5 is clipped at every cleared depth: the far plane is at 1.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    const Color kDrawn(230, 30, 30, 255);
    const Color kCleared(0, 0, 0, 255);
}

class ClipDepthRangeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int result_ = 0;

    void Check(bool ok, const std::string& label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label.c_str(), detail.c_str());
        std::fflush(stdout);
        if (!ok) result_ = 1;
    }

    /// Clears colour and depth to @p clearedDepth, draws a full-frame quad at @p z, and reports
    /// whether the quad reached the centre pixel.
    bool DrawnAt(GraphicsDevice& dev, BasicEffect& fx, float z, float clearedDepth)
    {
        const VertexPositionColor quad[6] = {
            {Vector3(-1.f, 1.f, z), kDrawn}, {Vector3(-1.f, -1.f, z), kDrawn},
            {Vector3(1.f, -1.f, z), kDrawn}, {Vector3(-1.f, 1.f, z), kDrawn},
            {Vector3(1.f, -1.f, z), kDrawn}, {Vector3(1.f, 1.f, z), kDrawn},
        };
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kCleared, clearedDepth, 0);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        Color got(0, 0, 0, 0);
        const Rectangle at(kSize / 2, kSize / 2, 1, 1);
        dev.GetBackBufferData(&at, &got, 0, 1);
        const int tol = 24;
        return std::abs(got.getRProperty() - kDrawn.getRProperty()) <= tol &&
               std::abs(got.getGProperty() - kDrawn.getGProperty()) <= tol &&
               std::abs(got.getBProperty() - kDrawn.getBProperty()) <= tol;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        BasicEffect fx(dev);
        fx.setVertexColorEnabledProperty(true);
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(false);
        fx.setFogEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());

        // ---- A: the truth table ----------------------------------------------------------------
        constexpr float kZ[5] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        constexpr float kDepths[4] = {0.1f, 0.4f, 0.6f, 0.9f};
        int agreed = 0;
        int total = 0;
        std::string firstMismatch;
        for (float z : kZ)
        {
            for (float d : kDepths)
            {
                const bool expected = z <= d + 1e-6f;
                const bool got = DrawnAt(dev, fx, z, d);
                ++total;
                if (got == expected)
                {
                    ++agreed;
                }
                else if (firstMismatch.empty())
                {
                    char buf[160];
                    std::snprintf(buf, sizeof(buf),
                                  "first at z=%.2f clearedDepth=%.2f: %s, XNA's [0,1] says %s", z,
                                  d, got ? "drawn" : "rejected", expected ? "drawn" : "rejected");
                    firstMismatch = buf;
                }
            }
        }
        Check(agreed == total,
              "A depth is z over [0,1], under LessEqual at every cell of a 5x4 table",
              std::to_string(agreed) + "/" + std::to_string(total) + " cells agree" +
                  (firstMismatch.empty() ? "" : "; " + firstMismatch));
        Check(DrawnAt(dev, fx, 0.0f, 0.4f), "A z=0 is the NEAR plane: it survives a depth of 0.4",
              "under OpenGL's [-1,1] range z=0 would be depth 0.5 and be rejected");
        Check(!DrawnAt(dev, fx, 0.75f, 0.6f), "A z=0.75 is rejected by a depth of 0.6",
              "the far half of the range is not compressed");

        // ---- B and C: the planes -----------------------------------------------------------------
        bool anyNegative = false;
        for (float d : kDepths)
            anyNegative = DrawnAt(dev, fx, -0.5f, d) || anyNegative;
        Check(!anyNegative, "B z=-0.5 is clipped at every cleared depth",
              "the near plane is at 0, not at -1");

        bool anyBeyondFar = false;
        for (float d : kDepths)
            anyBeyondFar = DrawnAt(dev, fx, 1.5f, d) || anyBeyondFar;
        Check(!anyBeyondFar, "C z=1.5 is clipped at every cleared depth", "the far plane is at 1");

        Exit();
    }

public:
    ClipDepthRangeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    ClipDepthRangeTest game;
    game.Run();
    return game.getResult();
}
