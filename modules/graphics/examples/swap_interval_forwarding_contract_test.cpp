// SPDX-License-Identifier: MS-PL
// plans/plan_dx.md DX-217: renderer-neutral forwarding proof for presentation interval changes.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;

class SwapIntervalForwardingContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    bool done_ = false;
    int passed_ = 0;
    int result_ = 1;

    void Check(bool condition, const char* label, int value)
    {
        std::printf("[%s] %s (interval=%d)\n", condition ? "PASS" : "FAIL", label, value);
        if (condition)
            ++passed_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& renderer = getGraphicsDeviceProperty().GetRenderer();
        manager_->setSynchronizeWithVerticalRetraceProperty(false);
        manager_->ApplyChanges();
        Check(renderer.GetSwapIntervalEXT() == 0,
              "SynchronizeWithVerticalRetrace=false forwards interval zero",
              renderer.GetSwapIntervalEXT());

        manager_->setSynchronizeWithVerticalRetraceProperty(true);
        manager_->ApplyChanges();
        Check(renderer.GetSwapIntervalEXT() == 1,
              "SynchronizeWithVerticalRetrace=true forwards interval one",
              renderer.GetSwapIntervalEXT());

        std::printf("=== %d/2 PASS ===\n", passed_);
        result_ = passed_ == 2 ? 0 : 1;
        Exit();
    }

public:
    SwapIntervalForwardingContractTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    SwapIntervalForwardingContractTest game;
    game.Run();
    return game.Result();
}
