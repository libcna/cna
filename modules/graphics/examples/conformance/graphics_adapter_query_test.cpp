// SPDX-License-Identifier: MS-PL
// GraphicsAdapter.QueryRenderTargetFormat / QueryBackBufferFormat answer before a GraphicsDevice
// exists, and their answer is exactly what RenderTarget2D and GraphicsDevice.Reset then apply.
//
// In Microsoft XNA 4.0 both queries are one routine, GraphicsAdapter.QueryFormat, which returns true
// only when the selected format, depth format and multisample count all equal the request. The same
// routine is what the device uses: RenderTarget2D.CreateRenderTarget passes its arguments through
// Adapter.QueryFormat(isBackBuffer: false, ...) and builds the surfaces from the selected values, and
// GraphicsDevice.ConvertPresentationParametersToNative passes the presentation parameters through
// QueryFormat(isBackBuffer: true, ...) before Reset. So a query is a promise, and this program holds
// the device to it rather than to any one renderer's format table:
//   * exact requests stay exact, and the return value is exactly "nothing was substituted";
//   * an impossible multisample request (1024) is substituted by a count below it, 0 or a power of
//     two, and the same clamp is reported for the fallback format a refused one selects;
//   * a block-compressed DXT1 surface is never a render-target format, so its query selects Color,
//     and a DXT1 RenderTarget2D is created as Color (XNA builds it from the selected format);
//   * GraphicsDeviceManager's default depth request is XNA's Depth24;
//   * Reset and RenderTarget2D apply the selected format, depth and sample count;
//   * the answers do not change once the device and its resources exist.
//
// First written for the retired DIRECTX11 renderer (DX-245, graphics shared cleanup
// GSC-0005); the Direct3D-specific fixed back-buffer table and its >= 4x MSAA floor are not carried
// over. That original, graphics_adapter_query_contract_test.cpp, was removed in RRC-047 once the two
// renderer-neutral checks only it made had been moved here.
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = the default adapter does not support HiDef.

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
#include <exception>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kImpossibleSampleRequest = 1024;

    struct Answer
    {
        bool exact = false;
        SurfaceFormat format = SurfaceFormat::Bgr565;
        DepthFormat depth = DepthFormat::None;
        int samples = -1;
    };

    bool IsZeroOrPowerOfTwo(int value)
    {
        return value == 0 || (value > 0 && (value & (value - 1)) == 0);
    }

    std::string Describe(const Answer& a)
    {
        return std::string(a.exact ? "exact" : "substituted") + " format=" +
               std::to_string(static_cast<int>(a.format)) + " depth=" +
               std::to_string(static_cast<int>(a.depth)) + " samples=" + std::to_string(a.samples);
    }
}

class GraphicsAdapterQueryTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int renderTargetClamp_ = -1;
    int backBufferClamp_ = -1;
    int passed_ = 0;
    int failed_ = 0;
    bool done_ = false;
    bool hiDef_ = true;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        condition ? ++passed_ : ++failed_;
    }

    // XNA's QueryFormat returns true exactly when nothing was substituted; every answer is checked
    // against that definition as well as against the case's own expectation.
    Answer Query(GraphicsAdapter& adapter, bool backBuffer, SurfaceFormat format, DepthFormat depth,
                 int samples, const char* label)
    {
        Answer a;
        a.exact = backBuffer
            ? adapter.QueryBackBufferFormat(GraphicsProfile::HiDef, format, depth, samples,
                                            a.format, a.depth, a.samples)
            : adapter.QueryRenderTargetFormat(GraphicsProfile::HiDef, format, depth, samples,
                                              a.format, a.depth, a.samples);
        const bool unchanged = a.format == format && a.depth == depth && a.samples == samples;
        Check(a.exact == unchanged,
              std::string(label) + ": the return value says whether anything was substituted (" +
                  Describe(a) + ")");
        return a;
    }

    void QueryBeforeDeviceCreation()
    {
        GraphicsAdapter& adapter = GraphicsAdapter::getDefaultAdapterProperty();
        Check(adapter.IsProfileSupported(GraphicsProfile::Reach), "the default adapter supports Reach");
        hiDef_ = adapter.IsProfileSupported(GraphicsProfile::HiDef);
        if (!hiDef_)
            return;

        Answer a = Query(adapter, false, SurfaceFormat::Color, DepthFormat::None, 0, "RT Color/None/0");
        Check(a.exact && a.format == SurfaceFormat::Color && a.depth == DepthFormat::None && a.samples == 0,
              "a single-sample Color render target is supported exactly");

        a = Query(adapter, false, SurfaceFormat::Color, DepthFormat::Depth16, kImpossibleSampleRequest,
                  "RT Color/Depth16/1024");
        renderTargetClamp_ = a.samples;
        Check(!a.exact, "an impossible render-target sample count is substituted");
        Check(a.format == SurfaceFormat::Color && a.depth == DepthFormat::Depth16,
              "the substitution keeps the supported Color and Depth16 requests");
        Check(a.samples < kImpossibleSampleRequest && IsZeroOrPowerOfTwo(a.samples),
              "the substituted sample count is below the request, and 0 or a power of two (got " +
                  std::to_string(a.samples) + ")");

        a = Query(adapter, false, SurfaceFormat::Dxt1, DepthFormat::Depth24Stencil8, kImpossibleSampleRequest,
                  "RT Dxt1/Depth24Stencil8/1024");
        Check(!a.exact && a.format == SurfaceFormat::Color,
              "block-compressed DXT1 is not a render-target format; the query selects Color");
        Check(a.depth == DepthFormat::Depth24Stencil8, "the depth request is answered independently");
        Check(a.samples == renderTargetClamp_,
              "the multisample clamp is the one for the selected Color format");

        a = Query(adapter, true, SurfaceFormat::Color, DepthFormat::Depth24Stencil8, 0, "BB Color/Depth24Stencil8/0");
        Check(a.exact, "a single-sample Color/Depth24Stencil8 back buffer is supported exactly");

        a = Query(adapter, true, SurfaceFormat::Color, DepthFormat::Depth16, 0, "BB Color/Depth16/0");
        Check(a.exact && a.depth == DepthFormat::Depth16,
              "a single-sample Color/Depth16 back buffer keeps its Depth16 request");

        a = Query(adapter, true, SurfaceFormat::Color, DepthFormat::Depth16, kImpossibleSampleRequest,
                  "BB Color/Depth16/1024");
        backBufferClamp_ = a.samples;
        Check(!a.exact && a.format == SurfaceFormat::Color && a.depth == DepthFormat::Depth16,
              "an impossible back-buffer sample count is substituted, keeping Color and Depth16");
        Check(a.samples < kImpossibleSampleRequest && IsZeroOrPowerOfTwo(a.samples),
              "the substituted back-buffer sample count is below the request, and 0 or a power of two (got " +
                  std::to_string(a.samples) + ")");
        // XNA answers both queries through one QueryFormat routine, so for the same Color format
        // the back buffer and a render target are clamped alike (carried over from
        // graphics_adapter_query_contract_test.cpp, RRC-047).
        Check(backBufferClamp_ == renderTargetClamp_,
              "the back-buffer and render-target Color queries select the same multisample clamp (" +
                  std::to_string(backBufferClamp_) + " vs " + std::to_string(renderTargetClamp_) + ")");

        a = Query(adapter, true, SurfaceFormat::Rgba1010102, DepthFormat::None, kImpossibleSampleRequest,
                  "BB Rgba1010102/None/1024");
        Check(a.depth == DepthFormat::None,
              "a None depth request is kept whichever back-buffer format the query selects");
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        if (!hiDef_)
        {
            Exit();
            return;
        }

        GraphicsDevice& device = getGraphicsDeviceProperty();
        GraphicsAdapter& adapter = device.getAdapterProperty();
        Check(device.getPresentationParametersProperty().getDepthStencilFormatProperty() == DepthFormat::Depth24,
              "GraphicsDeviceManager's default depth request, XNA's Depth24, is applied");

        // Reset applies the back-buffer query's answer.
        const Answer promised = Query(adapter, true, SurfaceFormat::Rgba1010102, DepthFormat::Depth16,
                                      kImpossibleSampleRequest, "BB Rgba1010102/Depth16/1024");
        PresentationParameters requested = device.getPresentationParametersProperty().Clone();
        requested.setBackBufferFormatProperty(SurfaceFormat::Rgba1010102);
        requested.setDepthStencilFormatProperty(DepthFormat::Depth16);
        requested.setMultiSampleCountProperty(kImpossibleSampleRequest);
        try
        {
            device.Reset(requested);
            const PresentationParameters& applied = device.getPresentationParametersProperty();
            Check(applied.getBackBufferFormatProperty() == promised.format,
                  "Reset applies the back-buffer format the query selected (applied " +
                      std::to_string(static_cast<int>(applied.getBackBufferFormatProperty())) + ", query " +
                      std::to_string(static_cast<int>(promised.format)) + ")");
            Check(applied.getDepthStencilFormatProperty() == promised.depth,
                  "Reset applies the depth format the query selected");
            Check(applied.getMultiSampleCountProperty() == promised.samples,
                  "Reset applies the sample count the query selected (applied " +
                      std::to_string(applied.getMultiSampleCountProperty()) + ", query " +
                      std::to_string(promised.samples) + ")");
        }
        catch (const std::exception& e)
        {
            Check(false, std::string("Reset with a substitutable request threw instead of applying the query: ") +
                             e.what());
        }

        // RenderTarget2D applies the render-target query's answer.
        try
        {
            RenderTarget2D target(device, 16, 16, false, SurfaceFormat::Color, DepthFormat::Depth16,
                                  kImpossibleSampleRequest, RenderTargetUsage::DiscardContents);
            std::printf("[MEASURE] Color multisample clamp: RT query=%d, back-buffer query=%d, target=%d\n",
                        renderTargetClamp_, backBufferClamp_, target.getMultiSampleCountProperty());
            Check(target.getFormatProperty() == SurfaceFormat::Color,
                  "a RenderTarget2D uses the Color format the query kept");
            Check(target.getDepthStencilFormatProperty() == DepthFormat::Depth16,
                  "a RenderTarget2D uses the Depth16 format the query kept");
            Check(target.getMultiSampleCountProperty() == renderTargetClamp_,
                  "a RenderTarget2D applies the sample count the query selected (target " +
                      std::to_string(target.getMultiSampleCountProperty()) + ", query " +
                      std::to_string(renderTargetClamp_) + ")");
        }
        catch (const std::exception& e)
        {
            Check(false, std::string("creating the Color/Depth16/1024 render target threw: ") + e.what());
        }

        try
        {
            RenderTarget2D compressed(device, 16, 16, false, SurfaceFormat::Dxt1, DepthFormat::None, 0,
                                      RenderTargetUsage::DiscardContents);
            Check(compressed.getFormatProperty() == SurfaceFormat::Color,
                  "a DXT1 RenderTarget2D is created with the Color format the query selects (got " +
                      std::to_string(static_cast<int>(compressed.getFormatProperty())) + ")");
        }
        catch (const std::exception& e)
        {
            Check(false, std::string("a DXT1 RenderTarget2D threw instead of taking the query's Color: ") + e.what());
        }

        const Answer after = Query(adapter, true, SurfaceFormat::Color, DepthFormat::Depth16,
                                   kImpossibleSampleRequest, "BB Color/Depth16/1024 after device creation");
        Check(!after.exact && after.format == SurfaceFormat::Color && after.depth == DepthFormat::Depth16 &&
                  after.samples == backBufferClamp_,
              "the adapter's answer is unchanged once the device and its resources exist");
        const Answer rtAfter = Query(adapter, false, SurfaceFormat::Color, DepthFormat::Depth16,
                                     kImpossibleSampleRequest, "RT Color/Depth16/1024 after device creation");
        Check(rtAfter.samples == renderTargetClamp_,
              "repeated render-target queries do not drift after back-buffer queries and a Reset");

        std::printf("=== %d/%d PASS ===\n", passed_, passed_ + failed_);
        Exit();
    }

public:
    GraphicsAdapterQueryTest()
    {
        QueryBeforeDeviceCreation();
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(hiDef_ ? GraphicsProfile::HiDef : GraphicsProfile::Reach);
        gdm_->setPreferredBackBufferWidthProperty(160);
        gdm_->setPreferredBackBufferHeightProperty(120);
    }

    [[nodiscard]] int Result() const
    {
        if (failed_ > 0) return 1;
        if (!hiDef_)
        {
            std::printf("[SKIP] the default adapter does not support HiDef, which every query here asks about\n");
            return 77;
        }
        return 0;
    }
};

int main()
{
    GraphicsAdapterQueryTest game;
    game.Run();
    return game.Result();
}
