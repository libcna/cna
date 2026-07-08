// SPDX-License-Identifier: MS-PL
// Task 714: Decide and document MultiSampleCount behavior on SDL_Renderer.
//
// Decision: SDL_Renderer's 2D blit pipeline has no MSAA control at all -- ACCEPT any requested
// MultiSampleCount without throwing (log once so a game that expects MSAA and doesn't see it has
// a diagnostic trail), always reporting back 0 for the real, device-clamped value. Rationale for
// accepting rather than throwing: SpriteBatch's 2D draws have no anti-aliasing seams to smooth
// over in the first place (unlike 3D geometry edges), so a caller requesting MSAA purely for
// portability with the other 3 MSAA-capable backends, without depending on any particular sample
// count actually being honored, hits no functional difference on this backend and should not be
// penalized with a throw for an inert, unactionable request -- the same reasoning shape as Task
// 708's DepthFormat decision.
//
// GraphicsDevice::Reset() already writes ApplyMultiSampleCount()'s return value back into
// PresentationParameters.MultiSampleCount (Task 902's FNA3D_GetMaxMultiSampleCount write-back
// convention), so this decision needed only a real IGraphicsBackend::ApplyMultiSampleCount
// override on SdlGraphicsBackend (previously relying on the shared default, which already
// returned 0 -- this override adds the diagnostic log without changing the returned value).
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this backend's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Exit code 0 = all checks PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlMultiSampleCountDecisionTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        check(dev.getPresentationParametersProperty().getMultiSampleCountProperty() == 0,
              "Default PresentationParameters.MultiSampleCount is 0");

        // Requesting a real MSAA sample count must NOT throw on SDL_Renderer.
        PresentationParameters pp = dev.getPresentationParametersProperty();
        pp.setMultiSampleCountProperty(4);
        bool threw = false;
        std::string message;
        try
        {
            dev.Reset(pp);
        }
        catch (const std::exception& e)
        {
            threw = true;
            message = e.what();
        }
        check(!threw, "Requesting MultiSampleCount=4 via Reset() does not throw on SDL_Renderer");
        if (threw) std::printf("       threw: %s\n", message.c_str());

        // The real, device-clamped value written back must be 0 -- SDL_Renderer's genuine maximum.
        check(dev.getPresentationParametersProperty().getMultiSampleCountProperty() == 0,
              "PresentationParameters.MultiSampleCount is clamped back to 0 after requesting 4 (matches FNA's device-clamped write-back)");

        // The device must remain fully functional after the unsupported MSAA request.
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.Clear(Color(0, 0, 255, 255));
        Color pixel(0, 0, 0, 0);
        const auto& vp = dev.getViewportProperty();
        const Rectangle region(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        check(pixel.getRProperty() <= 15 && pixel.getGProperty() <= 15 && pixel.getBProperty() >= 240,
              "GraphicsDevice remains fully functional after the unsupported MultiSampleCount request");

        Exit();
    }

public:
    SdlMultiSampleCountDecisionTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlMultiSampleCountDecisionTest game;
    game.Run();
    return game.getResult();
}
