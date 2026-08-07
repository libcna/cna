// SPDX-License-Identifier: MS-PL
// SKIA-15: selected raster present-interval policy and presenter-recovery persistence.

#include "CNA/Internal/Backends/Skia/SkiaGraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentInterval.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <array>
#include <cstdio>
#include <exception>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Backends::Skia::SkiaGraphicsBackend;

namespace
{
    struct IntervalCase
    {
        PresentInterval value;
        int exactOrPreferred;
        const char* name;
    };

    constexpr std::array<IntervalCase, 4> kCases{{
        {PresentInterval::Immediate, 0, "Immediate"},
        {PresentInterval::One, 1, "One"},
        {PresentInterval::Two, 2, "Two"},
        {PresentInterval::Default, 1, "Default"},
    }};
}

class SkiaPresentIntervalTest final : public Game
{
public:
    SkiaPresentIntervalTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(16);
        graphics_->setPreferredBackBufferHeightProperty(8);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto* backend = dynamic_cast<SkiaGraphicsBackend*>(&device.GetBackend());
        Check(backend != nullptr, "GraphicsDevice owns SkiaGraphicsBackend for interval diagnostics");
        if (!backend)
        {
            Exit();
            return;
        }

        for (const IntervalCase& interval : kCases)
        {
            PresentationParameters parameters = device.getPresentationParametersProperty().Clone();
            parameters.setPresentationIntervalProperty(interval.value);
            bool succeeded = true;
            try
            {
                device.Reset(parameters);
            }
            catch (const std::exception& exception)
            {
                succeeded = false;
                std::printf("       %s reset exception: %s\n", interval.name, exception.what());
            }
            Check(succeeded, interval.name, "Reset reaches the SDL presenter");
            if (!succeeded)
                continue;

            Check(device.getPresentationParametersProperty().getPresentationIntervalProperty()
                      == interval.value,
                  interval.name, "PresentationParameters round-trips the request");
            const int actual = backend->GetSwapIntervalEXT();
            const bool actualAccepted = interval.value == PresentInterval::Two
                ? (actual == 2 || actual == 1)
                : actual == interval.exactOrPreferred;
            Check(actualAccepted, interval.name,
                  interval.value == PresentInterval::Two
                      ? "actual interval is 2 or the documented driver clamp to 1"
                      : "actual interval matches the requested raster policy");
        }

        const int intervalBeforeRecovery = backend->GetSwapIntervalEXT();
        backend->DebugSimulateContextLoss();
        Check(backend->GetSwapIntervalEXT() == intervalBeforeRecovery,
              "presenter recovery reapplies the actual selected interval");

        device.Clear(Color(255, 128, 0, 255));
        device.Present();
        Color pixel(0, 0, 0, 0);
        const Rectangle region(8, 4, 1, 1);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        Check(pixel.getRProperty() == 255 && pixel.getGProperty() == 128
                  && pixel.getBProperty() == 0 && pixel.getAProperty() == 255,
              "Clear/readback/Present remains exact after all interval transitions");
        Exit();
    }

private:
    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass)
            ++failures_;
    }

    void Check(bool pass, const char* interval, const char* label)
    {
        std::printf("[%s] %s: %s\n", pass ? "PASS" : "FAIL", interval, label);
        if (!pass)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaPresentIntervalTest game;
    game.Run();
    return game.Result();
}
