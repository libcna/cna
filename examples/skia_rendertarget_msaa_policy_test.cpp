// SPDX-License-Identifier: MS-PL
// SKIA-67: CPU raster targets do not allocate MSAA attachments. The shared XNA normalization of
// one sample to zero remains observable, while every real multisample request is rejected.

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SkiaRenderTargetMsaaPolicyTest final : public Game
{
public:
    SkiaRenderTargetMsaaPolicyTest()
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
        Check(!device.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing),
              "raster backend does not advertise MSAA");
        Check(CreateAndReportSamples(device, 0) == 0, "requested 0 samples reports 0");
        Check(CreateAndReportSamples(device, 1) == 0, "requested 1 sample normalizes to 0");
        Check(RejectsRealMsaa(device, 2), "requested 2 samples is rejected");
        Check(RejectsRealMsaa(device, 3), "requested 3 samples normalizes then is rejected");
        Check(RejectsRealMsaa(device, 4), "requested 4 samples is rejected");
        Exit();
    }

private:
    static int CreateAndReportSamples(GraphicsDevice& device, int requested)
    {
        RenderTarget2D target(device, 2, 2, false, SurfaceFormat::Color, DepthFormat::None, requested);
        return target.getMultiSampleCountProperty();
    }

    static bool RejectsRealMsaa(GraphicsDevice& device, int requested)
    {
        try
        {
            RenderTarget2D target(device, 2, 2, false, SurfaceFormat::Color, DepthFormat::None, requested);
            (void)target;
        }
        catch (const std::runtime_error& error)
        {
            return std::string(error.what()).find("multisampling") != std::string::npos;
        }
        return false;
    }

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
    SkiaRenderTargetMsaaPolicyTest game;
    game.Run();
    return game.Result();
}
