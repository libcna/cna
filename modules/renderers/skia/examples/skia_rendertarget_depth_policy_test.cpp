// SPDX-License-Identifier: MS-PL
// SKIA-66: a requested XNA depth/stencil format is retained as metadata, but the CPU Skia raster
// target has no depth/stencil attachment and therefore must not claim or clear one.

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRed(255, 0, 0, 255);

    [[nodiscard]] bool Same(Color actual, Color expected)
    {
        return actual.getRProperty() == expected.getRProperty()
            && actual.getGProperty() == expected.getGProperty()
            && actual.getBProperty() == expected.getBProperty()
            && actual.getAProperty() == expected.getAProperty();
    }
}

class SkiaRenderTargetDepthPolicyTest final : public Game
{
public:
    SkiaRenderTargetDepthPolicyTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(8);
        graphics_->setPreferredBackBufferHeightProperty(8);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, 4, 4, false, SurfaceFormat::Color,
                              DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::PreserveContents);
        const auto* renderer = target.GetRenderTargetRenderer();

        Check(target.getDepthStencilFormatProperty() == DepthFormat::Depth24Stencil8,
              "requested Depth24Stencil8 remains public RenderTarget2D metadata");
        Check(!device.SupportsCapability(CNA::GraphicsCapability::DepthStencilBuffer),
              "raster device does not advertise a depth/stencil buffer");
        Check(renderer != nullptr && !renderer->HasRealDepthBuffer(true)
                  && !renderer->HasRealDepthBuffer(false),
              "Skia target reports no real depth buffer regardless of requested metadata");

        device.SetRenderTarget(&target);
        device.Clear(kRed);
        // GraphicsDevice strips these clear aspects when the bound renderer reports no actual
        // attachment. If a target fabricated depth/stencil support this would change the colour.
        device.Clear(ClearOptions::DepthBuffer | ClearOptions::Stencil, Color(0, 0, 255, 255), 0.25f, 7);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color pixel(0, 0, 0, 0);
        const Rectangle centre(2, 2, 1, 1);
        target.GetData(0, &centre, &pixel, 0, 1);
        Check(Same(pixel, kRed), "depth/stencil-only ClearOptions leave raster target colour unchanged");
        Exit();
    }

private:
    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaRenderTargetDepthPolicyTest game;
    game.Run();
    return game.Result();
}
