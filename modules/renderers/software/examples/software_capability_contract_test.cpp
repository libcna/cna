// SPDX-License-Identifier: MS-PL
// plans/plan_software.md SOFTWARE-104: capability promises must agree with the Software
// renderer's actual factories and execution paths throughout the parity campaign.

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    class SoftwareCapabilityContractTest final : public Game
    {
        std::unique_ptr<GraphicsDeviceManager> graphics_;
        int checks_ = 0;
        int passed_ = 0;
        int result_ = 1;

        void Check(bool condition, const char* description)
        {
            ++checks_;
            if (condition) ++passed_;
            std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", description);
        }

    protected:
        void Draw(const GameTime&) override
        {
            auto& device = getGraphicsDeviceProperty();

            Check(device.SupportsCapability(CNA::GraphicsCapability::ThreeD),
                  "ThreeD remains advertised");
            Check(device.SupportsCapability(CNA::GraphicsCapability::DepthStencilBuffer),
                  "DepthStencilBuffer remains advertised");
            Check(device.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing),
                  "MultiSampleAntiAliasing remains advertised");
            Check(device.SupportsCapability(CNA::GraphicsCapability::WireFrame),
                  "WireFrame remains advertised");
            Check(device.SupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput),
                  "MultiStreamVertexInput remains advertised");
            Check(device.SupportsCapability(CNA::GraphicsCapability::StencilBuffer),
                  "StencilBuffer remains advertised");
            Check(device.SupportsCapability(CNA::GraphicsCapability::AdditiveBlending),
                  "AdditiveBlending remains advertised");

            Check(device.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets),
                  "MultipleRenderTargets is advertised with the four-slot CPU binding path");
            Check(device.SupportsCapability(CNA::GraphicsCapability::AnisotropicFiltering),
                  "AnisotropicFiltering is advertised with its directional CPU sampler");
            Check(device.SupportsCapability(CNA::GraphicsCapability::OcclusionQuery),
                  "OcclusionQuery is advertised with its exact CPU counter");
            Check(!device.SupportsCapability(CNA::GraphicsCapability::CustomEffects),
                  "CustomEffects is false because supplied shader source is not executed");
            Check(device.SupportsCapability(CNA::GraphicsCapability::Texture3D),
                  "Texture3D is advertised with exact CPU volume storage");
            Check(device.SupportsCapability(CNA::GraphicsCapability::Instancing),
                  "Instancing is true with the CPU instance-frequency draw path");

            OcclusionQuery query(device);
            query.Begin();
            query.End();
            Check(query.HasRenderer(),
                  "OcclusionQuery owns a real Software renderer object");
            Check(query.getIsCompleteProperty() && query.getPixelCountProperty() == 0 &&
                      query.isPixelCountPreciseEXT(),
                  "an empty CPU query completes synchronously with an exact zero result");

            Texture3D texture(device, 2, 2, 2, false, SurfaceFormat::Color);
            const std::vector<Color> uploaded(8, Color(17, 31, 47, 191));
            std::vector<Color> readback(8, Color::Transparent);
            texture.SetData(uploaded.data(), static_cast<int>(uploaded.size()));
            texture.GetData(readback.data(), static_cast<int>(readback.size()));
            Check(readback == uploaded,
                  "Texture3D capability is backed by an exact SetData/GetData resource");

            std::printf("=== %d/%d PASS ===\n", passed_, checks_);
            result_ = passed_ == checks_ ? 0 : 1;
            Exit();
        }

    public:
        SoftwareCapabilityContractTest()
        {
            graphics_ = std::make_unique<GraphicsDeviceManager>(this);
            graphics_->setPreferredBackBufferWidthProperty(16);
            graphics_->setPreferredBackBufferHeightProperty(16);
        }

        [[nodiscard]] int Result() const { return result_; }
    };
}

int main()
{
    SoftwareCapabilityContractTest game;
    game.Run();
    return game.Result();
}
