// SPDX-License-Identifier: MS-PL
// A multisample count the device cannot apply is clamped, and what PresentationParameters and
// RenderTarget2D then report is the count really applied -- never the request echoed back.
//
// Moved from the retired VULKAN suite (vulkan_applied_multisample_test.cpp, VULKAN-347), which drove
// requests 1-16 through the renderer itself; this is the same contract through the public API, so it
// holds every family to it (RRC-047). FNA's rule is the reference: GraphicsDevice and RenderTarget2D
// apply min(the device's maximum, ClosestMSAAPower(request)), where ClosestMSAAPower rounds DOWN to a
// power of two and maps 1 to 0 (FNA MathHelper.ClosestMSAAPower; XNA likewise answers through one
// QueryFormat for both). No device maximum is assumed, so the oracle is the rule's shape:
//
//   A  every applied count is 0 or a power of two >= 2 -- never 1, never 3, 5, 7 or 9
//   B  no request is rounded UP (applied <= ClosestMSAAPower(request))
//   C  a non-power request behaves as the power below it: 3 as 2, 5 and 7 as 4, 9 as 8; 1 as 0
//   D  the applied count never falls as the request rises
//   E  a device that reports MultiSampleAntiAliasing multisamples the largest request; one that
//      does not applies 0 everywhere (which counts it offers is its own: WebGPU offers only 4x, so
//      2x and 3x round down to 0 there, as B requires)
//   F  a Color RenderTarget2D's MultiSampleCount follows the same rule and agrees with the back
//      buffer's for the same request
//   G  Reset is repeatable: the same request applies the same count again
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
#include "CNA/GraphicsCapability.hpp"

#include <cstdio>
#include <exception>
#include <map>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRequests[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 16};

    /// FNA's MathHelper.ClosestMSAAPower: the power of two at or below @p value; 1 becomes 0.
    int ClosestMsaaPower(int value)
    {
        if (value <= 1) return 0;
        int power = 1;
        while (power * 2 <= value) power *= 2;
        return power;
    }

    bool IsAppliable(int count)
    {
        return count == 0 || (count >= 2 && (count & (count - 1)) == 0);
    }
}

class MultiSampleCountClampTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool hiDef_ = false;
    bool done_ = false;
    int passed_ = 0;
    int failed_ = 0;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        condition ? ++passed_ : ++failed_;
    }

    /// Resets to @p request and returns what PresentationParameters reports, or -1 if Reset threw.
    int ApplyBackBuffer(GraphicsDevice& device, int request)
    {
        PresentationParameters wanted = device.getPresentationParametersProperty().Clone();
        wanted.setMultiSampleCountProperty(request);
        try
        {
            device.Reset(wanted);
        }
        catch (const std::exception& e)
        {
            Check(false, "Reset with MultiSampleCount " + std::to_string(request) +
                             " threw instead of clamping: " + e.what());
            return -1;
        }
        return device.getPresentationParametersProperty().getMultiSampleCountProperty();
    }

    /// The MultiSampleCount a Color render target reports for @p request, or -1 if it threw.
    int ApplyRenderTarget(GraphicsDevice& device, int request)
    {
        try
        {
            RenderTarget2D target(device, 16, 16, false, SurfaceFormat::Color, DepthFormat::None,
                                  request, RenderTargetUsage::DiscardContents);
            return target.getMultiSampleCountProperty();
        }
        catch (const std::exception& e)
        {
            Check(false, "a Color RenderTarget2D with MultiSampleCount " + std::to_string(request) +
                             " threw instead of clamping: " + e.what());
            return -1;
        }
    }

    void CheckRule(const std::map<int, int>& applied, const char* what)
    {
        int previous = 0;
        for (const int request : kRequests)
        {
            const int count = applied.at(request);
            const std::string tag = std::string(what) + " request " + std::to_string(request) +
                                    " -> " + std::to_string(count);
            Check(IsAppliable(count), "A " + tag + ": 0 or a power of two >= 2");
            Check(count <= ClosestMsaaPower(request), "B " + tag + ": not rounded up (at most " +
                                                          std::to_string(ClosestMsaaPower(request)) + ")");
            if (ClosestMsaaPower(request) != request)
            {
                const int power = ClosestMsaaPower(request);
                Check(count == applied.at(power),
                      "C " + tag + ": the same as the power below it, " + std::to_string(power) +
                          " -> " + std::to_string(applied.at(power)));
            }
            Check(count >= previous, "D " + tag + ": not below the smaller request's " +
                                         std::to_string(previous));
            previous = count;
        }
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
        const int original = device.getPresentationParametersProperty().getMultiSampleCountProperty();

        std::map<int, int> backBuffer;
        std::map<int, int> renderTarget;
        for (const int request : kRequests)
        {
            backBuffer[request] = ApplyBackBuffer(device, request);
            renderTarget[request] = ApplyRenderTarget(device, request);
            std::printf("[MEASURE] request %d: back buffer %d, Color render target %d\n", request,
                        backBuffer[request], renderTarget[request]);
        }
        CheckRule(backBuffer, "back buffer");
        CheckRule(renderTarget, "Color RenderTarget2D");

        const bool msaa = device.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing);
        if (msaa)
        {
            Check(backBuffer.at(16) >= 2 && renderTarget.at(16) >= 2,
                  "E MultiSampleAntiAliasing is reported, so a 16x request is multisampled (back "
                  "buffer " + std::to_string(backBuffer.at(16)) + ", target " +
                      std::to_string(renderTarget.at(16)) + ")");
        }
        else
        {
            Check(backBuffer.at(16) == 0 && renderTarget.at(16) == 0,
                  "E MultiSampleAntiAliasing is not reported, so nothing is multisampled (16x -> " +
                      std::to_string(backBuffer.at(16)) + ", target " +
                      std::to_string(renderTarget.at(16)) + ")");
        }

        for (const int request : kRequests)
        {
            Check(renderTarget.at(request) == backBuffer.at(request),
                  "F request " + std::to_string(request) + ": the Color render target (" +
                      std::to_string(renderTarget.at(request)) + ") and the back buffer (" +
                      std::to_string(backBuffer.at(request)) + ") are clamped alike");
        }

        for (const int request : {3, 9})
        {
            const int again = ApplyBackBuffer(device, request);
            Check(again == backBuffer.at(request),
                  "G Reset to " + std::to_string(request) + " again applies " +
                      std::to_string(again) + ", as the first time (" +
                      std::to_string(backBuffer.at(request)) + ")");
        }

        ApplyBackBuffer(device, original);
        std::printf("=== %d/%d PASS ===\n", passed_, passed_ + failed_);
        Exit();
    }

public:
    MultiSampleCountClampTest()
    {
        hiDef_ = GraphicsAdapter::getDefaultAdapterProperty().IsProfileSupported(GraphicsProfile::HiDef);
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(hiDef_ ? GraphicsProfile::HiDef : GraphicsProfile::Reach);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int Result() const
    {
        if (failed_ > 0) return 1;
        if (!hiDef_)
        {
            std::printf("[SKIP] the default adapter does not support HiDef\n");
            return 77;
        }
        return 0;
    }
};

int main()
{
    MultiSampleCountClampTest game;
    game.Run();
    return game.Result();
}
