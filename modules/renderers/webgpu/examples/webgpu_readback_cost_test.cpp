// SPDX-License-Identifier: MS-PL
// A Present-only frame must not stage the full backbuffer for a read the game never requests.
// One GetBackBufferData call captures the current generation, another unchanged read reuses it,
// and Present does not capture it again. A later rendered generation must make one fresh copy.

#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"

#include <cstdio>
#include <memory>

using CNA::Internal::Renderers::WebGPU::WebGPURenderer;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 32;

    bool SameRgb(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() &&
               a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty();
    }
}

class WebGpuReadbackCostTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::size_t beforePresent_ = 0;
    std::size_t afterRead_ = 0;
    int frame_ = 0;
    int passed_ = 0;
    int result_ = 1;

    WebGPURenderer& Renderer()
    {
        return static_cast<WebGPURenderer&>(getGraphicsDeviceProperty().GetRenderer());
    }

    Color ReadCentre()
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        Color pixel;
        getGraphicsDeviceProperty().GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (condition) ++passed_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = Renderer();

        if (frame_ == 0)
        {
            beforePresent_ = renderer.GetBackbufferReadbackCopyCountEXT();
            device.Clear(Color::Red);
        }
        else if (frame_ == 1)
        {
            Check(renderer.GetBackbufferReadbackCopyCountEXT() == beforePresent_,
                  "Present-only frame encodes no backbuffer readback copy");

            device.Clear(Color::Blue);
            const std::size_t beforeRead = renderer.GetBackbufferReadbackCopyCountEXT();
            const Color first = ReadCentre();
            const std::size_t afterFirst = renderer.GetBackbufferReadbackCopyCountEXT();
            const Color second = ReadCentre();
            afterRead_ = renderer.GetBackbufferReadbackCopyCountEXT();
            Check(afterFirst == beforeRead + 1,
                  "first read of a rendered generation encodes exactly one copy");
            Check(afterRead_ == afterFirst,
                  "repeated read of unchanged contents reuses the staged generation");
            Check(SameRgb(first, Color::Blue) && SameRgb(second, Color::Blue),
                  "both on-demand reads return the current frame pixels");
        }
        else
        {
            Check(renderer.GetBackbufferReadbackCopyCountEXT() == afterRead_,
                  "Present after an on-demand read does not encode a second copy");

            device.Clear(Color::Lime);
            const Color current = ReadCentre();
            Check(renderer.GetBackbufferReadbackCopyCountEXT() == afterRead_ + 1,
                  "a newly rendered generation receives one fresh readback copy");
            Check(SameRgb(current, Color::Lime),
                  "fresh generation readback returns its new pixels");

            std::printf("=== %d/7 PASS ===\n", passed_);
            result_ = passed_ == 7 ? 0 : 1;
            Exit();
        }
        ++frame_;
    }

public:
    WebGpuReadbackCostTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int Result() const noexcept { return result_; }
};

int main()
{
    WebGpuReadbackCostTest game;
    game.Run();
    return game.Result();
}
