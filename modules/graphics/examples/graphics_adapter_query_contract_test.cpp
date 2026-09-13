// SPDX-License-Identifier: MS-PL
// DX-245: GraphicsAdapter must answer from the selected D3D device before GraphicsDevice exists.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class GraphicsAdapterQueryContract final : public Game
{
    static constexpr int kImpossibleSampleRequest = 1024;

    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int renderTargetSampleCount_ = -1;
    int backBufferSampleCount_ = -1;
    int passed_ = 0;
    int failed_ = 0;
    bool done_ = false;

    static bool IsPowerOfTwo(int value)
    {
        return value > 0 && (value & (value - 1)) == 0;
    }

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        condition ? ++passed_ : ++failed_;
    }

    void QueryBeforeDeviceCreation()
    {
        GraphicsAdapter& adapter = GraphicsAdapter::getDefaultAdapterProperty();
        Check(adapter.IsProfileSupported(GraphicsProfile::Reach),
              "Reach stays supported without a fabricated profile capability table");
        Check(adapter.IsProfileSupported(GraphicsProfile::HiDef),
              "HiDef stays supported without a fabricated profile capability table");

        SurfaceFormat selectedFormat = SurfaceFormat::Bgr565;
        DepthFormat selectedDepth = DepthFormat::Depth16;
        int selectedSamples = -1;
        bool exact = adapter.QueryRenderTargetFormat(
            GraphicsProfile::HiDef, SurfaceFormat::Color, DepthFormat::None, 0,
            selectedFormat, selectedDepth, selectedSamples);
        Check(exact, "zero-sample Color render target is accepted exactly");
        Check(selectedFormat == SurfaceFormat::Color &&
                  selectedDepth == DepthFormat::None && selectedSamples == 0,
              "exact render-target query preserves all requested values");

        exact = adapter.QueryRenderTargetFormat(
            GraphicsProfile::HiDef, SurfaceFormat::Color, DepthFormat::Depth16,
            kImpossibleSampleRequest, selectedFormat, selectedDepth, selectedSamples);
        renderTargetSampleCount_ = selectedSamples;
        Check(!exact, "impossible render-target sample request is substituted");
        Check(selectedFormat == SurfaceFormat::Color && selectedDepth == DepthFormat::Depth16,
              "render-target query preserves supported color and depth formats");
        Check(renderTargetSampleCount_ >= 4 &&
                  renderTargetSampleCount_ < kImpossibleSampleRequest &&
                  IsPowerOfTwo(renderTargetSampleCount_),
              "render-target query returns a real nonzero native MSAA clamp");

        exact = adapter.QueryRenderTargetFormat(
            GraphicsProfile::HiDef, SurfaceFormat::Dxt1, DepthFormat::Depth24Stencil8,
            kImpossibleSampleRequest, selectedFormat, selectedDepth, selectedSamples);
        Check(!exact, "block-compressed DXT1 is rejected as a render-target format");
        Check(selectedFormat == SurfaceFormat::Color,
              "unsupported render-target format falls back to Color");
        Check(selectedDepth == DepthFormat::Depth24Stencil8,
              "render-target depth request remains independently supported");
        Check(selectedSamples == renderTargetSampleCount_,
              "MSAA clamp is evaluated for the selected fallback format");

        exact = adapter.QueryBackBufferFormat(
            GraphicsProfile::HiDef, SurfaceFormat::Color,
            DepthFormat::Depth24Stencil8, 0,
            selectedFormat, selectedDepth, selectedSamples);
        Check(exact, "fixed Color/Depth24Stencil8 single-sample back buffer is exact");

        exact = adapter.QueryBackBufferFormat(
            GraphicsProfile::HiDef, SurfaceFormat::Color, DepthFormat::Depth16,
            kImpossibleSampleRequest, selectedFormat, selectedDepth, selectedSamples);
        backBufferSampleCount_ = selectedSamples;
        Check(!exact, "fixed back-buffer depth and impossible sample requests are substituted");
        Check(selectedFormat == SurfaceFormat::Color,
              "supported back-buffer format remains Color");
        Check(selectedDepth == DepthFormat::Depth24Stencil8,
              "back-buffer query reports the fixed applied D24S8 depth resource");
        Check(backBufferSampleCount_ == renderTargetSampleCount_,
              "back-buffer and render-target Color queries use the same native MSAA clamp");

        exact = adapter.QueryBackBufferFormat(
            GraphicsProfile::HiDef, SurfaceFormat::Rgba1010102, DepthFormat::None,
            kImpossibleSampleRequest, selectedFormat, selectedDepth, selectedSamples);
        Check(!exact, "non-Color back-buffer request is rejected");
        Check(selectedFormat == SurfaceFormat::Color &&
                  selectedDepth == DepthFormat::Depth24Stencil8,
              "back-buffer format and depth fall back to the resources the renderer creates");
        Check(selectedSamples == backBufferSampleCount_,
              "back-buffer MSAA clamp follows the selected Color fallback");

        exact = adapter.QueryRenderTargetFormat(
            GraphicsProfile::HiDef, SurfaceFormat::Color, DepthFormat::Depth16,
            kImpossibleSampleRequest, selectedFormat, selectedDepth, selectedSamples);
        Check(!exact && selectedSamples == renderTargetSampleCount_,
              "repeated adapter query has no state leakage from back-buffer queries");
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        GraphicsDevice& device = getGraphicsDeviceProperty();
        PresentationParameters requested =
            device.getPresentationParametersProperty().Clone();
        requested.setBackBufferFormatProperty(SurfaceFormat::Rgba1010102);
        requested.setDepthStencilFormatProperty(DepthFormat::Depth16);
        requested.setMultiSampleCountProperty(kImpossibleSampleRequest);
        device.Reset(requested);

        const PresentationParameters& applied =
            device.getPresentationParametersProperty();
        Check(applied.getBackBufferFormatProperty() == SurfaceFormat::Color,
              "device Reset applies the adapter query's fixed back-buffer format");
        Check(applied.getDepthStencilFormatProperty() == DepthFormat::Depth24Stencil8,
              "device Reset applies the adapter query's fixed depth format");
        Check(applied.getMultiSampleCountProperty() == backBufferSampleCount_,
              "device Reset applies the adapter query's native back-buffer MSAA clamp");

        RenderTarget2D target(
            device, 16, 16, false, SurfaceFormat::Color, DepthFormat::Depth16,
            kImpossibleSampleRequest, RenderTargetUsage::DiscardContents);
        std::printf(
            "[MEASURE] Color MSAA clamp: adapter RT=%d, adapter back buffer=%d, target=%d\n",
            renderTargetSampleCount_, backBufferSampleCount_,
            target.getMultiSampleCountProperty());
        Check(target.getFormatProperty() == SurfaceFormat::Color,
              "real render target uses the adapter-selected Color format");
        Check(target.getDepthStencilFormatProperty() == DepthFormat::Depth16,
              "real render target uses the independently selected Depth16 format");
        Check(target.getMultiSampleCountProperty() == renderTargetSampleCount_,
              "real render target applies the adapter query's native MSAA clamp");

        SurfaceFormat selectedFormat = SurfaceFormat::Bgr565;
        DepthFormat selectedDepth = DepthFormat::None;
        int selectedSamples = -1;
        const bool exact = device.getAdapterProperty().QueryBackBufferFormat(
            GraphicsProfile::HiDef, SurfaceFormat::Color, DepthFormat::Depth16,
            kImpossibleSampleRequest, selectedFormat, selectedDepth, selectedSamples);
        Check(!exact && selectedFormat == SurfaceFormat::Color &&
                  selectedDepth == DepthFormat::Depth24Stencil8 &&
                  selectedSamples == backBufferSampleCount_,
              "adapter result is stable after device creation and resource allocation");

        std::printf("=== %d/%d PASS ===\n", passed_, passed_ + failed_);
        Exit();
    }

public:
    GraphicsAdapterQueryContract()
    {
        QueryBeforeDeviceCreation();
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(160);
        gdm_->setPreferredBackBufferHeightProperty(120);
    }

    [[nodiscard]] int Result() const { return failed_ == 0 ? 0 : 1; }
};

int main()
{
    GraphicsAdapterQueryContract game;
    game.Run();
    return game.Result();
}
