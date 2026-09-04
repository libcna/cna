// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-61: real-device proof that GraphicsDevice.SupportsCapability() agrees
// with what the corresponding Create/Apply operation actually does on THIS live device -- not just
// that EvaluateCapability()'s pure decision logic is internally consistent (already covered by the
// DiligentDeviceSelectionTest.EvaluateCapability* GTest cases with synthetic inputs, no GPU
// required). This is the live half of DILIGENT-61's own "pure helper tests plus live-device
// consistency checks" acceptance criterion.
//
// Check A -- SupportsCapability(OcclusionQuery): if true, creating a real OcclusionQuery must not
//   throw; if false, it must throw rather than silently returning a query that never signals.
// Check B -- SupportsCapability(WireFrame): if true, drawing with FillMode::WireFrame must not
//   throw.
// Check C -- SupportsCapability(MultiSampleAntiAliasing): if true, requesting MultiSampleCount=4 on
//   a RenderTarget2D must apply a genuinely > 1 sample count (not silently clamp to 1).
// Check D -- SupportsCapability(AnisotropicFiltering): if true, SamplerState.MaxAnisotropy > 1 must
//   not throw when drawing (already established crash-safe either way by DILIGENT-52; this only
//   checks the capability flag doesn't contradict that).
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class DiligentCapabilityConsistencyTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        ++totalCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok)
            ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        // Check A: OcclusionQuery.
        {
            const bool supports = device.SupportsCapability(CNA::GraphicsCapability::OcclusionQuery);
            bool threw = false;
            try
            {
                OcclusionQuery query(device);
                (void)query;
            }
            catch (const std::exception&)
            {
                threw = true;
            }
            std::printf("    (SupportsCapability(OcclusionQuery)=%d, creation threw=%d)\n", supports,
                        threw);
            Check(supports == !threw,
                 "SupportsCapability(OcclusionQuery) agrees with whether creation throws");
        }

        // Check B: WireFrame.
        {
            const bool supports = device.SupportsCapability(CNA::GraphicsCapability::WireFrame);
            bool threw = false;
            try
            {
                RasterizerState rs;
                rs.setFillModeProperty(FillMode::WireFrame);
                device.setRasterizerStateProperty(rs);

                BasicEffect effect(device);
                effect.setWorldProperty(Matrix::getIdentityProperty());
                effect.setViewProperty(Matrix::getIdentityProperty());
                effect.setProjectionProperty(Matrix::getIdentityProperty());
                effect.VertexColorEnabled = true;
                effect.Apply();

                const VertexPositionColor tri[3] = {
                    {Vector3(0.0f, 0.5f, 0.0f), Color::Red},
                    {Vector3(0.5f, -0.5f, 0.0f), Color::Red},
                    {Vector3(-0.5f, -0.5f, 0.0f), Color::Red},
                };
                device.setBlendStateProperty(BlendState::Opaque);
                device.DrawUserPrimitives(PrimitiveType::TriangleList, tri, 0, 1);
                device.setRasterizerStateProperty(RasterizerState::CullNone);
            }
            catch (const std::exception&)
            {
                threw = true;
            }
            std::printf("    (SupportsCapability(WireFrame)=%d, draw threw=%d)\n", supports, threw);
            Check(!supports || !threw,
                 "SupportsCapability(WireFrame)=true never precedes a throwing wireframe draw");
        }

        // Check C: MultiSampleAntiAliasing.
        {
            const bool supports =
                device.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing);
            RenderTarget2D rt(device, 16, 16, false, SurfaceFormat::Color, DepthFormat::None, 4,
                              RenderTargetUsage::DiscardContents);
            const int applied = rt.getMultiSampleCountProperty();
            std::printf("    (SupportsCapability(MultiSampleAntiAliasing)=%d, applied "
                        "MultiSampleCount=%d)\n",
                        supports, applied);
            Check(supports == (applied > 1),
                 "SupportsCapability(MultiSampleAntiAliasing) agrees with the applied sample count");
        }

        // Check D: AnisotropicFiltering.
        {
            const bool supports =
                device.SupportsCapability(CNA::GraphicsCapability::AnisotropicFiltering);
            bool threw = false;
            try
            {
                SamplerState sampler;
                sampler.setMaxAnisotropyProperty(16);
                device.getSamplerStatesProperty()[0] = sampler;

                BasicEffect effect(device);
                effect.setWorldProperty(Matrix::getIdentityProperty());
                effect.setViewProperty(Matrix::getIdentityProperty());
                effect.setProjectionProperty(Matrix::getIdentityProperty());
                effect.VertexColorEnabled = true;
                effect.Apply();

                const VertexPositionColor tri[3] = {
                    {Vector3(0.0f, 0.5f, 0.0f), Color::Red},
                    {Vector3(0.5f, -0.5f, 0.0f), Color::Red},
                    {Vector3(-0.5f, -0.5f, 0.0f), Color::Red},
                };
                device.setRasterizerStateProperty(RasterizerState::CullNone);
                device.DrawUserPrimitives(PrimitiveType::TriangleList, tri, 0, 1);
            }
            catch (const std::exception&)
            {
                threw = true;
            }
            std::printf("    (SupportsCapability(AnisotropicFiltering)=%d, draw threw=%d)\n",
                        supports, threw);
            Check(!threw, "MaxAnisotropy=16 draw never throws regardless of the capability flag");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = passCount_ == totalCount_ ? 0 : 1;
        Exit();
    }

public:
    DiligentCapabilityConsistencyTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(64);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentCapabilityConsistencyTest game;
        game.Run();
        return game.GetResult();
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        const bool noDevice = message.find("no device type could be created") != std::string::npos ||
                              message.find("unsupported SDL video driver") != std::string::npos ||
                              message.find("SDL_Init") != std::string::npos ||
                              message.find("live SDL window") != std::string::npos;
        if (noDevice)
        {
            std::printf("[SKIP] CNA Diligent smoke: no usable device (%s)\n", error.what());
            return 77;
        }
        std::printf("[FAIL] unexpected exception: %s\n", error.what());
        return 1;
    }
}
