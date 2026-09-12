// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "System/IDisposable.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    class RlglOcclusionQueryLifetimeTest final : public Game
    {
    public:
        RlglOcclusionQueryLifetimeTest()
            : graphics_(std::make_unique<GraphicsDeviceManager>(this))
        {
        }

        [[nodiscard]] int Result() const noexcept
        {
            return failures_ == 0 ? 0 : 1;
        }

    protected:
        void Draw(const GameTime&) override
        {
            if (finished_) return;
            finished_ = true;

            auto& device = getGraphicsDeviceProperty();
            auto* query = new OcclusionQuery(device);
            Check(query->HasRenderer(), "query owns its GL renderer before device disposal");
            query->Begin();

            device.Dispose();
            Check(query->getIsDisposedProperty(),
                  "device disposal marks its tracked query disposed");
            Check(!query->HasRenderer(),
                  "device disposal releases an active query before context shutdown");
            Check(!query->getIsCompleteProperty() && query->getPixelCountProperty() == 0,
                  "a disposed query reports safe default values");

            bool repeatedDisposeSucceeded = true;
            try
            {
                static_cast<System::IDisposable*>(query)->Dispose();
            }
            catch (...)
            {
                repeatedDisposeSucceeded = false;
            }
            Check(repeatedDisposeSucceeded, "repeated query disposal is idempotent");

            bool delayedDestructionSucceeded = true;
            try
            {
                delete query;
            }
            catch (...)
            {
                delayedDestructionSucceeded = false;
            }
            Check(delayedDestructionSucceeded,
                  "query destruction after context shutdown performs no GL call");
            Exit();
        }

    private:
        void Check(const bool condition, const char* label)
        {
            std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
            if (!condition) ++failures_;
        }

        std::unique_ptr<GraphicsDeviceManager> graphics_;
        bool finished_ = false;
        int failures_ = 0;
    };
}

int main()
{
    RlglOcclusionQueryLifetimeTest game;
    game.Run();
    return game.Result();
}
