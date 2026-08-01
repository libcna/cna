// SPDX-License-Identifier: MS-PL
// Verifies that raster-only SKIA advertises only its real CPU Texture3D storage capability while
// keeping unavailable GPU/3D calls as deterministic failures.

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::GraphicsCapability;

class SkiaGraphicsCapabilityTest final : public Game
{
public:
    SkiaGraphicsCapabilityTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(32);
        graphics_->setPreferredBackBufferHeightProperty(16);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();

        Check(!device.SupportsCapability(GraphicsCapability::ThreeD), "ThreeD is not advertised");
        Check(!device.SupportsCapability(GraphicsCapability::DepthStencilBuffer), "DepthStencilBuffer is not advertised");
        Check(!device.SupportsCapability(GraphicsCapability::MultiSampleAntiAliasing), "MSAA is not advertised");
        Check(!device.SupportsCapability(GraphicsCapability::MultipleRenderTargets), "MRT is not advertised");
        Check(!device.SupportsCapability(GraphicsCapability::AnisotropicFiltering), "anisotropic filtering is not advertised");
        Check(!device.SupportsCapability(GraphicsCapability::WireFrame), "wireframe is not advertised");
        Check(!device.SupportsCapability(GraphicsCapability::OcclusionQuery), "occlusion queries are not advertised");
        Check(!device.SupportsCapability(GraphicsCapability::CustomEffects), "custom effects are not advertised");
        Check(device.SupportsCapability(GraphicsCapability::Texture3D),
              "Texture3D CPU storage is advertised independently from 3D sampling");
        Check(Throws([&] { device.SetDepthTestEnabled(true); }), "3D state call throws deterministically");
        Check(Throws([&] { VertexBuffer vertexBuffer(device, 4); (void)vertexBuffer; }),
              "VertexBuffer construction throws deterministically");
        Exit();
    }

private:
    template <typename Callable>
    static bool Throws(Callable&& callable)
    {
        try { callable(); }
        catch (const std::runtime_error&) { return true; }
        return false;
    }

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    bool done_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaGraphicsCapabilityTest game;
    game.Run();
    return game.Result();
}
